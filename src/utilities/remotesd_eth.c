/*
  Remote SD card access tool for mega65_ftp to more quickly
  send and receive files.

  It implements a simple protocol with pre-emptive sending
  of read data in raw mode at 4mbit = 40KB/sec.  Can do this
  while writing jobs to the SD card etc to hide latency.

*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>
#include <debug.h>
#include <random.h>

#define ETH_RX_BUFFER 0xFFDE800L
#define ETH_TX_BUFFER 0xFFDE800L
#define ARP_REQUEST 0x0100 // big-endian for 0x0001
#define ARP_REPLY 0x0200   // big-endian for 0x0002

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
  uint8_t num_sectors;
  uint8_t unused_1;
  uint32_t sector_number;
} FTP_PKT;

typedef union {
  uint8_t b[1500];
  struct {
    ETH_HEADER eth;
    union {
      ARP_HDR arp;
      FTP_PKT ftp;
    };
  };
} PACKET;

#define NTOHS(x) (((uint16_t)x >> 8) | ((uint16_t)x << 8))
#define HTONS(x) (((uint16_t)x >> 8) | ((uint16_t)x << 8))

PACKET reply_template;

PACKET recv_buf;
uint8_t sector_buf[512];
PACKET send_buf;
uint16_t send_buf_size;
uint8_t sector_reading, sector_buffered;
uint32_t sector_number_read, sector_number_buf;
uint8_t batch_left;
uint16_t seq_num;

EUI48 mac_local;
uint8_t ip_addr_set = 0;

uint16_t ip_id = 0;

void wait_for_sdcard_to_go_busy(void)
{
  while (!(PEEK(0xD680) & 0x03))
    continue;
}

typedef union {
  uint16_t u;
  uint8_t b[2];
} chks_t;
chks_t chks;

static uint8_t _a, _b, _c;
static unsigned int _b16;

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
      if (send_buf.arp.opcode != ARP_REQUEST || send_buf.arp.dest_ip.d != reply_template.ftp.source.d) {
        continue;
      }
      lcopy((uint32_t)&send_buf.arp.orig_hw, (uint32_t)&send_buf.arp.dest_hw, sizeof(EUI48));
      lcopy((uint32_t)&mac_local, (uint32_t)&send_buf.arp.orig_hw, sizeof(EUI48));
      send_buf.arp.dest_ip.d = send_buf.arp.orig_ip.d;
      send_buf.arp.orig_ip.d = reply_template.ftp.source.d;
      send_buf.arp.opcode = ARP_REPLY;
      send_buf_size = sizeof(ETH_HEADER) + sizeof(ARP_HDR);
    }
    else if (recv_buf.eth.type == 0x0008) { // big-endian for 0x0800
      lcopy(ETH_RX_BUFFER + 2 + 14, (uint32_t)&recv_buf.ftp, sizeof(FTP_PKT));
      // printf("IP %d.%d.%d.%d-%d.%d.%d.%d:%d P:%d L:%d\n", recv_buf.ftp.source.b[0], recv_buf.ftp.source.b[1],
      // recv_buf.ftp.source.b[2],
      //     recv_buf.ftp.source.b[3], recv_buf.ftp.destination.b[0], recv_buf.ftp.destination.b[1],
      //     recv_buf.ftp.destination.b[2], recv_buf.ftp.destination.b[3], NTOHS(recv_buf.ftp.dst_port),
      //     recv_buf.ftp.protocol, NTOHS(recv_buf.ftp.udp_length));

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

      if (recv_buf.ftp.protocol != 17 /*udp*/ || NTOHS(recv_buf.ftp.dst_port) != 4510
          || NTOHS(recv_buf.ftp.udp_length) != 21) {
        continue;
      }

      chks.u = 0;
      checksum((uint8_t *)&recv_buf.ftp, 20);
      if (chks.u != 0xffff) {
        printf("IP checksum error\n");
        continue;
      }

      if (ip_addr_set == 0) {
        lcopy((uint32_t)&recv_buf.eth.source, (uint32_t)&reply_template.eth.destination, sizeof(EUI48));
        reply_template.ftp.source.d = recv_buf.ftp.destination.d;
        reply_template.ftp.destination.d = recv_buf.ftp.source.d;
        reply_template.ftp.dst_port = recv_buf.ftp.src_port;
        ip_addr_set = 1;
      }

      if (recv_buf.ftp.unused_1 != 0) {
        printf("Sector count > 255 not supported\n");
        continue;
      }

      if (recv_buf.ftp.ftp_magic != 0x7165726d /*mreq bug endian*/) {
        printf("Non matching magic bytes: %x", recv_buf.ftp.ftp_magic);
        continue;
      }

      batch_left = recv_buf.ftp.num_sectors;
      sector_number_read = recv_buf.ftp.sector_number;
      seq_num = recv_buf.ftp.seq_num;
    }
  }
}

void process()
{
  if (send_buf_size > 0) {
    if (!PEEK(0xD6E0)&0x80) {
      return;
    }

    // Copy to TX buffer
    lcopy((uint32_t)&send_buf, ETH_TX_BUFFER, send_buf_size);

    // Set packet length
    POKE(0xD6E2, send_buf_size & 0xff);
    POKE(0xD6E3, send_buf_size  >> 8);

    // Send packet
    POKE(0xD6E4,0x01); // TX now

    send_buf_size = 0;
  }

  if (sector_buffered) {
    lcopy((uint32_t)&reply_template, (uint32_t)&send_buf, sizeof(ETH_HEADER) + sizeof(FTP_PKT));
    send_buf.ftp.id = ip_id;
    send_buf.ftp.seq_num = seq_num;
    send_buf.ftp.sector_number = sector_number_buf;
    lcopy((uint32_t)&sector_buf, (uint32_t)&send_buf.b[sizeof(ETH_HEADER) + sizeof(FTP_PKT)], 0x200);
    chks.u = 0;
    checksum((uint8_t *)&send_buf.ftp, 20);
    send_buf.ftp.checksum_ip = ~chks.u;

    // Calculate UDP checksum
    /*chks.u = 0;
    checksum((uint8_t *)&send_buf.ftp.ftp_magic, 13 + 512);
    checksum((uint8_t *)&send_buf.ftp.source, 8 + 8); // 8 bytes src/dst ip addresses + 8 bytes udp header
    add_checksum(17); // UDP protocol number
    add_checksum(NTOHS(send_buf.ftp.udp_length)); // payload size + 8 bytes udp header
    */
    send_buf.ftp.checksum_udp = 0;

    ++ip_id;
    ++seq_num;
    sector_buffered = 0;
    send_buf_size = 14 + 20 + 8 + 13 + 512;
  }

  if (sector_reading) {
    if (PEEK(0xD680) & 0x03) {
      POKE(0xD020, PEEK(0xD020) + 1);
      return;
    }
    lcopy(0xffd6e00, (long)sector_buf, 0x200);
    sector_number_buf = sector_number_read;
    --batch_left;
    ++sector_number_read;
    sector_reading = 0;
    sector_buffered = 1;
  }

  if (batch_left > 0) {
    *(uint32_t *)0xD681 = sector_number_read;
    POKE(0xD680, 0x02);
    wait_for_sdcard_to_go_busy();
    sector_reading = 1;
    return;
  }

  get_new_job();
}

void main(void)
{
  uint8_t i;
  asm("sei");

  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);
  POKE(0xD689, PEEK(0xD689) | 128); // Enable SD card buffers instead of Floppy buffer

  // Cursor off
  POKE(204, 0x80);

  srand(random32(0));

  printf("%cMEGA65 SD network access helper.\n\n", 0x93);

  // Read MAC address
  lcopy(0xFFD36E9, (unsigned long)&mac_local.b[0], 6);

  printf("MAC %02x", mac_local.b[0]);
  for (i = 1; i < 6; i++)
    printf(":%02x", mac_local.b[i]);
  printf("\n");

  batch_left = 0;
  sector_reading = 0;
  sector_buffered = 0;
  send_buf_size = 0;
  while (PEEK(0xD680) & 0x03)
    POKE(0xD020, PEEK(0xD020) + 1);

  // Prepare response packet
  lcopy((uint32_t)&mac_local, (uint32_t)&reply_template.eth.source, sizeof(EUI48));
  reply_template.eth.type = 0x0008;
  reply_template.ftp.ver_length = 0x45;
  reply_template.ftp.tos = 0;
  reply_template.ftp.ip_length = HTONS(20 + 8 + 13 + 512);
  reply_template.ftp.frag = 0;
  reply_template.ftp.ttl = 64;
  reply_template.ftp.protocol = 17;
  reply_template.ftp.source.d = 0;
  reply_template.ftp.src_port = HTONS(4510);
  reply_template.ftp.udp_length = HTONS(8 + 13 + 512);
  reply_template.ftp.checksum_udp = 0;
  reply_template.ftp.ftp_magic = 0x7073726d; // 'mrsp' big endian
  reply_template.ftp.opcode = 4;
  reply_template.ftp.num_sectors = 1;
  reply_template.ftp.unused_1 = 0;
  printf("Data: %02x %02x %02x %02x %02x %02x\n", reply_template.b[6], reply_template.b[7], reply_template.b[8], reply_template.b[9], reply_template.b[10], reply_template.b[11]);

  while (1) {
    process();
  }
}
