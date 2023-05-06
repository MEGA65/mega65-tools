#include <etherload_common.h>
#include "etherload/ethlet_all_done_basic2_map.h"
#include "logging.h"

#include <unistd.h>
#include <string.h>

extern unsigned int helperroutine_eth_len;
extern unsigned char helperroutine_eth[];
extern unsigned char ethlet_all_done_basic2[];
extern unsigned int ethlet_all_done_basic2_len;

int get_packet_seq_callback(uint8_t *payload, int len)
{
  return payload[4] + (payload[5] << 8);
}

int match_payloads_callback(uint8_t *rx_payload, int rx_len, uint8_t *tx_payload, int tx_len)
{
  return memcmp(&rx_payload[4], &tx_payload[4], 6) == 0 ? 1 : 0;
}

int is_duplicate_callback(uint8_t *payload, int len, uint8_t *cmp_payload, int cmp_len)
{
  return 0;
}

int embed_packet_seq_callback(uint8_t *payload, int len, int seq_num)
{
  payload[4] = (seq_num >> 0) & 0xff;
  payload[5] = (seq_num >> 8) & 0xff;
  return 1;
}

void startup()
{
  unsigned char *helper_ptr = helperroutine_eth + 2;
  int bytes = helperroutine_eth_len - 2;
  int address = 0x0801;
  int block_size = 1024;
  if (etherload_init("192.168.141.31"/*"192.168.1.255"*/)) {
    log_error("Unable to initialize ethernet communication");
    exit(-1);
  }
  ethl_setup_dmaload();
  trigger_eth_hyperrupt();
  usleep(100000);
  log_info("Starting helper routine transfer...");
  while (bytes > 0) {
    if (bytes < block_size)
      block_size = bytes;
    send_mem(address, helper_ptr, block_size);
    helper_ptr += block_size;
    address += block_size;
    bytes -= block_size;
  }
  wait_all_acks();
  log_info("Helper routine transfer complete");
  // patch in end address
  ethlet_all_done_basic2[ethlet_all_done_basic2_offset_data_end_address] = 0x01;
  ethlet_all_done_basic2[ethlet_all_done_basic2_offset_data_end_address + 1] = 0x08;
  // patch in do_run
  ethlet_all_done_basic2[ethlet_all_done_basic2_offset_do_run] = 1;
  // disable cartridge signature detection
  ethlet_all_done_basic2[ethlet_all_done_basic2_offset_enable_cart_signature] = 0;
  send_ethlet(ethlet_all_done_basic2, ethlet_all_done_basic2_len);

  // setup callbacks for job queue protocol
  ethl_setup_callbacks(
      &get_packet_seq_callback, &match_payloads_callback, &is_duplicate_callback, &embed_packet_seq_callback);
}

void packet_loop()
{
  static const int packet_size = 50; // 1458;
  uint8_t buffer[packet_size];
  static const uint32_t magic = 0x68438723;
  uint32_t counter = 0;
  uint8_t fill_byte = 0;

  memcpy((void *)buffer, (void *)&magic, 4);

  ethl_set_queue_length(20);

  while (1) {
    buffer[6] = (counter >> 0) & 0xff;
    buffer[7] = (counter >> 8) & 0xff;
    buffer[8] = (counter >> 16) & 0xff;
    buffer[9] = (counter >> 24) & 0xff;
    memset(&buffer[10], fill_byte, packet_size - 10);
    ethl_send_packet(buffer, packet_size);
    ++counter;
    ++fill_byte;
  }
}

int main()
{
  log_setup(stderr, LOG_WARN);

  startup();

  // Give helper program time to initialize
  usleep(700000);

  packet_loop();

  return 0;
}
