#pragma once
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <tuple>
#include <string>

typedef int SOCK;

long                               cur_ms();
void                               getipportfromaddr(sockaddr_in* addr, char* ipstr, int ipstrlen, int* port);
void                               packet_forward(SOCK src, SOCK dst);
void                               packet_forward_with_sig(SOCK src, SOCK dst, int& sig);
std::tuple<SOCK, std::string, int> _accept_conn(SOCK sock);         // accept connection, with ip:port
void                               create_tunnel(SOCK s1, SOCK s2); // connect s1 and s2