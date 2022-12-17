#include <cstdio>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <vector>
#include <string>
#include <tuple>
#include <boost/program_options.hpp>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <thread>
#include <functional>
namespace bpo = boost::program_options;
using std::string;

typedef int                              SOCK;
typedef std::tuple<char, int, int, SOCK> _map_t; // type('t' 'u') local remote socket


int                 _port;   // listen port
std::vector<string> tcp_map; // tcp mapping local:remote
std::vector<string> udp_map; // udp mapping local:remote
std::vector<_map_t> mapping;
std::vector<SOCK>   mapping_socks;
SOCK                _local, _remote;
sockaddr_in         _remote_addr;

bool reg_opt(int argc, char** argv, bpo::variables_map& vm) {
    bpo::options_description            opts("vector convertor");
    bpo::positional_options_description file_desc;
    file_desc.add("source", -1);
    opts.add_options()                                                                                                //
        ("help,h", "show help info")                                                                                  //
        ("port,p", bpo::value<int>(&_port)->default_value(9999)->value_name("port"), "listen port")                   //
        ("tcp,t", bpo::value<std::vector<string>>(&tcp_map)->multitoken()->value_name("local:remote"), "tcp mapping") //
        ("udp,u", bpo::value<std::vector<string>>(&udp_map)->multitoken()->value_name("local:remote"), "udp mapping");

    try {
        store(bpo::command_line_parser(argc, argv).options(opts).positional(file_desc).run(), vm);
    } catch (bpo::error_with_no_option_name& ex) {
        printf("%s\n", ex.what());
        goto err;
    }
    notify(vm);


    if (vm.count("help")) goto err;
    if (vm.count("tcp") + vm.count("udp")) {
        int l, r;
        for (auto& s : tcp_map) {
            if (sscanf(s.c_str(), "%d:%d", &l, &r) < 2) {
                fprintf(stderr, "bad port mapping: \"%s\"\n", s.c_str());
                goto err;
            }
            mapping.emplace_back('t', l, r, 0);
        }
        for (auto& s : udp_map) {
            if (sscanf(s.c_str(), "%d:%d", &l, &r) < 2) {
                fprintf(stderr, "bad port mapping: \"%s\"\n", s.c_str());
                goto err;
            }
            mapping.emplace_back('u', l, r, 0);
        }
    } else {
        printf("no port mapping found!\n");
        goto err;
    }
    return false;
err:
    std::cout << opts << std::endl;
ret:
    return true;
}

void getipportfromaddr(sockaddr_in* addr, char* ipstr, int ipstrlen, int* port) {
    *port      = ntohs(addr->sin_port);
    in_addr in = addr->sin_addr;

    inet_ntop(AF_INET, &in, ipstr, ipstrlen);
}

int setup_socket(SOCK& sock, char type, int port) {
    if (type == 't') // tcp
        sock = socket(AF_INET, SOCK_STREAM, 0);
    else
        sock = socket(AF_INET, SOCK_DGRAM, 0);

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
        fprintf(stderr, "falied bind port %d\n", _port);
        return 1;
    }
    return 0;
}


int setup_svr() {
    if (setup_socket(_local, 't', _port)) return 1;
    listen(_local, 1);
    printf("svr listening at [:%d]\n", _port);

    for (auto& m : mapping) {
        SOCK s;
        if (setup_socket(s, std::get<0>(m), std::get<1>(m))) return 1;
        std::get<3>(m) = s;
    }


    return 0;
}

int wait_for_client() {
    // wait for client
    printf("waiting for client...\n");
    socklen_t len = sizeof(_remote_addr);
    SOCK      cli = accept(_local, (sockaddr*)&_remote_addr, &len);

    char buff[1024];
    int  port;
    getipportfromaddr((sockaddr_in*)&_remote_addr, buff, 1024, &port);
    printf("client[%s:%d] connected.\n", buff, port);
    // send a response to client here
    return 0;
}

void packet_forward(SOCK c, SOCK s) {
    char buff[2048];
    int  n = 0;
    while ((n = recv(c, buff, 2048, 0)) > 0) {
        send(s, buff, n, MSG_NOSIGNAL);
    }
    close(c);
    close(s);
}

void sock_forward(const _map_t& m) {
    while (true) {
        sockaddr_in from;
        socklen_t   len = sizeof(from);
        SOCK        c   = accept(std::get<3>(m), (sockaddr*)&from, &len);
        if (c < 0) continue;
        SOCK s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            fprintf(stderr, "cannot create socket");
            return;
        }
        // mapping_socks.emplace_back(s);
        sockaddr_in dst;
        memcpy(&dst, &_remote_addr, sizeof(dst));
        dst.sin_port = htons(std::get<2>(m));
        if (connect(s, (sockaddr*)&dst, sizeof(dst)) < 0) {
            fprintf(stderr, "cannot connect to client in group [%d:%d]\n", std::get<1>(m), std::get<2>(m));
            return;
        }
        {
            char buff[1024];
            int  port;
            getipportfromaddr((sockaddr_in*)&from, buff, 1024, &port);
            printf("forward connection[%s:%d] to remote[:%d]\n", buff, port, std::get<2>(m));
        }
        std::thread(std::bind(packet_forward, c, s)).detach();
        std::thread(std::bind(packet_forward, s, c)).detach();
    }
}


int start_mapping() {
    for (auto& m : mapping) {
        listen(std::get<3>(m), 100);
        printf("[%c:%d:%d] start\n", std::get<0>(m), std::get<1>(m), std::get<2>(m));
        std::thread(std::bind(sock_forward, m)).detach();
    }
    return 0;
}

void clear() {
    for (auto s : mapping_socks) {
        close(s);
    }
    for (auto m : mapping)
        if (std::get<3>(m)) close(std::get<3>(m));
    if (_local) close(_local);
}

int main(int argc, char** argv) {
    bpo::variables_map vm;
    if (reg_opt(argc, argv, vm)) return 0;
    if (setup_svr()) goto quit;
    if (wait_for_client()) goto quit;
    if (start_mapping()) goto quit;

    while (true)
        ;
quit:
    clear();
    return 0;
}