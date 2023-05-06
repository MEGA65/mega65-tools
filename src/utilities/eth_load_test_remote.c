#include <stdint.h>
#include <stdio.h>
#include <memory.h>

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
  uint32_t magic;
  uint16_t seq_num;
  uint32_t data;
} TEST_PKT;

typedef union {
  uint8_t b[2048];
  struct {
    ETH_HEADER eth;
    TEST_PKT ip;
    uint8_t ip_b[1024];
  } t;
} PACKET;

PACKET buf;

#define NTOHS(x) (((uint16_t)x >> 8) | ((uint16_t)x << 8))
#define HTONS(x) (((uint16_t)x >> 8) | ((uint16_t)x << 8))

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
    _b16 = _a + (*buf++) + _c;
    _b = _b16;
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
    _b16 = _a + (*buf++) + _c;
    _b = _b16;
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

uint8_t test_hdr[] = { 0x45, 0x00, 0x05, 0xce, 0xae, 0x87, 0x00, 0x00, 0x40, 0x11, 0x2b, 0x36, 0xc0, 0xa8, 0x8d, 0x0a, 0xc0, 0xa8, 0x8d, 0x06 };

uint8_t pst;

void main(void)
{
  EUI48 mac_local;
  uint8_t i;
  uint16_t rx_hdr;
  uint16_t packet_size;
  uint8_t mc, bc, mac_match, crc32_error;
  uint8_t fill_byte = 0;
  uint32_t counter = 0;
  uint8_t slot = 31;
  uint32_t slot_address = 0x40000UL;
  uint8_t tmp8;
  uint16_t tmp16;
  uint32_t tmp32;
  static const uint16_t send_data_size = sizeof(ETH_HEADER) + sizeof(TEST_PKT);
  uint8_t *ptr;
  uint8_t col_toggle = 0;

  asm("sei");
  printf("Ethernet load test\n");

  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // RXPH 1, MCST off, BCST on, TXPH 1, NOCRC off, NOPROM off
  POKE(0xD6E5, 0x74);

  // enable debug, select tx state
  POKE(0xD6EF, 1);

  // Cursor off
  POKE(204, 0x80);

  // Read MAC address
  lcopy(0xFFD36E9, (unsigned long)&mac_local.b[0], 6);

  printf("Local\nMAC: %02x", mac_local.b[0]);
  for (i = 1; i < 6; i++)
    printf(":%02x", mac_local.b[i]);
  printf("\n");

  chks.u = 0;
  checksum(test_hdr, 20);
  printf("test checksum: %04x\n", chks.u);
  //while(1) continue;

  __asm__("sei");
  while(1) continue;
  while (1) {
    __asm__("php");
    __asm__("pla");
    __asm__("sta %v", pst);
    if (pst & 0x04) {

      printf("hallo");
      POKE(0xD020, PEEK(0xD020) + 1);
    }
    else {
      POKE(0xD021, 0);
      while (1) continue;
    }
    __asm__("php");
    __asm__("pla");
    __asm__("sta %v", pst);
    if (pst & 0x04) {
      POKE(0xD020, PEEK(0xD020) + 1);
    }
    else {
      POKE(0xD021, 0);
      while (1) continue;
    }
  }
#if 0

    // receive packet
    while (!(PEEK(0xD6E1) & 0x20))
      continue;


    ++slot;
    slot_address += 2048;

    if (slot == 32) {
        slot = 0;
        slot_address = 0x40000UL;
    }

    // switch to next slot in eth rx buffer
    POKE(0xD6E1, 0x01);
    POKE(0xD6E1, 0x03);

    //printf("slot %d addr $%lx\n", slot, slot_address);
    lcopy(ETH_RX_BUFFER, (uint32_t)&rx_hdr, 2);
    lcopy(ETH_RX_BUFFER + 2, slot_address, 2046);
    mc = (rx_hdr & 0x1000) ? 1 : 0;
    bc = (rx_hdr & 0x2000) ? 1 : 0;
    mac_match = (rx_hdr & 0x4000) ? 1 : 0;
    crc32_error = (rx_hdr & 0x8000) ? 1 : 0;
    packet_size = rx_hdr & 0x0fff;
    lcopy(slot_address, (uint32_t)buf.b, packet_size);

    if (bc && buf.t.eth.type == 0x0608) {
      printf("Ignoring ARP\n");
      continue;
    }

    if (crc32_error) {
      goto error;
    }
    if (!mac_match) {
      //printf("Ignoring packet for other MAC\n");
      continue;
    }

    // no ip packet?
    if (buf.t.eth.type != 0x0008)
      continue;

    chks.u = 0;
    checksum((uint8_t *)&buf.t.ip, 20);
    if (chks.u != 0xffff) {
      uint8_t *p = (uint8_t *)&buf.t.ip;
      printf("IP checksum error\n");
      for (tmp8 = 0; tmp8 < 20; ++tmp8) {
        printf("%02x ", *p);
        if (tmp8 == 9) printf("\n");
        ++p;
      }
      printf("\nChecksum: %04x\n", chks.u);
      goto error;
    }

    if (buf.t.ip.protocol == 1) {
      continue;
    }

    if (buf.t.ip.protocol != 17) {
      printf("Ignoring IP protocol %d\n", buf.t.ip.protocol);
      continue;
    }

    if (buf.t.ip.magic != 0x68438723) {
      printf("magic mismatch\n");
      goto error;
    }

    /*tmp16 = 1448;
    ptr = &buf.b[send_data_size];
    fill_byte = *ptr;
    while (--tmp16 != 0) {
      if (*ptr != fill_byte) {
        printf("fill error\n");
        goto error;
      }
      ++ptr;
    }*/

    // swap mac
    lcopy((uint32_t)&buf.t.eth.source, (uint32_t)&buf.t.eth.destination, sizeof(EUI48));
    lcopy((uint32_t)&mac_local, (uint32_t)&buf.t.eth.source, sizeof(EUI48));

    // swap ip
    tmp32 = buf.t.ip.source.d;
    buf.t.ip.source.d = buf.t.ip.destination.d;
    buf.t.ip.destination.d = tmp32;

    // swap udp port
    tmp16 = buf.t.ip.src_port;
    buf.t.ip.src_port = buf.t.ip.dst_port;
    buf.t.ip.dst_port = tmp16;

    tmp16 = sizeof(TEST_PKT);
    buf.t.ip.ip_length = HTONS(tmp16);
    tmp16 -= 20;
    buf.t.ip.udp_length = HTONS(tmp16);
    
    buf.t.ip.checksum_ip = 0;
    chks.u = 0;
    checksum((uint8_t *)&buf.t.ip, 20);
    buf.t.ip.checksum_ip = ~chks.u;
    buf.t.ip.checksum_udp = 0;

    //tmp8 = PEEK(0xD6EF);
    //printf("tx state %d", tmp8);
    tmp8 = PEEK(0xD6E0);
    while (tmp8 < 127) {
      POKE(0xD021, PEEK(0xD021) + 1);
      tmp8 = PEEK(0xD6E0);
    }

    if (send_data_size != 52) printf("%d\n", send_data_size);

    lcopy((uint32_t)&buf, ETH_TX_BUFFER, send_data_size);
    // Set packet length
    POKE(0xD6E2, send_data_size & 0xff);
    POKE(0xD6E3, send_data_size >> 8);    

    // transmit packet
    POKE(0xD6E4, 0x01);
    /*tmp8 = PEEK(0xD6EF);
    printf(" %d", tmp8);
    tmp8 = PEEK(0xD6EF);
    printf(" %d", tmp8);
    tmp8 = PEEK(0xD6EF);
    printf(" %d", tmp8);
    tmp8 = PEEK(0xD6EF);
    printf(" %d", tmp8);
    tmp8 = PEEK(0xD6EF);
    printf(" %d\n", tmp8);
    */
    POKE(0xD020, col_toggle);
    col_toggle = 1 - col_toggle;

    ++fill_byte;
    ++counter;
  }

error:
  printf("RX %d bytes m:%d b:%d m:%d c:%d\n", packet_size, mc, bc, mac_match, crc32_error);
  printf("Slot: %d at $%lx\n", slot, slot_address);
  printf("rx hdr: %04x\n", rx_hdr);
  while (1) continue;
#endif
}
