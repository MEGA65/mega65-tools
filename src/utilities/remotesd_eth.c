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
        };
      };
    };
  };
} PACKET;

#define NTOHS(x) (((uint16_t)x >> 8) | ((uint16_t)x << 8))
#define HTONS(x) (((uint16_t)x >> 8) | ((uint16_t)x << 8))

uint8_t quit_requested = 0;
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

void wait_for_sd_ready()
{
  while (PEEK(0xD680) & 0x03)
    POKE(0xD020, PEEK(0xD020) + 1);
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

  if (!slot_ids_received[slots_written]) {
    printf("WARN: slot #%d not yet available\n", slots_written);
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

  if (recv_buf.write_sector.batch_counter != current_batch_counter) {
    printf("Late duplicate packet for batch #%d\n", recv_buf.write_sector.batch_counter);
    return;
  }

  if (slot_ids_received[id] != 0) {
    printf("WARN: Dup sector data, slot id %d\n", id);
    return;
  }

  slot_ids_received[id] = 1;
  lcopy(ETH_RX_BUFFER + 2 + sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(WRITE_SECTOR_JOB), cache_position, 512);
  --batch_left;

  multi_sector_write_next();
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

      // We read the header and job data. Since we don't know the exact job, yet, copy the worst case
      // (largest job) which is the read sector command.
      lcopy(ETH_RX_BUFFER + 2 + sizeof(ETH_HEADER), (uint32_t)&recv_buf.ftp.ver_length,
          sizeof(FTP_PKT) + sizeof(READ_SECTOR_JOB));
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

      if (recv_buf.ftp.protocol != 17 /*udp*/ || NTOHS(recv_buf.ftp.dst_port) != 4510) {
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
        printf("IP : %d.%d.%d.%d\n", reply_template.ftp.source.b[0], reply_template.ftp.source.b[1],
            reply_template.ftp.source.b[2], reply_template.ftp.source.b[3]);
        printf("\nRemote\nIP : %d.%d.%d.%d\n", reply_template.ftp.destination.b[0], reply_template.ftp.destination.b[1],
            reply_template.ftp.destination.b[2], reply_template.ftp.destination.b[3]);
      }

      if (recv_buf.ftp.ftp_magic != 0x7165726d /* 'mreq' big endian*/) {
        printf("Non matching magic bytes: %x", recv_buf.ftp.ftp_magic);
        continue;
      }

      udp_length = NTOHS(recv_buf.ftp.udp_length);

      switch (recv_buf.ftp.opcode) {
      case 0x02: // write sector
        if (udp_length != 534) {
          continue;
        }
        if (recv_buf.write_sector.num_sectors_minus_one == 0) {
          /*
           * Single sector write request
           */
          if (write_batch_active) {
            printf("ERROR: Single write request while write batch still active\n");
            while (1)
              continue;
          }

          lcopy((uint32_t)&reply_template, (uint32_t)&send_buf,
              sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(WRITE_SECTOR_JOB)); // copy header incl. ftp_magic
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
          //printf("Single write sector %ld\n", recv_buf.write_sector.start_sector_number);
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
          handle_batch_write();

          lcopy((uint32_t)&reply_template, (uint32_t)&send_buf,
              sizeof(ETH_HEADER) + sizeof(FTP_PKT) + sizeof(WRITE_SECTOR_JOB)); // copy header incl. ftp_magic
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
        /*
         * Read sectors request
         */

        if (udp_length != 21) {
          continue;
        }
        if (recv_buf.read_sector.unused_1 != 0) {
          printf("Sector count > 255 not supported\n");
          continue;
        }

        reply_template.ftp.ip_length = HTONS(20 + 8 + 13 + 512);
        reply_template.ftp.udp_length = HTONS(8 + 13 + 512);
        reply_template.ftp.opcode = 4;
        reply_template.read_sector.unused_1 = 0;
        reply_template.read_sector.num_sectors_minus_one = 0;

        batch_left = recv_buf.read_sector.num_sectors_minus_one;
        ++batch_left;
        sector_number_read = recv_buf.read_sector.sector_number;
        seq_num = recv_buf.ftp.seq_num;
        read_batch_active = 1;
        // printf("read job q:%d b:%d s:%ld\n", seq_num, batch_left, sector_number_read);
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
    lcopy((uint32_t)&send_buf, ETH_TX_BUFFER, send_buf_size);

    // Set packet length
    POKE(0xD6E2, send_buf_size & 0xff);
    POKE(0xD6E3, send_buf_size >> 8);

    // Make sure ethernet is not under reset
    POKE(0xD6E0, 0x03);

    // Send packet
    POKE(0xD6E4, 0x01); // TX now

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
      POKE(0xD020, PEEK(0xD020) + 1);
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
    wait_for_sd_ready();
    while (!(PEEK(0xD6E0) & 0x80)) continue;
    __asm__("jmp 58552");
    // Should never get here
    while (1) continue;
  }

  get_new_job();
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

void main(void)
{
  uint8_t i;
  asm("sei");

  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // RXPH 1, MCST off, BCST on, TXPH 1, NOCRC off, NOPROM off
  POKE(0xD6E5, 0x64);


  // Commented Ethernet controller reset out, it will be detected as a new Ethernet connection
  // by the remote computer, flushing ARP cache and doing all kinds of re-initialisation.
  // This just takes time and the controller state should be fine still after etherload
  // transfer of this helper routine.
/*
  POKE(0xD6E0, 0);
  wait_100ms();
  POKE(0xD6E0, 3);
  wait_100ms();
  POKE(0xD6E1, 3);
  POKE(0xD6E1, 0);
*/

  POKE(0xD689, PEEK(0xD689) | 128); // Enable SD card buffers instead of Floppy buffer

  // Cursor off
  POKE(204, 0x80);

  srand(random32(0));

  printf("%cMEGA65 Ethernet File Transfer helper.\n\n", 0x93);

  // Read MAC address
  lcopy(0xFFD36E9, (unsigned long)&mac_local.b[0], 6);

  printf("Local\nMAC: %02x", mac_local.b[0]);
  for (i = 1; i < 6; i++)
    printf(":%02x", mac_local.b[i]);
  printf("\n");

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
    process();
  }
}
