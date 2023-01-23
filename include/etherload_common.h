#pragma once

#include <stdlib.h>
#ifdef WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

int etherload_init(const char* broadcast_address);
void etherload_finish(void);

void etherload_setup_dmaload(void);

int trigger_eth_hyperrupt(void);
char* ethl_get_ip_address(void);
uint16_t ethl_get_port(void);
int ethl_get_socket(void);
struct sockaddr_in* ethl_get_server_addr(void);

int send_mem(unsigned int address, unsigned char *buffer, int bytes);
int wait_all_acks(void);
int no_pending_ack(int addr);
int send_ethlet(const char data[], const int bytes);
