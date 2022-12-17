#pragma once
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

typedef int SOCK;

void getipportfromaddr(sockaddr_in* addr, char* ipstr, int ipstrlen, int* port);
void packet_forward(SOCK src, SOCK dst);
long cur_ms();