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
#include <signal.h>
#include <atomic>
#include "utils.h"
namespace bpo = boost::program_options;
using std::string;

constexpr const int HEALTH_CHECK_DELAY = 5;
enum SOCK_TYP {
    _st_hub = 0,
    _st_epc,
    _st_fwd
};

std::atomic<bool> _srving(false);
string            _hub;     // hub addr
int               _hport;   // hub port
int               _epcport; // epc port
string            _fwd;     // forward addr
int               _lport;   // forward port
SOCK              _ctrl;    // ctrl connection
sockaddr_in       _hub_addr;
sockaddr_in       _epc_addr;
sockaddr_in       _fwd_addr;

bool reg_opt(int argc, char** argv, bpo::variables_map& vm) {
    bpo::options_description opts("tunnel endpoint");
    opts.add_options()                                                                                           //
        ("help,h", "show help info")                                                                             //
        ("hub,u", bpo::value<string>(&_hub)->value_name("host"), "hub host")                                     //
        ("port,p", bpo::value<int>(&_hport)->value_name("port"), "hub port")                                     //
        ("forward,f", bpo::value<string>(&_fwd)->value_name("host")->default_value("127.0.0.1"), "forward host") //
        ("lport,l", bpo::value<int>(&_lport)->value_name("port"), "forward port");

    try {
        store(bpo::command_line_parser(argc, argv).options(opts).run(), vm);
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

SOCK setup_socket(int typ) {
    SOCK s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        fprintf(stderr, "failed create socket\n");
        return -1;
    }
    sockaddr* addr;
    int       len;
    if (typ == _st_hub) {
        addr = (sockaddr*)&_hub_addr;
        len  = sizeof(_hub_addr);
    } else if (typ == _st_fwd) {
        addr = (sockaddr*)&_fwd_addr;
        len  = sizeof(_fwd_addr);
    } else if (typ == _st_epc) {
        addr = (sockaddr*)&_epc_addr;
        len  = sizeof(_fwd_addr);
    }
    if (connect(s, addr, len) < 0) {
        fprintf(stderr, "failed connect to typ[%d]\n", typ);
        return -1;
    }

    return s;
}

int ctrl_msg(const string& msg) {
    if (send(_ctrl, msg.c_str(), msg.length(), 0) < 0) {
        _srving = false; // ep disconnected
        return 1;
    }
    return 0;
}

void add_data_connection() {
    SOCK h = setup_socket(_st_epc);
    if (h < 0) {
        printf("add tunnel failed: failed connect to epc\n");
        return;
    }
    SOCK f = setup_socket(_st_fwd);
    if (f < 0) {
        printf("add tunnel failed: failed connect to forward host for epc[%d]\n", h);
        close(h);
        return;
    }
    std::thread(std::bind(create_tunnel, h, f)).detach();
}

int setup_ctrl() {
    _ctrl = setup_socket(_st_hub);
    if (_ctrl < 0) return 1;
    return 0;
}

// init addr
void init() {
    memset(&_fwd_addr, 0, sizeof(_fwd_addr));
    _fwd_addr.sin_family      = AF_INET;
    _fwd_addr.sin_port        = htons(_lport);
    _fwd_addr.sin_addr.s_addr = inet_addr(_fwd.c_str());

    memset(&_hub_addr, 0, sizeof(_hub_addr));
    _hub_addr.sin_family      = AF_INET;
    _hub_addr.sin_port        = htons(_hport);
    _hub_addr.sin_addr.s_addr = inet_addr(_hub.c_str());
}

void handle_ctrl() {
    printf("start rcving hub request.\n");
    while (_srving) {
        char buff[2048];
        int  n = recv(_ctrl, buff, 2048, 0);
        if (n <= 0) {
            perror("error recv from ctrl\n");
            break;
        }
        switch (buff[0]) {
        case 'c': {
            // new epc request
            printf("got epc request, creating new connections...\n");
            add_data_connection();
        } break;
        case 'h': {
            // health check, ignore
        } break;
        }
    }
    printf("stop rcving hub request.\n");
}

// handle epc port msg
int handle_epc_port() {
    char buff[2048];
    int  n = recv(_ctrl, buff, 2048, 0);
    if (n <= 0) {
        return 1;
    }
    n = 0;
    n = sscanf(buff, "%d", &_epcport);
    if (n != 1) {
        printf("error epc port msg: [%s]\n", buff);
        return 1;
    }
    printf("Hub epc port: %d\n", _epcport);
    memset(&_epc_addr, 0, sizeof(_epc_addr));
    _epc_addr.sin_family      = AF_INET;
    _epc_addr.sin_port        = htons(_epcport);
    _epc_addr.sin_addr.s_addr = inet_addr(_hub.c_str());
    return 0;
}

void start() {
    printf("Endpoint start.\nHub    : %s:%d\nForward: %s:%d\n",
           _hub.c_str(), _hport, _fwd.c_str(), _lport);
    _srving = false;
    printf("connecting hub...\n");
    while (true) {
        // 1. connect hub
        while (setup_ctrl()) {
            sleep(5);
            printf("connect failed, try again after 5s...\n");
        }

        // 2. get epc port from hub
        if (handle_epc_port()) {
            close(_ctrl);
            printf("failed to get epc port from hub, try reconnect...\n");
            continue;
        }

        // 2. srving
        printf("connected to hub.\n");
        _srving = true;
        std::thread(handle_ctrl).detach();

        // 3. health check
        while (_srving) {
            sleep(HEALTH_CHECK_DELAY);
            ctrl_msg("h"); // empty msg, just check connection.
        }
        printf("hub lost, reconnecting...\n");
    }
}

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    bpo::variables_map vm;
    if (reg_opt(argc, argv, vm)) return 0;
    init();
    start();
    return 0;
}