#pragma once

#include <stdlib.h>
#include <stdint.h>
#ifdef WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

typedef int (*get_packet_seq_callback_t)(uint8_t *payload, int len);
typedef int (*match_payloads_callback_t)(uint8_t *rx_payload, int rx_len, uint8_t *tx_payload, int tx_len);
typedef int (*is_duplicate_callback_t)(uint8_t *payload, int len, uint8_t *cmp_payload, int cmp_len);
typedef int (*embed_packet_seq_callback_t)(uint8_t *payload, int len, int seq_num);
typedef int (*timeout_handler_callback_t)();

int etherload_init(const char *broadcast_address);
void etherload_finish(void);

void ethl_setup_dmaload(void);
void ethl_setup_callbacks(get_packet_seq_callback_t c1, match_payloads_callback_t c2, is_duplicate_callback_t c3,
    embed_packet_seq_callback_t c4, timeout_handler_callback_t c5);

int trigger_eth_hyperrupt(void);
char *ethl_get_ip_address(void);
uint16_t ethl_get_port(void);
int ethl_get_socket(void);
struct sockaddr_in *ethl_get_server_addr(void);
int ethl_send_packet(uint8_t *payload, int len);
int ethl_send_packet_unscheduled(uint8_t *payload, int len);
int ethl_schedule_ack(uint8_t *payload, int len);
int ethl_set_queue_length(uint16_t length);
int ethl_get_current_seq_num();

void set_send_mem_rom_write_enable();
int send_mem(unsigned int address, unsigned char *buffer, int bytes);
int wait_ack_slots_available(int num_free_slots_needed);
int wait_all_acks(void);
int dmaload_no_pending_ack(int addr);
int send_ethlet(const uint8_t data[], const int bytes);
