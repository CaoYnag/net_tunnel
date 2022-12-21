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

typedef std::tuple<SOCK, long> _sock_ts; // SOCK and accept timestamp

std::atomic<bool>    _srving(false); // srving client
std::mutex           _qlock;         // queue lock
std::queue<_sock_ts> _queue;         // connection queue
int                  _cport;         // port for client
int                  _ep_port;       // port for endpoint
int                  _epc_port;      // port for epc
SOCK                 _ctrl;          // ctrl socket
SOCK                 _sc;            // sock for client
SOCK                 _sep;           // sock for endpoint
SOCK                 _sepc;          // sock for epc
std::string          _psw;           // psw
std::string          _ep_ip;         // ep ip

// addtional option
int HEALTH_CHECK_FREQUENCE = 5;                   // s, how long to check status of ep
int EPC_REQUEST_LIMIT      = 3;                   // 3 times
int EPC_REQUEST_TIMEOUT    = 1000;                // ms, timeout client request epc each time
int EPC_EXPIRE_TIME        = 5000;                // ms, how long a unused epc would be drop
int EP_VERIFY_TIMEOUT      = EPC_REQUEST_TIMEOUT; // NOTICE: kept this value same as EPC_REQUEST_TIMEOUT in reg_opt

void show_conf() {
    printf("Hub Configuration:\nep[:%d], epc[:%d] cli[:%d]\n", _ep_port, _epc_port, _cport);
    printf("ep verify password : %s\n", _psw.c_str());
    printf("ep verify timeout  : %dms\n", EP_VERIFY_TIMEOUT);
    printf("ep health check    : %ds\n", HEALTH_CHECK_FREQUENCE);
    printf("epc request limit  : %d times\n", EPC_REQUEST_LIMIT);
    printf("epc request timeout: %dms\n", EPC_REQUEST_TIMEOUT);
    printf("epc expire time    : %dms\n", EPC_EXPIRE_TIME);
}

bool reg_opt(int argc, char** argv, bpo::variables_map& vm) {
    bpo::options_description opts("tunnel hub");
    opts.add_options()                                                                                        //
        ("help,h", "show help info")                                                                          //
        ("psw", bpo::value<std::string>(&_psw), "password")                                                   //
        ("cli,c", bpo::value<int>(&_cport)->default_value(7777)->value_name("port"), "port for user(client)") //
        ("ep,e", bpo::value<int>(&_ep_port)->default_value(9559)->value_name("port"), "port for ep(ctrl)")    //
        ("epc,p", bpo::value<int>(&_epc_port)->default_value(25565)->value_name("port"), "port to rcv epc")   //
        // addtional option
        ("ep_healthcheck", bpo::value<int>(&HEALTH_CHECK_FREQUENCE)->default_value(5)->value_name("s"), "ep health check frequence")      //
        ("epc_limit", bpo::value<int>(&EPC_REQUEST_LIMIT)->default_value(3)->value_name("n"), "epc request limit for each client")        //
        ("epc_timeout", bpo::value<int>(&EPC_REQUEST_TIMEOUT)->default_value(1000)->value_name("ms"), "timeout wait epc in each request") //
        ("epc_expire", bpo::value<int>(&EPC_EXPIRE_TIME)->default_value(5000)->value_name("ms"), "epc port")                              //
        ;

    try {
        store(bpo::command_line_parser(argc, argv).options(opts).run(), vm);
    } catch (bpo::error_with_no_option_name& ex) {
        printf("%s\n", ex.what());
        goto err;
    }
    notify(vm);

    if (vm.count("help")) goto err;
    if (!vm.count("psw")) {
        printf("pls set a password\n");
        goto err;
    }
    EP_VERIFY_TIMEOUT = EPC_REQUEST_TIMEOUT;
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
    for (int req = 0; req < EPC_REQUEST_LIMIT; ++req) {
        if (ctrl_msg("c")) return -1; // request new ep connnection
        long start = cur_ms();

        while (true) {
            if (_queue.empty()) {
                if (cur_ms() - start >= EPC_REQUEST_TIMEOUT)
                    break;
                continue;
            }
            {
                _lock_t l(_qlock);
                if (_queue.empty()) continue; // no valid ep connection
                auto [s, ts] = _queue.front();
                _queue.pop();
                if (ts - start >= EPC_EXPIRE_TIME) // expired epc, drop it.
                {
                    close(s);
                    continue;
                }
                return s;
            }
        }
    }
    perror("arrive request limit, no valid ep connection\n");
    return -1;
}

// rcv epc
void rcv_epc() {
    printf("start rcv epc...\n");
    while (_srving) {
        auto [s, ip, port] = _accept_conn(_sepc);
        if (s <= 0) continue;
        if (ip != _ep_ip) {
            printf("bad epc[%d]: [%s:%d], reject...\n", s, ip.c_str(), port);
            close(s);
            continue;
        }
        {
            _lock_t g(_qlock);
            _queue.emplace(s, cur_ms());
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
        printf("[%s] client[%d %s:%d] connect.\n", cur_time().c_str(), c, ip.c_str(), port);
        std::thread(std::bind(dispatch_new_cli, c)).detach();
    }
    printf("stop srving client...\n");
    return 0;
}

int _accept_ep() {
    auto [s, ip, port] = _accept_conn(_sep);
    if (s < 0) return 1;
    printf("\nnew ep[%d %s:%d] connect.\n", s, ip.c_str(), port);

    auto psw = _rcv_msg(s, EP_VERIFY_TIMEOUT);
    if (psw != _psw) {
        printf("bad ep[%d]: [%s], reject...\n", s, psw.c_str());
        close(s);
        return 1;
    }

    _ctrl  = s;
    _ep_ip = ip;
    printf("ep[%d] passed.\n", _ctrl);
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

    printf("waiting for ep...\n");

    while (true) {
        // first, wait for ep ctrl connection.
        if (_accept_ep()) continue;

        // got ctrl, start srving cli
        _srving = true;
        ctrl_msg(msg_epc_port); // tell ep [epc port], add ack later
        std::thread(rcv_epc).detach();
        std::thread(srv_cli).detach();

        // ep health check
        while (_srving) {
            sleep(HEALTH_CHECK_FREQUENCE);
            ctrl_msg("h"); // empty msg, just check connection.
        }

        // ep disconnected, clear queue
        {
            _lock_t l(_qlock);
            while (_queue.size()) {
                auto [s, ts] = _queue.front();
                printf("clear old epc[%d]\n", s);
                close(s);
                _queue.pop();
            }
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

    show_conf();
    srv();

quit:
    clear();
    return 0;
}