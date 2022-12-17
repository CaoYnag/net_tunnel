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
#include "signal.h"
#include "utils.h"
namespace bpo = boost::program_options;
using std::string;
using _lock_t = std::lock_guard<std::mutex>;

int  _cport; // port for client
int  _eport; // port for endpoint
SOCK _ctrl;  // ctrl socket
SOCK _cs;    // sock for client
SOCK _es;    // sock for endpoint

bool reg_opt(int argc, char** argv, bpo::variables_map& vm) {
    bpo::options_description            opts("vector convertor");
    bpo::positional_options_description file_desc;
    file_desc.add("source", -1);
    opts.add_options()                                                                                //
        ("help,h", "show help info")                                                                  //
        ("cport,c", bpo::value<int>(&_cport)->default_value(9999)->value_name("port"), "client port") //
        ("eport,e", bpo::value<int>(&_eport)->default_value(9000)->value_name("port"), "endpoint port");

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
    send(_ctrl, msg.c_str(), msg.length(), 0);
    return 0;
}

SOCK next_ep_connection() {
    ctrl_msg("c");
    SOCK s = accept(_es, nullptr, 0);
    if (s < 0) {
        perror("failed accept ep conn\n");
    }
    return s;
}

void clear() {
    if (_ctrl) close(_ctrl);
    if (_cs) close(_cs);
    if (_es) close(_es);
}

int build_ep() {
    listen(_es, 100);
    printf("waiting endpoints at [:%d]\n", _eport);
    _ctrl = accept(_es, nullptr, 0); // first accept connection as ctrl connection
    if (_ctrl < 0) {
        perror("failed accept ctrl conn\n");
        return 1;
    }
    printf("ep connected.\n");
    return 0;
}

void connect_cli_ep(SOCK c, SOCK e) {
    std::thread(std::bind(packet_forward, c, e)).detach(); // c => e
    packet_forward(e, c);                                  // e => c
    close(c);
    close(e);
    printf("connection close\n");
}

void dispatch_new_cli(SOCK c) {
    printf("accept new client, ");
    SOCK ep = next_ep_connection();
    if (ep < 0) {
        printf("but no valid ep conntion\n");
        close(c); // no spare connection, refuse client connection
    } else {
        printf("success rcv ep conntion\n");
        connect_cli_ep(c, ep);
    }
}

int start_srv() {
    listen(_cs, 100);
    printf("waiting clients [:%d]\n", _cport);
    while (true) {
        SOCK c = accept(_cs, nullptr, 0);
        if (c < 0) continue;
        std::thread(std::bind(dispatch_new_cli, c)).detach();
    }
    printf("svr down...\n");
    return 0;
}

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    bpo::variables_map vm;
    if (reg_opt(argc, argv, vm)) return 0;
    if (setup_socket(_es, _eport)) return 1;
    if (setup_socket(_cs, _cport)) return 1;

    if (build_ep()) goto quit;
    start_srv();

quit:
    clear();
    return 0;
}