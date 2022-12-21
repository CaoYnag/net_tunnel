#include "utils.h"
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <functional>
#include <unistd.h>
#include <ctime>
#include <atomic>

void getipportfromaddr(sockaddr_in* addr, char* ipstr, int ipstrlen, int* port) {
    *port      = ntohs(addr->sin_port);
    in_addr in = addr->sin_addr;

    inet_ntop(AF_INET, &in, ipstr, ipstrlen);
}

void packet_forward(SOCK src, SOCK dst) {
    char buff[2048];
    int  n = 0;
    while ((n = recv(src, buff, 2048, 0)) > 0) {
        if (send(dst, buff, n, 0) < 0) return;
    }
}
void packet_forward_with_sig(SOCK src, SOCK dst, std::atomic<int>& sig) {
    char buff[2048];
    int  n = 0;
    while ((n = recv(src, buff, 2048, 0)) > 0) {
        if (send(dst, buff, n, 0) < 0)
            break;
    }
    --sig;
}
long cur_ms() {
    timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
std::tuple<SOCK, std::string, int> _accept_conn(SOCK s) {
    sockaddr_in addr;
    socklen_t   len = sizeof(addr);
    SOCK        c   = accept(s, (sockaddr*)&addr, &len);
    if (c < 0) return { c, "", 0 };
    char buff[256];
    int  port;
    getipportfromaddr(&addr, buff, 256, &port);
    return { c, buff, port };
}
void create_tunnel(SOCK s1, SOCK s2) {
    printf("tunnel[%d:%d] create.\n", s1, s2);
    std::atomic<int> sig(2);
    std::thread(std::bind(packet_forward_with_sig, s1, s2, std::ref(sig))).detach();
    std::thread(std::bind(packet_forward_with_sig, s2, s1, std::ref(sig))).detach();
    while (sig == 2) usleep(1000); // 1ms
    close(s1);
    close(s2);
    while (sig > 0) usleep(1000); // wait for another thread
    printf("tunnel[%d:%d] close.\n", s1, s2);
}
void create_tunnel_old(SOCK s1, SOCK s2) {
    printf("tunnel[%d:%d] create.\n", s1, s2);
    std::thread(std::bind(packet_forward, s1, s2)).detach();
    std::thread(std::bind(packet_forward, s2, s1)).detach();
    int n = 0;
    while (true) {
        // test connection
        if (send(s1, "", 0, MSG_NOSIGNAL) < 0) break;
        if (send(s2, "", 0, MSG_NOSIGNAL) < 0) break;
        usleep(100 * 1000); // 100ms
    }
    close(s1);
    close(s2);
    printf("tunnel[%d:%d] close.\n", s1, s2);
}
std::string cur_time() {
    time_t t;
    time(&t);
    auto tm = localtime(&t);
    char buff[256];
    strftime(buff, 256, "%Y-%m-%d %H:%M:%S", tm);
    return buff;
}

std::string _rcv_msg(SOCK s, long timeout) {
    char buff[2048];
    int  n     = 0;
    long start = cur_ms();
    while (true) {
        int n = recv(s, buff, 2048, MSG_DONTWAIT);
        if (n > 0) break; // got valid msg
        if ((cur_ms() - start) >= timeout) {
            printf("ep psw timeout.\n");
            return "";
        }             // timeout
        usleep(1000); // wait 1ms
    }
    printf("got psw from ep[%d]: [%s]\n", s, buff);
    return buff;
}