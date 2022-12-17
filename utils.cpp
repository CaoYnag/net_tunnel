#include "utils.h"
#include <sys/time.h>


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
long cur_ms() {
    timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}