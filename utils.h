#pragma once
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <tuple>
#include <string>
#include <atomic>

typedef int SOCK;


long        cur_ms();
std::string cur_time();

void getipportfromaddr(sockaddr_in* addr, char* ipstr, int ipstrlen, int* port);
void packet_forward(SOCK src, SOCK dst);
void packet_forward_with_sig(SOCK src, SOCK dst, std::atomic<int>& sig);
void create_tunnel(SOCK s1, SOCK s2); // connect s1 and s2

std::tuple<SOCK, std::string, int> _accept_conn(SOCK sock); // accept connection, with ip:port
/* rcv msg in timeout(ms), notice this method use usleep to wait msg internal */
std::string _rcv_msg(SOCK s, long timeout);