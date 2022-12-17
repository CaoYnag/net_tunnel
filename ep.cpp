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

string      _hub;   // hub addr
int         _hport; // hub port
string      _fwd;   // forward addr
int         _lport; // forward port
SOCK        _ctrl;  // ctrl connection
int         _cnt;   // connection count
int         _base;  // base connection count
int         _step;  // connection increase step
int         _max;   // max connections
sockaddr_in _hub_addr;
sockaddr_in _fwd_addr;


bool reg_opt(int argc, char** argv, bpo::variables_map& vm) {
    bpo::options_description            opts("vector convertor");
    bpo::positional_options_description file_desc;
    file_desc.add("source", -1);
    opts.add_options()                                                                                                    //
        ("help,h", "show help info")                                                                                      //
        ("hub,u", bpo::value<string>(&_hub)->value_name("host"), "hub host")                                              //
        ("port,p", bpo::value<int>(&_hport)->value_name("port"), "hub port")                                              //
        ("forward,f", bpo::value<string>(&_fwd)->value_name("host")->default_value("127.0.0.1"), "forward host")          //
        ("lport,l", bpo::value<int>(&_lport)->value_name("port"), "forward port")                                         //
        ("base,n", bpo::value<int>(&_base)->default_value(50)->value_name("num")->required(), "init connection num")      //
        ("step,s", bpo::value<int>(&_step)->default_value(10)->value_name("num")->required(), "connection increase step") //
        ("max,m", bpo::value<int>(&_max)->default_value(100)->value_name("num")->required(), "max connections");

    try {
        store(bpo::command_line_parser(argc, argv).options(opts).positional(file_desc).run(), vm);
    } catch (bpo::error_with_no_option_name& ex) {
        printf("%s\n", ex.what());
        goto err;
    }
    notify(vm);

    if (vm.count("help")) goto err;
    if (vm.count("hub") * vm.count("port") * vm.count("lport") == 0) {
        perror("check parameters.\n");
        goto err;
    }
    return false;
err:
    std::cout << opts << std::endl;
ret:
    return true;
}

SOCK setup_socket(bool hub) {
    SOCK s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        fprintf(stderr, "failed create socket\n");
        return -1;
    }
    sockaddr* addr;
    int       len;
    if (hub) {
        addr = (sockaddr*)&_hub_addr;
        len  = sizeof(_hub_addr);
    } else {
        addr = (sockaddr*)&_fwd_addr;
        len  = sizeof(_fwd_addr);
    }
    if (connect(s, addr, len) < 0) {
        fprintf(stderr, "failed connect to %s\n", (hub ? "hub" : "fwd"));
        return -1;
    }

    return s;
}

int ctrl_msg(const string& msg) {
    send(_ctrl, msg.c_str(), msg.length(), 0);
    return 0;
}

void add_data_connection();
void connect_hub_fwd(SOCK h, SOCK f) {
    std::thread(std::bind(packet_forward, f, h)).detach();
    packet_forward(h, f);
    close(h);
    close(f);
    --_cnt;
    add_data_connection(); // add a new connection
}

void add_data_connection() {
    SOCK h = setup_socket(true);
    if (h < 0) return;
    SOCK f = setup_socket(false);
    if (f < 0) {
        close(h);
        return;
    }
    std::thread(std::bind(connect_hub_fwd, h, f)).detach();
    ++_cnt;
}
void add_data_connections(int num) {
    while (num--) {
        add_data_connection();
    }
}

int setup_ctrl() {
    _ctrl = setup_socket(true);
    if (_ctrl < 0) return 1;
    char buff[2048];
    int  n = recv(_ctrl, buff, 2048, 0);
    if (n < 0) {
        perror("error recv from ctrl\n");
        return 1;
    }
    add_data_connections(_base);
    return 0;
}
void init() {
    memset(&_fwd_addr, 0, sizeof(_fwd_addr));
    _fwd_addr.sin_family      = AF_INET;
    _fwd_addr.sin_port        = htons(_lport);
    _fwd_addr.sin_addr.s_addr = inet_addr(_fwd.c_str());
    printf("forward %s:%d", _fwd.c_str(), _lport);

    memset(&_hub_addr, 0, sizeof(_hub_addr));
    _hub_addr.sin_family      = AF_INET;
    _hub_addr.sin_port        = htons(_hport);
    _hub_addr.sin_addr.s_addr = inet_addr(_hub.c_str());
    printf("hub     %s:%d", _hub.c_str(), _hport);
}
int handle_ctrl() {
    while (true) {
        char buff[2048];
        int  n = recv(_ctrl, buff, 2048, 0);
        if (n < 0) {
            perror("error recv from ctrl\n");
            return 1;
        }
        if (_cnt < _max)
            add_data_connections(_step);
    }
}

int main(int argc, char** argv) {
    bpo::variables_map vm;
    if (reg_opt(argc, argv, vm)) return 0;
    init();
    if (setup_ctrl()) return 0;
    handle_ctrl();
    return 0;
}