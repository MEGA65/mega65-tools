/*
  Remote SD card access tool for mega65_ftp to more quickly
  send and receive files.

  It implements a simple protocol with pre-emptive sending
  of read data in raw mode at 4mbit = 40KB/sec.  Can do this
  while writing jobs to the SD card etc to hide latency.

*/

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <memory.h>

#define ETH_RX_BUFFER 0xFFDE800L
#define ETH_TX_BUFFER 0xFFDE800L
#define ARP_REQUEST 0x0100 // big-endian for 0x0001
#define ARP_REPLY 0x0200   // big-endian for 0x0002

uint16_t fastcall ip_checksum_recv();
uint16_t fastcall checksum_fast(uint16_t size);
void fastcall dma_copy_eth_io(void *src, void *dst, uint16_t size);

typedef struct {
  uint8_t b[6];
} EUI48;

typedef union {
  uint32_t d;
  uint8_t b[4];
} IPV4;

/**
 * Ethernet frame header.
 */
typedef struct {
  EUI48 destination; ///< Packet Destination address.
  EUI48 source;      ///< Packet Source address.
  uint16_t type;     ///< Packet Type or Size (big-endian)
} ETH_HEADER;

/**
 * ARP message format.
 */
typedef struct {
  uint16_t hardware;
  uint16_t protocol;
  uint8_t hw_size;
  uint8_t pr_size;
  uint16_t opcode;
  EUI48 orig_hw;
  IPV4 orig_ip;
  EUI48 dest_hw;
  IPV4 dest_ip;
} ARP_HDR;

/**
 * IP Header format.
 */
typedef struct {
  uint8_t ver_length;    ///< Protocol version (4) and header size (32 bits units).
  uint8_t tos;           ///< Type of Service.
  uint16_t ip_length;    ///< Total packet length.
  uint16_t id;           ///< Message identifier.
  uint16_t frag;         ///< Fragmentation index (not used).
  uint8_t ttl;           ///< Time-to-live.
  uint8_t protocol;      ///< Transport protocol identifier.
  uint16_t checksum_ip;  ///< Header checksum.
  IPV4 source;           ///< Source host address.
  IPV4 destination;      ///< Destination host address.
  uint16_t src_port;     ///< Source application address.
  uint16_t dst_port;     ///< Destination application address.
  uint16_t udp_length;   ///< UDP packet size.
  uint16_t checksum_udp; ///< Packet checksum, with pseudo-header.
  uint32_t ftp_magic;
  uint16_t seq_num;
  uint8_t opcode;
} FTP_PKT;

typedef struct {
  uint8_t num_sectors_minus_one;
  uint8_t unused_1;
  uint32_t sector_number;
} READ_SECTOR_JOB;

typedef struct {
  uint8_t batch_counter;
  uint8_t num_sectors_minus_one;
  uint8_t slot_index;
  uint32_t start_sector_number;
} WRITE_SECTOR_JOB;

typedef struct {
  uint8_t num_bytes_minus_one;
  uint32_t address;
} READ_MEMORY_JOB;

typedef union {
  uint8_t b[1500];
  struct {
    ETH_HEADER eth;
    union {
      ARP_HDR arp;
      struct {
        FTP_PKT ftp;
        union {
          READ_SECTOR_JOB read_sector;
          WRITE_SECTOR_JOB write_sector;
          READ_MEMORY_JOB read_memory;
        };
      };
    };
  };
} PACKET;

#define NTOHS(x) (((uint16_t)x >> 8) | ((uint16_t)x << 8))
#define HTONS(x) (((uint16_t)x >> 8) | ((uint16_t)x << 8))

uint8_t cpu_status;
char msg[80];
uint8_t quit_requested = 0;
uint8_t eth_controller_reset_done = 0;
PACKET reply_template;
PACKET recv_buf;
uint8_t sector_buf[512];
PACKET send_buf;
uint16_t send_buf_size;
uint8_t sector_reading, sector_buffered;
uint32_t sector_number_read, sector_number_buf, sector_number_write;
uint8_t read_batch_active = 0;
uint8_t write_batch_active = 0;
uint8_t slot_ids_received[256];
uint8_t slots_written;
uint8_t write_batch_max_id = 0;
uint8_t batch_left = 0;
uint8_t current_batch_counter = 0xff;
uint16_t seq_num;
uint32_t write_cache_offset;

EUI48 mac_local;
uint8_t ip_addr_set = 0;

uint16_t ip_id = 0;

uint32_t chks_err_cnt = 0;
uint32_t udp_chks_err_cnt = 0;
uint32_t retrans_cnt = 0;
uint32_t dup_cnt = 0;
uint32_t tx_reset_cnt = 0;
uint32_t rx_invalid_cnt = 0;

typedef union {
  uint16_t u;
  uint8_t b[2];
} chks_t;
chks_t chks;

static uint8_t _a, _b, _c;
static unsigned int _b16;

void stop_fatal(char *text);

void print(uint8_t row, uint8_t col, char *text)
{
  uint16_t addr = 0x400u + 40u * row + col;

  lfill(addr, 0x20, 40 - col);
  while (*text) {
    *((char *)addr++) = *text++;
    
    if (addr > 0x7bf) {
      stop_fatal("print out of bounds error");
    }
  }
}

void init(void)
{
  POKE(0xD020, 0);
  POKE(0xD021, 0);
  lfill(0x400, 0x20, 1000);
  lfill(0xff80000, 5, 1000);

  print(10, 0, "ip chks err count:  0");
  print(11, 0, "udp chks err count: 0");
  print(12, 0, "retrans detected:   0");
  print(13, 0, "duplicate packets:  0");
  print(14, 0, "tx resets:          0");
  print(15, 0, "rx invalid packets: 0");
}

void stop_fatal(char *text)
{
  print(2, 0, text);
  while (1) {
    POKE(0xD020, 1);
    POKE(0xD020, 2);
  }
}

void dump_bytes(uint8_t *data, uint8_t n, uint8_t screen_line)
{
  char *msg_ptr = msg;
  while (n-- > 0) {
    uint8_t b = *data++;
    *msg_ptr = b >> 4;
    if (*msg_ptr < 10) {
      *msg_ptr |= 0x30;
    }
    else {
      *msg_ptr += 0x37;
    }
    ++msg_ptr;
    *msg_ptr = b & 0xf;
    if (*msg_ptr < 10) {
      *msg_ptr |= 0x30;
    }
    else {
      *msg_ptr += 0x37;
    }

    ++msg_ptr;
    *msg_ptr = 0x20;
    ++msg_ptr;
  }
  *msg_ptr = 0;

  print(screen_line, 0, msg);
}

void print_mac_address(void)
{
  // Read MAC address
  lcopy(0xFFD36E9, (unsigned long)&mac_local.b[0], 6);
  sprintf(msg, "mac: %02x:%02x:%02x:%02x:%02x:%02x", mac_local.b[0], mac_local.b[1], mac_local.b[2], mac_local.b[3],
      mac_local.b[4], mac_local.b[5]);
  print(3, 0, "local");
  print(4, 0, msg);
}

void print_ip_informtaion(void)
{
  sprintf(msg, "ip : %d.%d.%d.%d", reply_template.ftp.source.b[0], reply_template.ftp.source.b[1],
      reply_template.ftp.source.b[2], reply_template.ftp.source.b[3]);
  print(5, 0, msg);
  print(7, 0, "remote");
  sprintf(msg, "ip : %d.%d.%d.%d", reply_template.ftp.destination.b[0], reply_template.ftp.destination.b[1],
      reply_template.ftp.destination.b[2], reply_template.ftp.destination.b[3]);
  print(8, 0, msg);
}

void update_rx_invalid_counter()
{
  ++rx_invalid_cnt;
  sprintf(msg, "%lu", rx_invalid_cnt);
  print(15, 20, msg);
}

void update_counters(void)
{
  static const uint8_t col = 20;

  sprintf(msg, "%lu", chks_err_cnt);
  print(10, col, msg);
  sprintf(msg, "%lu", udp_chks_err_cnt);
  print(11, col, msg);
  sprintf(msg, "%lu", retrans_cnt);
  print(12, col, msg);
  sprintf(msg, "%lu", dup_cnt);
  print(13, col, msg);
  sprintf(msg, "%lu", tx_reset_cnt);
  print(14, col, msg);
}

/**
 * Calculate checksum for a memory area (must be word-aligned).
 * Pad a zero byte, if the size is odd.
 * Optimized for 8-bit word processors.
 * The result is found in chks.
 * @param buf Pointer to a memory buffer.
 * @param size Data size in bytes.
 */
void checksum(uint8_t *buf, uint16_t size)
{
  _c = 0;
  while (size) {
    /*
     * First byte (do not care if LSB or not).
     */
    _a = chks.b[0];
    _b = _b16 = _a + (*buf++) + _c;
    _c = _b16 >> 8;
    chks.b[0] = _b;
    if (--size == 0) {
      /*
       * Pad a zero. Just test the carry.
       */
      if (_c) {
        if (++chks.b[1] == 0)
          chks.b[0]++;
      }
      return;
    }
    /*
     * Second byte (do not care if MSB or not).
     */
    _a = chks.b[1];
    _b = _b16 = _a + (*buf++) + _c;
    _c = _b16 >> 8;
    chks.b[1] = _b;
    size--;
  }
  /*
   * Test the carry.
   */
  if (_c) {
    ++chks.b[0];
    if (!chks.b[0])
      chks.b[1]++;
  }
}

/**
 * Sums a 16-bit word to the current checksum value.
 * Optimized for 8-bit word processors.
 * The result is found in chks.
 * @param v Value to sum.
 */
void add_checksum(uint16_t v)
{
  /*
   * First byte (MSB).
   */
  _a = chks.b[0];
  _b = _b16 = _a + (v >> 8);
  _c = _b16 >> 8;
  chks.b[0] = _b;

  /*
   * Second byte (LSB).
   */
  _a = chks.b[1];
  _b = _b16 = _a + (v & 0xff) + _c;
  _c = _b16 >> 8;
  chks.b[1] = _b;

  /*
   * Test for carry.
   */
  if (_c) {
    if (++chks.b[0] == 0)
      chks.b[1]++;
  }
}

uint8_t check_ip_checksum(uint8_t *hdr)
{
  static chks_t ref_chks;

  chks.u = ip_checksum_recv();
  //chks.u = 0;
  //checksum(hdr, 20);
  if (chks.u == 0xffffU) {
    return 1;
  }

  ref_chks.b[0] = hdr[10];
  ref_chks.b[1] = hdr[11];
  sprintf(msg, "ip chks    rx: %04x  act: %04x", ref_chks.u, chks.u);
  print(20, 0, msg);
  return 0;
}

uint8_t check_udp_checksum()
{
  static uint16_t udp_length;
  static uint16_t ref_chks;

  udp_length = NTOHS(recv_buf.ftp.udp_length);

  if (udp_length > 1400)
  {
    stop_fatal("udp length too large");
  }

  if (udp_length < 8)
  {
    stop_fatal("udp length too small");
  }
  *(uint16_t *)0xC80A = recv_buf.ftp.udp_length;
  //lcopy(ETH_RX_BUFFER + 2 + sizeof(ETH_HEADER) + 20, 0xC80CU, udp_length);
  dma_copy_eth_io((void *)(0xD800U + 2 + sizeof(ETH_HEADER) + 20), (void *)(0xC80CU), udp_length);
  
  ref_chks = *(uint16_t *)0xC812;
  *(uint16_t *)0xC812 = 0; // reset checksum field

  chks.u = checksum_fast(udp_length + 12);
  //chks.u = 0;
  //checksum((uint8_t *)0xC800U, udp_length + 12);

  if (chks.u == 0) {
    chks.u = 0xffffU;
  }
  if (chks.u == ref_chks) {
    return 1;
  }
  sprintf(msg, "udp chks   rx: %04x act: %04x len: %04x", ref_chks, chks.u, udp_length + 12);
  print(23, 0, msg);
  
  ++udp_chks_err_cnt;
  update_counters();
  update_rx_invalid_counter();

  return 0;
}

void wait_for_sd_ready()
{
  while (PEEK(0xD680) & 0x03)
    //POKE(0xD020, PEEK(0xD020) + 1);
    continue;
}

void init_new_write_batch()
{
  write_batch_active = 1;
  slots_written = 0;
  lfill((uint32_t)slot_ids_received, 0x00, 0x100);
  write_cache_offset = 0x40000ul;

  batch_left = recv_buf.write_sector.num_sectors_minus_one;
  write_batch_max_id = batch_left;
  ++batch_left;
  if (batch_left < 2 || batch_left > 128) {
    stop_fatal("error: batch size out of bounds");
  }
  sector_number_write = recv_buf.write_sector.start_sector_number;
  current_batch_counter = recv_buf.write_sector.batch_counter;
  // printf("batch #%d size %d sector %ld\n", current_batch_counter, batch_left, sector_number_write);

  reply_template.ftp.ip_length = HTONS(20 + 8 + 14);
  reply_template.ftp.udp_length = HTONS(8 + 14);
  reply_template.ftp.opcode = 2;
  reply_template.write_sector.num_sectors_minus_one = write_batch_max_id;
  reply_template.write_sector.start_sector_number = sector_number_write;
}

void multi_sector_write_next()
{
  int cmd = 5; // Multi-sector mid

  if (slots_written > write_batch_max_id) {
    stop_fatal("error: slots written out of bounds");
  }

  if (!slot_ids_received[slots_written]) {
    // print(2, 0, "retransmission detected");
    ++retrans_cnt;
    update_counters();
    return;
  }

  wait_for_sd_ready();
  lcopy(write_cache_offset, 0xffd6e00, 512);
  if (slots_written == 0) {
    cmd = 4; // Multi-sector start
    *(uint32_t *)0xD681 = sector_number_write;
  }
  else {
    if (slots_written == write_batch_max_id) {
      cmd = 6; // Multi-sector end
    }
  }
  POKE(0xD680, 0x57); // Open write gate
  POKE(0xD680, cmd);
  write_cache_offset += 512;
  ++slots_written;
}

void handle_batch_write()
{
  uint8_t id = recv_buf.write_sector.slot_index;
  uint32_t cache_position = 0x40000ul;
  cache_position += (uint32_t)id << 9;
  if (cache_position < 0x40000ul || cache_position > 0x5fe00ul) {
    stop_fatal("cache_position out of bounds");
  }

  if (recv_buf.write_sector.batch_counter != current_batch_counter) {
    print(2, 0, "late duplicate packet for batch");
    return;
  }

  if (slot_ids_received[id] != 0) {
    // print(2, 0, "duplicate packet");
    ++dup_cnt;
    update_counters();
    return;
  }

  slot_ids_received[id] = 1;
  lcopy(ETH_RX_BUFFER + 2 + sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(WRITE_SECTOR_JOB), cache_position, 512);
  --batch_left;

  multi_sector_write_next();
}

void wait_100ms(void)
{
  // 16 x ~64usec raster lines = ~1ms
  int c = 1600;
  unsigned char b;
  while (c--) {
    b = PEEK(0xD012U);
    while (b == PEEK(0xD012U))
      continue;
  }
}

void get_new_job()
{
  while (PEEK(0xD6E1) & 0x20) {
    POKE(0xD6E1, 0x01);
    POKE(0xD6E1, 0x03);

    lcopy(ETH_RX_BUFFER + 2L, (uint32_t)&recv_buf.eth, sizeof(ETH_HEADER));
    /*
     * Check destination address.
     */

    if ((recv_buf.eth.destination.b[0] & recv_buf.eth.destination.b[1] & recv_buf.eth.destination.b[2]
            & recv_buf.eth.destination.b[3] & recv_buf.eth.destination.b[4] & recv_buf.eth.destination.b[5])
        != 0xff) {
      /*
       * Not broadcast, check if it matches the local address.
       */
      if (memcmp(&recv_buf.eth.destination, &mac_local, sizeof(EUI48)))
        continue;
    }

    if (recv_buf.eth.type == 0x0608) { // big-endian for 0x0806
      /*
       * ARP packet.
       */
      lcopy((uint32_t)&recv_buf.eth.source, (uint32_t)&send_buf.eth.destination, sizeof(EUI48));
      lcopy((uint32_t)&mac_local, (uint32_t)&send_buf.eth.source, sizeof(EUI48));
      send_buf.eth.type = 0x0608;
      lcopy(ETH_RX_BUFFER + 2 + 14, (uint32_t)&send_buf.arp, sizeof(ARP_HDR));
      if (send_buf.arp.opcode != ARP_REQUEST) {
        continue;
      }
      if (ip_addr_set) {
        if (send_buf.arp.dest_ip.d != reply_template.ftp.source.d) {
          continue;
        }
      }
      else {
        if (send_buf.arp.dest_ip.b[3] != 65) {
          continue;
        }
        reply_template.ftp.source.d = send_buf.arp.dest_ip.d;
      }
      lcopy((uint32_t)&send_buf.arp.orig_hw, (uint32_t)&send_buf.arp.dest_hw, sizeof(EUI48));
      lcopy((uint32_t)&mac_local, (uint32_t)&send_buf.arp.orig_hw, sizeof(EUI48));
      send_buf.arp.dest_ip.d = send_buf.arp.orig_ip.d;
      send_buf.arp.orig_ip.d = reply_template.ftp.source.d;
      send_buf.arp.opcode = ARP_REPLY;
      send_buf_size = sizeof(ETH_HEADER) + sizeof(ARP_HDR);
      return;
    }
    else if (recv_buf.eth.type == 0x0008) { // big-endian for 0x0800
      /*
       * IP packet.
       */
      uint16_t udp_length;
      uint16_t num_bytes;

      // We read the header and job data. Since we don't know the exact job, yet, copy the worst case
      // (largest job) which is the write sector command.
      lcopy(ETH_RX_BUFFER + 2 + sizeof(ETH_HEADER), (uint32_t)&recv_buf.ftp.ver_length,
          sizeof(FTP_PKT) + sizeof(WRITE_SECTOR_JOB));

      if (ip_addr_set == 0) {
        if (recv_buf.ftp.destination.b[3] != 65) {
          continue;
        }
      }
      else {
        if (recv_buf.ftp.destination.d != reply_template.ftp.source.d
            || recv_buf.ftp.source.d != reply_template.ftp.destination.d) {
          continue;
        }
      }

      if (recv_buf.ftp.protocol != 17 /*udp*/ || NTOHS(recv_buf.ftp.dst_port) != 4510) {
        continue;
      }

      if (!check_ip_checksum((uint8_t *)&recv_buf.ftp)) {
        uint8_t *data_ptr = (uint8_t *)&recv_buf.ftp;
        print(17, 0, "wrong ip checksum detected");
        dump_bytes(data_ptr, 10, 18);
        dump_bytes(data_ptr + 10, 10, 19);

        ++chks_err_cnt;
        update_counters();
        update_rx_invalid_counter();
        continue;
      }

      if (ip_addr_set == 0) {
        lcopy((uint32_t)&recv_buf.eth.source, (uint32_t)&reply_template.eth.destination, sizeof(EUI48));
        reply_template.ftp.source.d = recv_buf.ftp.destination.d;
        reply_template.ftp.destination.d = recv_buf.ftp.source.d;
        reply_template.ftp.dst_port = recv_buf.ftp.src_port;

        // init pseudo header bytes in udp recv checksum buffer
        *(uint32_t *)0xC800 = recv_buf.ftp.source.d;
        *(uint32_t *)0xC804 = recv_buf.ftp.destination.d;
        *(uint8_t *)0xC808 = 0;
        *(uint8_t *)0xC809 = recv_buf.ftp.protocol;

        ip_addr_set = 1;
        print_ip_informtaion();
      }

      if (recv_buf.ftp.ftp_magic != 0x7165726d /* 'mreq' big endian*/) {
        // printf("Non matching magic bytes: %x", recv_buf.ftp.ftp_magic);
        update_rx_invalid_counter();
        continue;
      }

      udp_length = NTOHS(recv_buf.ftp.udp_length);

      if (!check_udp_checksum())
      {
        ++udp_chks_err_cnt;
        update_counters();
        update_rx_invalid_counter();
        continue;
      }

      switch (recv_buf.ftp.opcode) {
      case 0x02: // write sector
        if (udp_length != 534) {
          update_rx_invalid_counter();
          continue;
        }
        if (recv_buf.write_sector.num_sectors_minus_one == 0) {
          /*
           * Single sector write request
           */
          if (write_batch_active) {
            stop_fatal("error: single/multi write conflict");
          }

          lcopy((uint32_t)&reply_template, (uint32_t)&send_buf,
              sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(WRITE_SECTOR_JOB));
          send_buf.ftp.id = ip_id;
          send_buf.ftp.ip_length = HTONS(20 + 8 + 14);
          send_buf.ftp.udp_length = HTONS(8 + 14);
          send_buf.ftp.seq_num = recv_buf.ftp.seq_num;
          send_buf.ftp.opcode = 2;
          send_buf.write_sector.batch_counter = recv_buf.write_sector.batch_counter;
          send_buf.write_sector.num_sectors_minus_one = 0;
          send_buf.write_sector.slot_index = 0;
          send_buf.write_sector.start_sector_number = recv_buf.write_sector.start_sector_number;
          chks.u = 0;
          checksum((uint8_t *)&send_buf.ftp, 20);
          send_buf.ftp.checksum_ip = ~chks.u;
          send_buf.ftp.checksum_udp = 0;

          ++ip_id;
          send_buf_size = sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(WRITE_SECTOR_JOB);

          wait_for_sd_ready();
          lcopy(ETH_RX_BUFFER + 2 + sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(WRITE_SECTOR_JOB), 0xffd6e00, 512);
          recv_buf.write_sector.start_sector_number += recv_buf.write_sector.slot_index;
          *(uint32_t *)0xD681 = recv_buf.write_sector.start_sector_number;
          POKE(0xD680, 0x57); // Open write gate
          POKE(0xD680, 0x03); // Single sector write command
        }
        else {
          /*
           * Multi sector write request (batch of sectors)
           */
          if (!write_batch_active) {
            if (recv_buf.write_sector.batch_counter != current_batch_counter) {
              init_new_write_batch();
            }
          }

          if (recv_buf.write_sector.slot_index > write_batch_max_id) {
            stop_fatal("error: write slot out of range");
          }
          handle_batch_write();

          lcopy((uint32_t)&reply_template, (uint32_t)&send_buf,
              sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(WRITE_SECTOR_JOB));
          send_buf.ftp.id = ip_id;
          send_buf.ftp.seq_num = recv_buf.ftp.seq_num;
          send_buf.write_sector.batch_counter = recv_buf.write_sector.batch_counter;
          send_buf.write_sector.slot_index = recv_buf.write_sector.slot_index;
          chks.u = 0;
          checksum((uint8_t *)&send_buf.ftp, 20);
          send_buf.ftp.checksum_ip = ~chks.u;
          send_buf.ftp.checksum_udp = 0;

          ++ip_id;
          send_buf_size = sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(WRITE_SECTOR_JOB);
        }
        return;

      case 0x04:
        if (write_batch_active) {
          stop_fatal("error: read/write requests mixed up");
        }

        /*
         * Read sectors request
         */

        if (udp_length != 21) {
          update_rx_invalid_counter();
          continue;
        }
        if (recv_buf.read_sector.unused_1 != 0) {
          stop_fatal("error: sector count > 255 not supported");
        }

        reply_template.ftp.ip_length = HTONS(20 + 8 + 13 + 512);
        reply_template.ftp.udp_length = HTONS(8 + 13 + 512);
        reply_template.ftp.opcode = 4;
        reply_template.read_sector.unused_1 = 0;
        reply_template.read_sector.num_sectors_minus_one = 0;

        batch_left = recv_buf.read_sector.num_sectors_minus_one;
        if (batch_left > 64) {
          stop_fatal("batchleft out of bounds");
        }
        ++batch_left;
        sector_number_read = recv_buf.read_sector.sector_number;
        seq_num = recv_buf.ftp.seq_num;
        read_batch_active = 1;
        // printf("read job q:%d b:%d s:%ld\n", seq_num, batch_left, sector_number_read);
        return;

      case 0x11:
        /*
         * Read memory request
         */

        if (udp_length != 20) {
          update_rx_invalid_counter();
          continue;
        }
        num_bytes = recv_buf.read_memory.num_bytes_minus_one;
        ++num_bytes;
        if (num_bytes > 600) {
          stop_fatal("num_bytes out of range");
        }
        lcopy(
            (uint32_t)&reply_template, (uint32_t)&send_buf, sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(READ_MEMORY_JOB));
        lcopy(recv_buf.read_memory.address,
            (uint32_t)&send_buf.b[sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(READ_MEMORY_JOB)], num_bytes);
        send_buf.ftp.id = ip_id;
        send_buf.ftp.ip_length = HTONS(20 + 8 + 12 + num_bytes);
        send_buf.ftp.udp_length = HTONS(8 + 12 + num_bytes);
        send_buf.ftp.seq_num = recv_buf.ftp.seq_num;
        send_buf.ftp.opcode = 0x11;
        send_buf.read_memory.num_bytes_minus_one = recv_buf.read_memory.num_bytes_minus_one;
        send_buf.read_memory.address = recv_buf.read_memory.address;
        chks.u = 0;
        checksum((uint8_t *)&send_buf.ftp, 20);
        send_buf.ftp.checksum_ip = ~chks.u;
        send_buf.ftp.checksum_udp = 0;

        ++ip_id;
        send_buf_size = sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(READ_MEMORY_JOB) + num_bytes;

        return;

      case 0xfe:
        /*
         * Reset TX request
         */
        if (!eth_controller_reset_done) {
          // Reset Ethernet controller tx, it seemed to stop sending packets
          POKE(0xD6E0, 0x00);
          wait_100ms();
          wait_100ms();
          wait_100ms();
          wait_100ms();
          wait_100ms();
          POKE(0xD6E0, 0x03);
          wait_100ms();
          wait_100ms();
          wait_100ms();
          wait_100ms();
          wait_100ms();
          POKE(0xD6E1, 3);
          POKE(0xD6E1, 0);
          ++tx_reset_cnt;
          update_counters();
          eth_controller_reset_done = 1;
        }
        return;

      case 0xff:
        /*
         * Quit request
         */
        quit_requested = 1;
        lcopy((uint32_t)&reply_template, (uint32_t)&send_buf,
            sizeof(ETH_HEADER) + sizeof(FTP_PKT)); // copy header incl. opcode
        send_buf.ftp.id = ip_id;
        send_buf.ftp.seq_num = recv_buf.ftp.seq_num;
        send_buf.ftp.ip_length = HTONS(20 + 8 + 7);
        send_buf.ftp.udp_length = HTONS(8 + 7);
        send_buf.ftp.opcode = 0xff;
        chks.u = 0;
        checksum((uint8_t *)&send_buf.ftp, 20);
        send_buf.ftp.checksum_ip = ~chks.u;
        send_buf.ftp.checksum_udp = 0;

        send_buf_size = sizeof(ETH_HEADER) + sizeof(FTP_PKT);

        return;
      }
    }
  }
}

void process()
{
  if (send_buf_size > 0) {
    if (!(PEEK(0xD6E0) & 0x80)) {
      return;
    }
    // Copy to TX buffer
    if (send_buf_size > 600) {
      stop_fatal("send_buf_size out of bounds");
    }
    lcopy((uint32_t)&send_buf, ETH_TX_BUFFER, send_buf_size);

    // Set packet length
    POKE(0xD6E2, send_buf_size & 0xff);
    POKE(0xD6E3, send_buf_size >> 8);

    // Send packet
    POKE(0xD6E4, 0x01); // TX now

    eth_controller_reset_done = 0;

    send_buf_size = 0;
  }

  if (batch_left == 0 && write_batch_active) {
    while (slots_written <= write_batch_max_id) {
      multi_sector_write_next();
    }
    write_batch_active = 0;
  }

  if (sector_buffered) {
    lcopy((uint32_t)&reply_template, (uint32_t)&send_buf,
        sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(READ_SECTOR_JOB)); // copy header incl. opcode
    send_buf.ftp.id = ip_id;
    send_buf.ftp.seq_num = seq_num;
    send_buf.read_sector.sector_number = sector_number_buf;
    lcopy((uint32_t)&sector_buf, (uint32_t)&send_buf.b[sizeof(ETH_HEADER) + 41], 0x200);
    chks.u = 0;
    checksum((uint8_t *)&send_buf.ftp, 20);
    send_buf.ftp.checksum_ip = ~chks.u;

    ++ip_id;
    ++seq_num;
    sector_buffered = 0;
    send_buf_size = sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(READ_SECTOR_JOB) + 512;
  }

  if (sector_reading) {
    if (PEEK(0xD680) & 0x03) {
      //POKE(0xD020, PEEK(0xD020) + 1);
      return;
    }
    lcopy(0xffd6e00, (long)sector_buf, 0x200);
    sector_number_buf = sector_number_read;
    --batch_left;
    if (batch_left == 0) {
      read_batch_active = 0;
    }
    ++sector_number_read;
    sector_reading = 0;
    sector_buffered = 1;
  }

  if (read_batch_active) {
    *(uint32_t *)0xD681 = sector_number_read;
    POKE(0xD680, 0x02);
    sector_reading = 1;
    return;
  }

  if (quit_requested) {
    print(1, 0, "quit requested");
    wait_for_sd_ready();
    while (!(PEEK(0xD6E0) & 0x80))
      continue;
    __asm__("jmp 58552");
    // Should never get here
    stop_fatal("error: failed to execute rom reset routine");
  }

  get_new_job();
}

void main(void)
{
  asm("sei");

  // Fast CPU, M65 Ethernet I/O (eth buffer @ $d800)
  POKE(0, 65);
  POKE(0xD02F, 0x45);
  POKE(0xD02F, 0x54);

    // RXPH 1, MCST on, BCST on, TXPH 1, NOCRC off, NOPROM on
  POKE(0xD6E5, 0x75);
  
  POKE(0xD689, PEEK(0xD689) | 128); // Enable SD card buffers instead of Floppy buffer

  // Cursor off
  POKE(204, 0x80);

  init();
  print(1, 0, "mega65 ethernet file transfer helper.");

  print_mac_address();
  update_counters();

  sector_reading = 0;
  sector_buffered = 0;
  send_buf_size = 0;
  wait_for_sd_ready();

  // Prepare response packet
  lcopy((uint32_t)&mac_local, (uint32_t)&reply_template.eth.source, sizeof(EUI48));
  reply_template.eth.type = 0x0008;
  reply_template.ftp.ver_length = 0x45;
  reply_template.ftp.tos = 0;
  reply_template.ftp.frag = 0;
  reply_template.ftp.ttl = 64;
  reply_template.ftp.protocol = 17;
  reply_template.ftp.checksum_ip = 0;
  reply_template.ftp.source.d = 0;
  reply_template.ftp.src_port = HTONS(4510);
  reply_template.ftp.checksum_udp = 0;
  reply_template.ftp.ftp_magic = 0x7073726d; // 'mrsp' big endian

  while (1) {
    __asm__("php");
    __asm__("pla");
    __asm__("sta %v", cpu_status);
    if (!(cpu_status & 0x04)) {
      stop_fatal("error: cpu irq enabled");
    }
    process();
  }
}
