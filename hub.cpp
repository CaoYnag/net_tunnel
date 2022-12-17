#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <tuple>
#include <boost/program_options.hpp>
#include <thread>
#include <functional>
#include "utils.h"
namespace bpo = boost::program_options;
using std::string;
using _lock_t = std::lock_guard<std::mutex>;

std::vector<SOCK> _pool;
int               _count = 0; // total connections
int               _using = 0; // using connectoins
std::mutex        _plock;     // lock for pool
int               _cport;     // port for client
int               _eport;     // port for endpoint
SOCK              _cs;        // sock for client
SOCK              _es;        // sock for endpoint

bool reg_opt(int argc, char** argv, bpo::variables_map& vm) {
    bpo::options_description            opts("vector convertor");
    bpo::positional_options_description file_desc;
    file_desc.add("source", -1);
    opts.add_options()                                                                                            //
        ("help,h", "show help info")                                                                              //
        ("cport,c", bpo::value<int>(&_cport)->default_value(9999)->value_name("port")->required(), "client port") //
        ("eport,e", bpo::value<int>(&_eport)->default_value(9000)->value_name("port")->required(), "endpoint port");

    try {
        store(bpo::command_line_parser(argc, argv).options(opts).positional(file_desc).run(), vm);
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
    send(_pool[0], msg.c_str(), msg.length(), 0);
    return 0;
}

void add_spare_connection(SOCK s, bool _new) {
    _lock_t g(_plock);
    if (_new)
        ++_count;
    else
        --_using;
    _pool.emplace_back(s);
}

SOCK get_spare_connection() {
    SOCK s = -1;
    if (_pool.size() > 1) {
        _lock_t g(_plock);
        if (_pool.size() > 1) {
            s = _pool.back();
            _pool.pop_back();
            ++_using;
        }
    }
    if (_pool.size() < 2) ctrl_msg("ac"); // request more connection from ep
    return s;
}


void clear() {
    for (auto s : _pool)
        if (s) close(s);
    if (_cs) close(_cs);
    if (_es) close(_es);
}

void rcv_ep() {
    while (true) {
        SOCK s = accept(_es, nullptr, 0); // first accept connection as ctrl connection
        if (s) add_spare_connection(s, true);
    }
}

int build_ep() {
    listen(_es, 100);
    printf("waiting endpoints at [:%d]\n", _eport);
    SOCK s = accept(_es, nullptr, 0); // first accept connection as ctrl connection
    if (s < 0) {
        perror("failed accept ctrl conn\n");
        return 1;
    }
    _pool.emplace_back(s);

    ctrl_msg("rdy"); // send rdy msg, prepare to rcv connections
    std::thread(rcv_ep).detach();
    return 0;
}

void connect_cli_ep(SOCK c, SOCK e) {
    std::thread(std::bind(packet_forward, c, e)).detach(); // c => e
    packet_forward(e, c);                                  // e => c
    close(c);
    close(e);
}


int start_srv() {
    listen(_cs, 100);
    printf("waiting clients [:%d]\n", _cport);
    while (true) {
        SOCK c  = accept(_cs, nullptr, 0);
        SOCK ep = get_spare_connection();
        if (ep < 0)
            close(c); // no spare connection, refuse client connection
        else
            std::thread(connect_cli_ep, c, ep).detach();
    }
    return 0;
}

// report connection status every 5s
void timer() {
    while (true) {
        printf("connections: %d/%d\n", _using, _count);
        sleep(5);
    }
}

int main(int argc, char** argv) {
    bpo::variables_map vm;
    if (reg_opt(argc, argv, vm)) return 0;
    if (setup_socket(_es, _eport)) return 1;
    if (setup_socket(_cs, _cport)) return 1;

    if (build_ep()) goto quit;
    std::thread(timer).detach();
    start_srv();

quit:
    clear();
    return 0;
}