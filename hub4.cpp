#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <tuple>
#include <queue>
#include <boost/program_options.hpp>
#include <thread>
#include <functional>
#include <signal.h>
#include <atomic>
#include "utils.h"
namespace bpo = boost::program_options;
using std::string;
using _lock_t = std::lock_guard<std::mutex>;

constexpr const int HEALTH_CHECK_DELAY = 5;
std::atomic<bool>   _srving(false); // srving client
std::mutex          _qlock;         // queue lock
std::queue<SOCK>    _queue;         // connection queue
int                 _cport;         // port for client
int                 _ep_port;       // port for endpoint
int                 _epc_port;      // port for epc
SOCK                _ctrl;          // ctrl socket
SOCK                _sc;            // sock for client
SOCK                _sep;           // sock for endpoint
SOCK                _sepc;          // sock for epc

bool reg_opt(int argc, char** argv, bpo::variables_map& vm) {
    bpo::options_description opts("tunnel hub");
    opts.add_options()                                                                                 //
        ("help,h", "show help info")                                                                   //
        ("cli,c", bpo::value<int>(&_cport)->default_value(9999)->value_name("port"), "client port")    //
        ("ep,e", bpo::value<int>(&_ep_port)->default_value(9000)->value_name("port"), "endpoint port") //
        ("epc,p", bpo::value<int>(&_epc_port)->default_value(9001)->value_name("port"), "epc port");

    try {
        store(bpo::command_line_parser(argc, argv).options(opts).run(), vm);
    } catch (bpo::error_with_no_option_name& ex) {
        printf("%s\n", ex.what());
        goto err;
    }
    notify(vm);

    if (vm.count("help")) goto err;
    return false;
err:
    std::cout << opts << std::endl;
ret:
    return true;
}

int setup_socket(SOCK& sock, int port) {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "failed create socket\n");
        return 1;
    }
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    int ret              = bind(sock, (sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        fprintf(stderr, "falied bind port %d\n", port);
        return 1;
    }
    return 0;
}

int ctrl_msg(const string& msg) {
    if (send(_ctrl, msg.c_str(), msg.length(), 0) < 0) {
        _srving = false; // ep disconnected
        printf("ctrl fail, ep lost.\n");
        return 1;
    }
    return 0;
}

// request and wait for new epc
SOCK next_ep_connection() {
    constexpr const int _EP_WAIT_TIMEOUT = 200; // ms
    constexpr const int _EP_REQ_LIMIT    = 3;   // request 3 times, if no valid new connection from ep, quit
    for (int req = 0; req < _EP_REQ_LIMIT; ++req) {
        if (ctrl_msg("c")) return -1; // request new ep connnection
        long start = cur_ms();

        while (true) {
            if (_queue.empty()) {
                if (cur_ms() - start >= _EP_WAIT_TIMEOUT)
                    break;
                continue;
            }
            {
                _lock_t l(_qlock);
                if (_queue.empty()) continue; // no valid ep connection
                SOCK s = _queue.front();
                _queue.pop();
                return s;
            }
        }
    }
    perror("arrive request limit, no valid ep connection\n");
    return -1;
}

// rcv epc
void rcv_ep() {
    printf("start rcv epc...\n");
    while (_srving) {
        auto [s, ip, port] = _accept_conn(_sepc); // ctrl connection
        if (s > 0) {
            _lock_t g(_qlock);
            _queue.push(s);
        }
        printf("rcv epc[%d]: [%s:%d]\n", s, ip.c_str(), port);
    }
    printf("stop rcv epc...\n");
}

void clear() {
    if (_ctrl) close(_ctrl);
    if (_sc) close(_sc);
    if (_sep) close(_sep);
}

void dispatch_new_cli(SOCK c) {
    SOCK ep = next_ep_connection();
    if (ep < 0) {
        printf("no valid epc for cli[%d], close.\n", c);
        close(c); // no spare connection, refuse client connection
    } else {
        create_tunnel(c, ep);
    }
}

// srv client
int srv_cli() {
    printf("start srving client...\n");
    while (_srving) {
        auto [c, ip, port] = _accept_conn(_sc);
        if (c < 0) continue;
        printf("client[%d %s:%d] connect.\n", c, ip.c_str(), port);
        std::thread(std::bind(dispatch_new_cli, c)).detach();
    }
    printf("stop srving client...\n");
    return 0;
}


void srv() {
    listen(_sc, 100);
    listen(_sep, 100);
    listen(_sepc, 100);
    _srving = false;

    string msg_epc_port; // construct epc port msg here
    {
        char buff[16];
        sprintf(buff, "%d", _epc_port);
        msg_epc_port = buff;
    }

    printf("Hub srving at ep[:%d], epc[:%d] cli[:%d]\nwaiting for ep...\n", _ep_port, _epc_port, _cport);

    while (true) {
        // first, wait for ep ctrl connection.
        {
            auto [s, ip, port] = _accept_conn(_sep); // accept ctrl connection first
            if (s < 0) continue;
            _ctrl = s;
            printf("\nnew ep[%d %s:%d] connected.\n", _ctrl, ip.c_str(), port);
        }

        // got ctrl, start srving cli
        _srving = true;
        ctrl_msg(msg_epc_port); // tell ep [epc port], add ack later
        std::thread(rcv_ep).detach();
        std::thread(srv_cli).detach();

        // ep health check
        while (_srving) {
            sleep(HEALTH_CHECK_DELAY);
            ctrl_msg("h"); // empty msg, just check connection.
        }
        printf("ep lost, waiting for new one...\n");
        close(_ctrl); // close it
    }
}

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    bpo::variables_map vm;
    if (reg_opt(argc, argv, vm)) return 0;
    if (setup_socket(_sep, _ep_port)) return 1;
    if (setup_socket(_sepc, _epc_port)) return 1;
    if (setup_socket(_sc, _cport)) return 1;

    srv();

quit:
    clear();
    return 0;
}