#include <debug.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <memory.h>

#define ETH_RX_BUFFER 0xFFDE800L
#define ETH_TX_BUFFER 0xFFDE800L
#define ARP_REQUEST 0x0100 // big-endian for 0x0001
#define ARP_REPLY 0x0200   // big-endian for 0x0002

uint16_t fastcall ip_checksum_recv();
// from checksum.s:
uint16_t fastcall checksum_fast(uint16_t size);
extern uint16_t chks_pseudo_hdr, chks_pseudo_length;

/**
 * @brief Copy a memory area using DMA with ETH I/O personality enabled.
 *
 * The function will enable the ETH I/O personality, copy the memory area and leave the ETH
 * I/O personality enabled. That personality allows reading and writing of the ETH RX/TX
 * buffers at $D800-$DFFF, so src and dst can use this memory area for read/write.
 *
 * @param src Source address.
 * @param dst Destination address.
 * @param size Number of bytes to copy.
 *
 * @note The function is implemented in assembly language in ip_checksum_recv.s.
*/
void fastcall dma_copy_eth_io(void *src, void *dst, uint16_t size);

uint8_t fastcall cmp_c000_c200();
uint8_t fastcall cmp_c000_c816();

#define ETH_HDR_SIZE 14
#define IPV6_HDR_SIZE 40
#define IPV6_PSEUDO_HDR_SIZE 40
#define UDP_HDR_SIZE 8
#define FTP_HDR_SIZE 7

typedef struct {
  uint8_t b[6];
} EUI48;

typedef struct { 
  uint8_t b[16]; 
} IPV6;

/**
 * Ethernet frame header.
 */
typedef struct {
  EUI48 destination; ///< Packet Destination address.
  EUI48 source;      ///< Packet Source address.
  uint16_t type;     ///< Packet Type or Size (big-endian)
} ETH_HEADER;

/**
 * IPv6 header
 */
typedef struct {
  // IPv6 header
  uint32_t ver_cls_flow; ///< Version (4 bits), Traffic Class (8 bits), and Flow Label (20 bits)
  uint16_t ip_length;    ///< Length of the payload
  uint8_t next_header;   ///< Identifies the type of header immediately following the IPv6 header
  uint8_t hop_limit;     ///< Specifies the maximum number of hops (i.e., links) that the packet may be forwarded on toward its destination
  IPV6 source;           ///< 16 bytes: Source IPv6 address
  IPV6 destination;      ///< 16 bytes: Destination IPv6 address
} IPV6_HEADER;

/**
 * ICMPv6 ND Solicit/Advertise message format.
 */
typedef struct {
  uint8_t icmpv6_type;
  uint8_t icmpv6_code;
  uint16_t checksum;
  uint32_t flags;
  IPV6 target_address;
  uint8_t link_layer_type;
  uint8_t link_layer_length;
  EUI48 link_layer_address;
} NEIGHBOR_DISCOVERY;

/**
 * FTP Header format.
 */
typedef struct {
  // UDP header
  uint16_t src_port;     ///< Source application address.
  uint16_t dst_port;     ///< Destination application address.
  uint16_t udp_length;   ///< UDP packet size.
  uint16_t checksum_udp; ///< Packet checksum, with pseudo-header.
  // mega65_ftp protocol header
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
    IPV6_HEADER ipv6;
    union {
      NEIGHBOR_DISCOVERY nd;
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
uint16_t last_eth_controller_reset_seq_no = 0;
PACKET reply_template;
PACKET recv_buf;
uint16_t udp_length;
uint8_t sector_buf[512];
#pragma bss-name (push, "SENDBUF")
/*
 We want this to be in segment SENDBUF, so it will be at $C7CA
 The reason is we want the fast checksum routine to operate starting
 at $c800, which will be the first byte of the IPv6 payload.
 The Ethernet header (14 bytes) will be at $C7CA-$C7D7, the
 IPv6 header will be at $C7D8-$C7FF.
*/
PACKET send_buf; 
#pragma bss-name (pop)
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
IPV6 ip_local;

uint32_t udp_chks_err_cnt = 0;
uint32_t outdated_cnt = 0;
uint32_t dup_cnt = 0;
uint32_t tx_reset_cnt = 0;
uint32_t rx_invalid_cnt = 0;
uint32_t rx_valid_cnt = 0;
uint32_t tx_cnt = 0;
uint32_t unauth_cnt = 0;

typedef union {
  uint16_t u;
  uint8_t b[2];
} chks_t;
chks_t chks;
chks_t chks_pseudo_hdr_udp;
chks_t chks_pseudo_hdr_ndp;

static uint8_t _a, _b, _c;
static unsigned int _b16;

void init(void);
void init_screen(void);
void init_own_ipv6_address(void);
void init_pseudo_checksum_ndp(void);
void init_pseudo_checksum_udp(void);
void print(uint8_t row, uint8_t col, char *text);
void stop_fatal(char *text);
void print_core_commit();
void print_mac_address(void);
void print_ip_information(void);
void update_counters(void);
void update_rx_tx_counters(void);
void dump_bytes(uint8_t *data, uint8_t n, uint8_t screen_line);
const char *ipv6_to_str(const uint8_t *addr);
void checksum(uint8_t *buf, uint16_t size);
void add_checksum(uint16_t v);
uint8_t check_udp_checksum(void);
void calculate_send_buf_udp_checksum(uint16_t size);
void wait_for_sd_ready(void);
void init_new_write_batch(void);
void multi_sector_write_next(void);
uint8_t is_received_batch_counter_outdated(uint8_t previous_id, uint8_t received_id);
void handle_batch_write(void);
void wait_rasters(uint16_t num_rasters);
void verify_sector(void);
void wait_100ms(void);
void get_new_job(void);

void init(void)
{
  // Fast CPU, M65 Ethernet I/O (eth buffer @ $d800)
  POKE(0, 65);
  POKE(0xD02F, 0x45);
  POKE(0xD02F, 0x54);

    // RXPH 1, MCST on, BCST on, TXPH 1, NOCRC off, NOPROM on
  POKE(0xD6E5, 0x75);
  
  POKE(0xD689, PEEK(0xD689) | 128); // Enable SD card buffers instead of Floppy buffer

  init_screen();

  init_own_ipv6_address();

  sector_reading = 0;
  sector_buffered = 0;
  send_buf_size = 0;
  wait_for_sd_ready();

  // Prepare response packet
  lcopy((uint32_t)&mac_local, (uint32_t)&reply_template.eth.source, sizeof(EUI48));
  reply_template.eth.type = 0xdd86; // little endian for eth type IPv6 (86dd)
  reply_template.ipv6.ver_cls_flow = 0x60; // IPv6 version 6, traffic class 0, flow label 0, little endian
  reply_template.ipv6.next_header = 17; // UDP
  reply_template.ipv6.hop_limit = 64;
  memcpy(reply_template.ipv6.source.b, ip_local.b, 16);
  reply_template.ftp.src_port = HTONS(4510);
  reply_template.ftp.dst_port = 0;
  reply_template.ftp.checksum_udp = 0;
  reply_template.ftp.ftp_magic = 0x7073726d; // 'mrsp' big endian
}

void init_screen()
{
  // Cursor off
  POKE(204, 0x80);

  POKE(0xD020, 0);
  POKE(0xD021, 0);
  // clear screen memory
  lfill(0x400, 0x20, 1000);
  // clear color ram
  lfill(0xff80000, 5, 1000);

  print(1, 0, "mega65 ethernet file transfer helper.");

  print_mac_address();
  print_core_commit();

  print(11, 0, "udp chks:      0");
  print(12, 0, "outdated:      0");
  print(13, 0, "duplicate:     0");
  print(14, 0, "tx resets:     0");
  print(15, 0, "rx/tx/invalid: 0/0/0");
  print(16, 0, "unauthorized:  0");

  update_counters();
}

void init_own_ipv6_address()
{
  uint8_t i;

  // Generate IPv6 address from MAC address
  ip_local.b[0] = 0xfe;
  ip_local.b[1] = 0x80;
  for (i = 2; i < 8; ++i) {
    ip_local.b[i] = 0;
  }
  ip_local.b[8] = mac_local.b[0] ^ 0x02;
  ip_local.b[9] = mac_local.b[1];
  ip_local.b[10] = mac_local.b[2];
  ip_local.b[11] = 0xff;
  ip_local.b[12] = 0xfe;
  ip_local.b[13] = mac_local.b[3];
  ip_local.b[14] = mac_local.b[4];
  ip_local.b[15] = mac_local.b[5];
}

void init_pseudo_checksum_ndp(void)
{
  uint8_t pseudo_hdr[36];

  lcopy((uint32_t)&ip_local, (uint32_t)&pseudo_hdr[0], 16);
  lcopy((uint32_t)&send_buf.ipv6.destination, (uint32_t)&pseudo_hdr[16], 16);
  pseudo_hdr[32] = 0;
  pseudo_hdr[33] = 0;
  pseudo_hdr[34] = 0;
  pseudo_hdr[35] = 58; // ICMPv6

  chks.u = 0;
  checksum(pseudo_hdr, 36);
  chks_pseudo_hdr_ndp.u = chks.u;
}

void init_pseudo_checksum_udp(void)
{
  uint8_t pseudo_hdr[36];

  lcopy((uint32_t)&ip_local, (uint32_t)&pseudo_hdr[0], 16);
  lcopy((uint32_t)&reply_template.ipv6.destination, (uint32_t)&pseudo_hdr[16], 16);
  pseudo_hdr[32] = 0;
  pseudo_hdr[33] = 0;
  pseudo_hdr[34] = 0;
  pseudo_hdr[35] = 17; // UDP

  chks.u = 0;
  checksum(pseudo_hdr, 36);
  chks_pseudo_hdr_udp.u = chks.u;
}

void print(uint8_t row, uint8_t col, char *text)
{
  uint16_t addr = 0x400u + 40u * row + col;

  lfill(addr, 0x20, 40 - col);
  while (*text) {
    *((char *)addr++) = *text++;
    
    if (addr > 0x7e8) {
      stop_fatal("print out of bounds error");
    }
  }
}

void stop_fatal(char *text)
{
  print(2, 0, text);
  while (1) {
    POKE(0xD020, 1);
    POKE(0xD020, 2);
  }
}

void print_core_commit()
{
  uint8_t commit[4];
  lcopy(0xffd3632UL, (uint32_t)&commit, 4);
  sprintf(msg, "%02x%02x%02x%02x", commit[3], commit[2], commit[1], commit[0]);
  print(24, 32, msg);
}

void print_mac_address()
{
  // Read MAC address
  lcopy(0xFFD36E9, (unsigned long)&mac_local.b[0], 6);
  sprintf(msg, "mac: %02x:%02x:%02x:%02x:%02x:%02x", mac_local.b[0], mac_local.b[1], mac_local.b[2], mac_local.b[3],
      mac_local.b[4], mac_local.b[5]);
  print(3, 0, "local");
  print(4, 0, msg);

  if ((mac_local.b[0] | mac_local.b[1] | mac_local.b[2] | mac_local.b[3] | mac_local.b[4] | mac_local.b[5]) == 0) {
    stop_fatal("no mac address! please configure first.");
  }
}

void print_ip_information(void)
{
  sprintf(msg, "ip : \x1b%s\x1d", ipv6_to_str(ip_local.b));
  print(5, 0, msg);
  print(7, 0, "remote");
  sprintf(msg, "ip : \x1b%s\x1d:%u", ipv6_to_str(reply_template.ipv6.destination.b), NTOHS(reply_template.ftp.dst_port));
  print(8, 0, msg);
}

void update_counters(void)
{
  static const uint8_t col = 15;

  sprintf(msg, "%lu", udp_chks_err_cnt);
  print(11, col, msg);
  sprintf(msg, "%lu", outdated_cnt);
  print(12, col, msg);
  sprintf(msg, "%lu", dup_cnt);
  print(13, col, msg);
  sprintf(msg, "%lu", tx_reset_cnt);
  print(14, col, msg);
  sprintf(msg, "%lu", unauth_cnt);
  print(16, col, msg);
}

void update_rx_tx_counters(void)
{
  sprintf(msg, "%lu/%lu/%lu", rx_valid_cnt, tx_cnt, rx_invalid_cnt);
  print(15, 15, msg);
}

/**
 * @brief Dump an array of bytes to the screen.
 *
 * This function takes an array of bytes and prints them to the screen in hexadecimal format.
 *
 * @param data Pointer to the array of bytes to be dumped.
 * @param n Number of bytes to be dumped.
 * @param screen_line The line number on the screen where the dump should start.
 */
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

const char *ipv6_to_str(const uint8_t *addr)
{
  uint8_t i, curpos, curlen, run;
  int8_t maxpos, maxlen;
  char hextet[5];
  static char result[40];

  result[0] = '\0';
  maxpos = -1;
  maxlen = -1;
  run = 0;

  for (i = 0; i < 16; i += 2) {
    if (addr[i] == 0 && addr[i+1] == 0) {
      if (!run) {
        curpos = i;
        curlen = 0;
        run = 1;
      }
      curlen += 2;
    }
    else {
      if (run) {
        if (curlen > maxlen) {
          maxlen = curlen;
          maxpos = curpos;
        }
        run = 0;
      }
    }
  }

  if (run) {
    if (curlen > maxlen) {
      maxlen = curlen;
      maxpos = curpos;
    }
  }

  if (maxlen == 16) {
    return "::";
  }

  for (i = 0; i < 16; i += 2){
    sprintf(hextet, "%x", (((uint16_t)addr[i] << 8) + addr[i+1]));
    if (i == maxpos) {
      if (i == 0) {
        strcat(result, "::");
      }
      else {
        strcat(result, ":");
      }
      i += maxlen - 2;
    }
    else {
      strcat(result, hextet);
      if (i < 14) {
        strcat(result, ":");
      }
    }
  }

  return result;
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

/**
 * Checks the UDP checksum of the received packet (recv_buf).
 * @return 1 if the checksum is correct, 0 otherwise.
 */
uint8_t check_udp_checksum()
{
  static uint16_t ref_chks;

  if (udp_length > 1400)
  {
    stop_fatal("udp length too large");
  }

  if (udp_length < 8)
  {
    stop_fatal("udp length too small");
  }
  dma_copy_eth_io((void *)(0xD800U + 2 + ETH_HDR_SIZE + IPV6_HDR_SIZE), (void *)(0xC800U), udp_length);
  
  ref_chks = *(uint16_t *)0xC806;
  *(uint16_t *)0xC806 = 0; // reset checksum field

  chks_pseudo_hdr = chks_pseudo_hdr_udp.u;
  chks_pseudo_length = recv_buf.ftp.udp_length;
  chks.u = checksum_fast(udp_length);
  //chks.u = 0;
  //checksum((uint8_t *)0xC800U, udp_length);

  if (chks.u == 0) {
    chks.u = 0xffffU;
  }
  if (chks.u == ref_chks) {
    return 1;
  }
  sprintf(msg, "udp chks   rx: %04x act: %04x len: %04x", ref_chks, chks.u, udp_length + 40);
  print(23, 0, msg);
  
  return 0;
}

void calculate_send_buf_udp_checksum(uint16_t size)
{
  chks_pseudo_hdr = chks_pseudo_hdr_udp.u;
  chks_pseudo_length = send_buf.ftp.udp_length;
  // checksum field should alredy be zero (it was set to zero in the reply_template)
  send_buf.ftp.checksum_udp = checksum_fast(size);
  if (send_buf.ftp.checksum_udp == 0) {
    send_buf.ftp.checksum_udp = 0xffff;
  }
}

void wait_for_sd_ready()
{
  while (PEEK(0xD680) & 0x03) {
    //POKE(0xD020, PEEK(0xD020) + 1);
    continue;
  }
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

  reply_template.ipv6.ip_length = HTONS(UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(WRITE_SECTOR_JOB));
  reply_template.ipv6.next_header = 17; // UDP
  reply_template.ftp.udp_length = reply_template.ipv6.ip_length;
  reply_template.ftp.opcode = 2;
  reply_template.write_sector.num_sectors_minus_one = write_batch_max_id;
  reply_template.write_sector.start_sector_number = sector_number_write;
}

void multi_sector_write_next()
{
  static uint32_t verify_sector_number = 0;
  static uint32_t verify_cache_position = 0x40000ul;
  static uint8_t i;

  int cmd = 5; // Multi-sector mid

  if (slots_written > write_batch_max_id) {
    stop_fatal("error: slots written out of bounds");
  }

  if (!slot_ids_received[slots_written]) {
    // print(2, 0, "retransmission detected");
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

  if (slots_written <= write_batch_max_id) {
    return;
  }

  // verify complete batch written against cache
  verify_sector_number = sector_number_write;
  verify_cache_position = 0x40000ul;
  wait_for_sd_ready();
  // read first sector to verify
  *(uint32_t *)0xD681 = verify_sector_number;
  POKE(0xD680, 0x02);
  for (i = 0; i < slots_written; ++i) {
    wait_for_sd_ready();
    lcopy(0xffd6e00UL, 0xC200, 512);
    // we saved the sector data to sector_buf, now already trigger read of the next sector
    // so it can happen in the background while we verify the last one
    if (i != slots_written - 1) {
      ++verify_sector_number;
      *(uint32_t *)0xD681 = verify_sector_number;
      POKE(0xD680, 0x02);
    }
    lcopy(verify_cache_position, 0xC000UL, 512);
    if (cmp_c000_c200()) {
      sprintf(msg, "verify batch failed at block %05lx", verify_cache_position);
      stop_fatal(msg);
    }
    verify_cache_position += 512;
  }
}

uint8_t is_received_batch_counter_outdated(uint8_t previous_id, uint8_t received_id)
{
  if (received_id == previous_id) {
    return 0;
  }
  if ((uint8_t)(previous_id - received_id) < ((uint8_t)0x80)) {
    return 1;
  }
  return 0;
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
    // verify received data matches cache
    lcopy(cache_position, 0xC000U, 512);
    if (cmp_c000_c816()) {
      stop_fatal("duplicate packet with different data");
    }
    ++dup_cnt;
    update_counters();
    return;
  }

  slot_ids_received[id] = 1;
  //lcopy(ETH_RX_BUFFER + 2 + ETH_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(WRITE_SECTOR_JOB), cache_position, 512);
  lcopy(0xC800UL + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(WRITE_SECTOR_JOB), cache_position, 512);
  --batch_left;

  multi_sector_write_next();
}

void wait_rasters(uint16_t num_rasters)
{
  static unsigned char b;
  while (num_rasters)
  {
    b = PEEK(0xD012U);
    while (b == PEEK(0xD012U))
      continue;
    --num_rasters;
  }
}

void verify_sector()
{
  wait_rasters(2);
  wait_for_sd_ready();
  *(uint32_t *)0xD681 = recv_buf.write_sector.start_sector_number;
  POKE(0xD680, 0x02);
  wait_rasters(2);
  wait_for_sd_ready();

  lcopy(0xffd6e00UL, 0xC000UL, 512);
  if (cmp_c000_c816()) {
    stop_fatal("sector verification failed");
  }
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

    // We read the header and job data. Since we don't know the exact job, yet, copy the worst case
    // (largest job) which is the write sector command.
    lcopy(ETH_RX_BUFFER + 2L, (uint32_t)&recv_buf, ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(WRITE_SECTOR_JOB));

    /*
     * Check destination address.
     */

    if (memcmp(&recv_buf.eth.destination, &mac_local, sizeof(EUI48))) {
      // Not unicast for us, check if multicast for NDP
      if (recv_buf.eth.destination.b[0] != 0x33 || 
          recv_buf.eth.destination.b[1] != 0x33 || 
          recv_buf.eth.destination.b[2] != 0xff ||
          recv_buf.eth.destination.b[3] != mac_local.b[3] || 
          recv_buf.eth.destination.b[4] != mac_local.b[4] ||
          recv_buf.eth.destination.b[5] != mac_local.b[5]) {
        // Not unicast nor NDP multicast, check if broadcast
        if ((recv_buf.eth.destination.b[0] & recv_buf.eth.destination.b[1] & recv_buf.eth.destination.b[2]
              & recv_buf.eth.destination.b[3] & recv_buf.eth.destination.b[4] & recv_buf.eth.destination.b[5])
            != 0xff) {
          continue;
        }
      }
    }

    if (recv_buf.eth.type != 0xdd86) { // big-endian for 0x86dd
      continue;
    }

    if (recv_buf.ipv6.next_header == 58 &&  // ICMPv6
        recv_buf.nd.icmpv6_type == 135 &&   // NDP Neighbor Solicitation
        HTONS(recv_buf.ipv6.ip_length) >= sizeof(NEIGHBOR_DISCOVERY)) {   
      /*
       * ND solicitation packet.
       */
      lcopy((uint32_t)&recv_buf.eth.source, (uint32_t)&send_buf.eth.destination, sizeof(EUI48));
      lcopy((uint32_t)&mac_local, (uint32_t)&send_buf.eth.source, sizeof(EUI48));
      send_buf.eth.type = 0xdd86;
      lcopy(ETH_RX_BUFFER + 2 + ETH_HDR_SIZE, (uint32_t)&send_buf.ipv6, IPV6_HDR_SIZE + sizeof(NEIGHBOR_DISCOVERY));
      if (memcmp(send_buf.nd.target_address.b, ip_local.b, 16)) {
        continue;
      }
      // prepare IPv6 header
      send_buf.ipv6.ip_length = HTONS(sizeof(NEIGHBOR_DISCOVERY));
      lcopy((uint32_t)&send_buf.ipv6.source, (uint32_t)&send_buf.ipv6.destination, sizeof(IPV6));
      lcopy((uint32_t)&ip_local, (uint32_t)&send_buf.ipv6.source, sizeof(IPV6));
      // prepare ICMPv6 header
      send_buf.nd.icmpv6_type = 136; // NDP Neighbor Advertisement
      send_buf.nd.checksum = 0; // clear checksum field
      send_buf.nd.flags = HTONS(0x6000); // Solicited flag
      // option type 2: target link layer address
      send_buf.nd.link_layer_type = 2; // Target link layer address
      send_buf.nd.link_layer_length = 1; // 6 bytes
      lcopy((uint32_t)&mac_local, (uint32_t)&send_buf.nd.link_layer_address, sizeof(EUI48));

      // calculate ICMPv6 checksum
      init_pseudo_checksum_ndp();
      chks.u = 0;
      chks_pseudo_hdr = chks_pseudo_hdr_ndp.u;
      chks_pseudo_length = HTONS(32);
      send_buf.nd.checksum = checksum_fast(sizeof(NEIGHBOR_DISCOVERY));
      if (send_buf.nd.checksum == 0) {
        send_buf.nd.checksum = 0xffff;
      }
      send_buf_size = ETH_HDR_SIZE + IPV6_HDR_SIZE + sizeof(NEIGHBOR_DISCOVERY);
      return;
    }
    else if (recv_buf.ipv6.next_header == 17) { // udp
      /*
       * UDP packet.
       */
      uint16_t num_bytes;

      if (memcmp(recv_buf.ipv6.destination.b, ip_local.b, 16)) {
        continue;
      }
      
      if (NTOHS(recv_buf.ftp.dst_port) != 4510) {
        continue;
      }

      if (reply_template.ftp.dst_port == 0) {
        // dst_port is unknown, so we have don't have an authenticated connection yet
        lcopy((uint32_t)&recv_buf.eth.source, (uint32_t)&reply_template.eth.destination, sizeof(EUI48));
        lcopy((uint32_t)&recv_buf.ipv6.source, (uint32_t)&reply_template.ipv6.destination, sizeof(IPV6));
        init_pseudo_checksum_udp();
      }
      else {
        // dst_port is known, so we have an authenticated connection
        // reject packets from other clients
        if (memcmp(recv_buf.ipv6.source.b, reply_template.ipv6.destination.b, 16)) {
          continue;
        }
      }

      if (recv_buf.ftp.ftp_magic != 0x7165726d /* 'mreq' big endian*/) {
        // printf("Non matching magic bytes: %x", recv_buf.ftp.ftp_magic);
        ++rx_invalid_cnt;
        update_rx_tx_counters();
        continue;
      }

      udp_length = NTOHS(recv_buf.ftp.udp_length);

      if (!check_udp_checksum())
      {
        ++udp_chks_err_cnt;
        update_counters();
        ++rx_invalid_cnt;
        update_rx_tx_counters();
        continue;
      }

      if (reply_template.ftp.dst_port != 0) {
        if (recv_buf.ftp.src_port != reply_template.ftp.dst_port) {
          print(21, 0, "detected multiple clients active");
          continue;
        }
      }
      else {
        if (recv_buf.ftp.opcode != 0xfd) {
          ++unauth_cnt;
          update_counters();
          continue;
        }
      }


      switch (recv_buf.ftp.opcode) {
      case 0x02: // write sector
        if (udp_length != 534) {
          ++rx_invalid_cnt;
          update_rx_tx_counters();
          continue;
        }
        if (recv_buf.write_sector.num_sectors_minus_one == 0) {
          /*
           * Single sector write request
           */
          if (write_batch_active) {
            // usually, the sender will take care that packets for a new batch are only
            // sent out if all packets of the previous batch have been acknowledged.
            // However, it can happen that we already have retransmissions of old
            // packets in the queue that show up after a new batch has already started.
            // In this case, we have to ignore the old packets.
            if (is_received_batch_counter_outdated(current_batch_counter, recv_buf.write_sector.batch_counter)) {
              ++outdated_cnt;
              update_counters();
              continue;
            }
            stop_fatal("error: single/multi write conflict");
          }

          ++rx_valid_cnt;

          if (is_received_batch_counter_outdated(current_batch_counter, recv_buf.write_sector.batch_counter)) {
            stop_fatal("error: outdated batch counter");
          }
          current_batch_counter = recv_buf.write_sector.batch_counter;

          wait_for_sd_ready();
          lcopy(ETH_RX_BUFFER + 2 + ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(WRITE_SECTOR_JOB), 0xffd6e00, 512);
          recv_buf.write_sector.start_sector_number += recv_buf.write_sector.slot_index;
          *(uint32_t *)0xD681 = recv_buf.write_sector.start_sector_number;
          POKE(0xD680, 0x57); // Open write gate
          POKE(0xD680, 0x03); // Single sector write command
          verify_sector();

          lcopy((uint32_t)&reply_template, (uint32_t)&send_buf,
              ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(WRITE_SECTOR_JOB));
          send_buf.ipv6.ip_length = HTONS(UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(WRITE_SECTOR_JOB));
          send_buf.ftp.udp_length = send_buf.ipv6.ip_length;
          send_buf.ftp.seq_num = recv_buf.ftp.seq_num;
          send_buf.ftp.opcode = 2;
          send_buf.write_sector.batch_counter = recv_buf.write_sector.batch_counter;
          send_buf.write_sector.num_sectors_minus_one = 0;
          send_buf.write_sector.slot_index = 0;
          send_buf.write_sector.start_sector_number = recv_buf.write_sector.start_sector_number;

          calculate_send_buf_udp_checksum(UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(WRITE_SECTOR_JOB));

          send_buf_size = ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(WRITE_SECTOR_JOB);
        }
        else {
          /*
           * Multi sector write request (batch of sectors)
           */
          if (!write_batch_active) {
            if (recv_buf.write_sector.batch_counter != current_batch_counter) {
              snprintf(msg, 80, "new batch %d, size %d\n", recv_buf.write_sector.batch_counter, recv_buf.write_sector.num_sectors_minus_one + 1);
              debug_msg(msg);
              init_new_write_batch();
            }
            else {
              snprintf(msg, 80, "outdated batch %d (current %d)\n", recv_buf.write_sector.batch_counter, current_batch_counter);
              debug_msg(msg);
              ++outdated_cnt;
              update_counters();
            }
          }
          
          ++rx_valid_cnt;

          if (recv_buf.write_sector.batch_counter == current_batch_counter) {
            handle_batch_write();
          }

          lcopy((uint32_t)&reply_template, (uint32_t)&send_buf,
              ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(WRITE_SECTOR_JOB));
          send_buf.ftp.seq_num = recv_buf.ftp.seq_num;
          send_buf.write_sector.batch_counter = recv_buf.write_sector.batch_counter;
          send_buf.write_sector.slot_index = recv_buf.write_sector.slot_index;
          
          calculate_send_buf_udp_checksum(UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(WRITE_SECTOR_JOB));

          send_buf_size = ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(WRITE_SECTOR_JOB);
        }
        return;

      case 0x04:
        /*
         * Read sectors request
         */
         
        if (write_batch_active) {
          stop_fatal("error: read/write requests mixed up");
        }

        if (udp_length != 21) {
          ++rx_invalid_cnt;
          update_rx_tx_counters();
          continue;
        }
        if (recv_buf.read_sector.unused_1 != 0) {
          stop_fatal("error: sector count > 255 not supported");
        }

        ++rx_valid_cnt;

        reply_template.ipv6.ip_length = HTONS(UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(READ_SECTOR_JOB) + 512);
        reply_template.ftp.udp_length = reply_template.ipv6.ip_length;
        reply_template.ftp.opcode = 4;
        reply_template.read_sector.unused_1 = 0;
        reply_template.read_sector.num_sectors_minus_one = 0;

        batch_left = recv_buf.read_sector.num_sectors_minus_one;
        if (batch_left > 127) {
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
          ++rx_invalid_cnt;
          update_rx_tx_counters();
          continue;
        }
        num_bytes = recv_buf.read_memory.num_bytes_minus_one;
        ++num_bytes;
        if (num_bytes > 600) {
          stop_fatal("num_bytes out of range");
        }
        ++rx_valid_cnt;
        lcopy(
            (uint32_t)&reply_template, (uint32_t)&send_buf, ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(READ_MEMORY_JOB));
        lcopy(recv_buf.read_memory.address,
            (uint32_t)&send_buf.b[ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(READ_MEMORY_JOB)], num_bytes);
        send_buf.ipv6.ip_length = HTONS(UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(READ_MEMORY_JOB) + num_bytes);
        send_buf.ftp.udp_length = send_buf.ipv6.ip_length;
        send_buf.ftp.seq_num = recv_buf.ftp.seq_num;
        send_buf.ftp.opcode = 0x11;
        send_buf.read_memory.num_bytes_minus_one = recv_buf.read_memory.num_bytes_minus_one;
        send_buf.read_memory.address = recv_buf.read_memory.address;
        
        calculate_send_buf_udp_checksum(UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(READ_MEMORY_JOB) + num_bytes);

        send_buf_size = ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(READ_MEMORY_JOB) + num_bytes;

        return;

      case 0xfd:
        /*
         * Hello request
         */
        if (reply_template.ftp.dst_port != 0) {
          if (recv_buf.ftp.src_port != reply_template.ftp.dst_port) {
            stop_fatal("hello requests from multiple clients");
          }
          continue;
        }
        reply_template.ftp.dst_port = recv_buf.ftp.src_port;
        print_ip_information();
        lcopy((uint32_t)&reply_template, (uint32_t)&send_buf,
              ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE); // copies header incl. opcode (will patch it below)
        send_buf.ftp.seq_num = recv_buf.ftp.seq_num;
        send_buf.ipv6.ip_length = HTONS(UDP_HDR_SIZE + FTP_HDR_SIZE);
        send_buf.ftp.udp_length = send_buf.ipv6.ip_length;
        send_buf.ftp.opcode = 0xfd;
        
        calculate_send_buf_udp_checksum(UDP_HDR_SIZE + FTP_HDR_SIZE);

        send_buf_size = ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE;

        return;

      case 0xfe:
        /*
         * Reset TX request
         */
        if (last_eth_controller_reset_seq_no != recv_buf.ftp.seq_num) {
          // Reset Ethernet controller tx, it seemed to stop sending packets
          wait_100ms();
          POKE(0xD6EF, 1);
          sprintf(msg, "tx state: %02x tx idle: %02x", PEEK(0xD6EF), PEEK(0xD6E0));
          print(22, 0, msg);
          POKE(0xD6EF, 2);
          sprintf(msg, "tx count: %02x", PEEK(0xD6EF));
          print(23, 0, msg);
          POKE(0xD6E0, 0x01);
          wait_100ms();
          POKE(0xD6E0, 0x03);
          wait_100ms();
          //POKE(0xD6E1, 3);
          //POKE(0xD6E1, 0);
          ++tx_reset_cnt;
          update_counters();
          last_eth_controller_reset_seq_no = recv_buf.ftp.seq_num;
        }
        return;

      case 0xff:
        /*
         * Quit request
         */
        quit_requested = 1;
        lcopy((uint32_t)&reply_template, (uint32_t)&send_buf,
            ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE); // copies header incl. opcode (will patch it below)
        send_buf.ftp.seq_num = recv_buf.ftp.seq_num;
        send_buf.ipv6.ip_length = HTONS(UDP_HDR_SIZE + FTP_HDR_SIZE);
        send_buf.ftp.udp_length = send_buf.ipv6.ip_length;
        send_buf.ftp.opcode = 0xff;

        calculate_send_buf_udp_checksum(UDP_HDR_SIZE + FTP_HDR_SIZE);

        send_buf_size = ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE;

        return;
      }
    }
  }
}


/**
 * @brief     Process pipeline work, eg. send packets, read/write sectors, etc.
 *      
 * @details   The goal is to keep the pipeline full, so that the CPU is not waiting for I/O operations.
 *            Especially, sending packets and reading data from SD are operations that can happen in the background.
 * @note      This function is called from the main loop.
 */
void process()
{
  // send_buf_size > 0 indicates we need to send out a packet,
  if (send_buf_size > 0) {
    // Check if TX controller is busy
    if (!(PEEK(0xD6E0) & 0x80)) {
      return;
    }
    // Copy to TX buffer
    if (send_buf_size > 600) {
      stop_fatal("send_buf_size out of bounds");
    }
    //lcopy((uint32_t)&send_buf, ETH_TX_BUFFER, send_buf_size);
    dma_copy_eth_io(&send_buf, (void*)0xD800U, send_buf_size);

    // Set packet length
    POKE(0xD6E2, send_buf_size & 0xff);
    POKE(0xD6E3, send_buf_size >> 8);

    // Send packet, will be sent immediately in the background
    POKE(0xD6E4, 0x01); // TX now

    tx_cnt++;

    // Indicate we sent the data
    send_buf_size = 0;
  }

  // We received all sectors of a batch, but there still are some slots left to be written
  // to the SD card.
  if (batch_left == 0 && write_batch_active) {
    while (slots_written <= write_batch_max_id) {
      multi_sector_write_next();
    }
    write_batch_active = 0;
  }

  if (sector_buffered) {
    lcopy((uint32_t)&reply_template, (uint32_t)&send_buf,
        ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(READ_SECTOR_JOB)); // copy header incl. opcode
    send_buf.ftp.seq_num = seq_num;
    send_buf.read_sector.sector_number = sector_number_buf;
    lcopy((uint32_t)&sector_buf, (uint32_t)&send_buf.b[ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(READ_SECTOR_JOB)], 0x200);
    
    calculate_send_buf_udp_checksum(UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(READ_SECTOR_JOB) + 512);

    ++seq_num;
    sector_buffered = 0;
    send_buf_size = ETH_HDR_SIZE + IPV6_HDR_SIZE + UDP_HDR_SIZE + FTP_HDR_SIZE + sizeof(READ_SECTOR_JOB) + 512;
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
  static uint8_t frame_cnt = 0;
  static uint8_t last_frame_cnt = 0;
  static uint8_t frames_elapsed = 0;

  // Disable all interrupts
  asm("sei");

  init();

  while (1) {
    // continuously check whether irqs are still disabled
    // as these would mess up our program flow
    __asm__("php");
    __asm__("pla");
    __asm__("sta %v", cpu_status);
    if (!(cpu_status & 0x04)) {
      stop_fatal("error: cpu irq enabled");
    }
    
    // main loop function
    process();

    frame_cnt = PEEK(0xD7FA);
    if (frame_cnt != last_frame_cnt) {
      last_frame_cnt = frame_cnt;
      ++frames_elapsed;
      if (frames_elapsed > 10) {
        frames_elapsed = 0;
        update_rx_tx_counters();
      }
    }
  }
}
