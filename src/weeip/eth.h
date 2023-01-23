
#ifndef __ETHH__
#define __ETHH__
#include "defs.h"
#include "inet.h"

#define ETH_RX_BUFFER 0xFFDE800L
#define ETH_TX_BUFFER 0xFFDE800L

extern IPV4 ip_mask;
extern IPV4 ip_gate;
extern IPV4 ip_dnsserver;
extern EUI48 mac_local;
void eth_read(buffer_t dest, uint16_t tam);
void eth_write(localbuffer_t orig, uint16_t tam);
void eth_set(byte_t v, uint16_t tam);
bool_t eth_clear_to_send(void);
void eth_drop(void);
byte_t eth_task(byte_t sig);
bool_t eth_ip_send(void);
void eth_arp_send(EUI48 *mac);
void eth_packet_send(void);
void eth_init(void);
void eth_disable(void);
void eth_enable(void);
#endif
