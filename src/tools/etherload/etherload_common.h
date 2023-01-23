#pragma once

#include <stdlib.h>

int etherload_init(const char* broadcast_address);
void etherload_finish(void);

int trigger_eth_hyperrupt(void);
char* ethl_get_ip_address(void);
uint16_t ethl_get_port(void);

int send_mem(unsigned int address, unsigned char *buffer, int bytes);
int wait_all_acks(void);
int no_pending_ack(int addr);
int send_ethlet(const char data[], const int bytes);
