#include <stdio.h>
#ifndef NATIVE_TEST
#include <memory.h>
#include <targets.h>
#include <time.h>

struct m65_tm tm;
#else
#include <strings.h>
#include <string.h>
#include <pcap/pcap.h>
#include <stdlib.h>

pcap_t *p = NULL;

#define POKE(X, Y)
// Make PEEK(0xD6E1) always indicate that a packet is always ready to be received
#define PEEK(X) 0x20

#define RX_DEBUG

void lcopy(long src, long dst, int count)
{
  if (src == 0xFFDE800L) {
    // Read next packet from pcap file
    struct pcap_pkthdr h;

    char *packet = pcap_next(p, &h);
    if (!packet)
      exit(0);
    ((char *)dst)[0] = h.len;
    ((char *)dst)[1] = h.len >> 8;
    bcopy(packet, (void *)dst + 2, count - 1);
  }
  else {
    bcopy((void *)src, (void *)dst, count);
  }
}

void lpoke(long addr, int val)
{
  // Ignore POKEs to upper memory
}

void lfill(long addr, int val, int count)
{
  // Ignore POKEs to upper memory
}

int lpeek(long addr)
{
  return 0;
}
#endif

void m65_io_enable(void)
{
  // Gate C65 IO enable
  POKE(0xd02fU, 0x47);
  POKE(0xd02fU, 0x53);
  // Force to full speed
  POKE(0, 65);
}

void wait_10ms(void)
{
  // 16 x ~64usec raster lines = ~1ms
  int c = 160;
  unsigned char b;
  while (c--) {
    b = PEEK(0xD012U);
    while (b == PEEK(0xD012U))
      continue;
  }
}

unsigned short mdio_read_register(unsigned char addr, unsigned char reg)
{
  unsigned short result = 0, r1 = 1, r2 = 2, r3 = 3;

  // Require several consecutive identical reads to debounce
  while (result != r1 || r1 != r2 || r2 != r3) {
    r3 = r2;
    r2 = r1;
    r1 = result;

    // Use new MIIM interface
    POKE(0xD6e6L, (reg & 0x1f) + ((addr & 7) << 5));
    // Reading takes a little while...
    for (result = 0; result < 32000; result++)
      continue;
    result = PEEK(0xD6E7L);
    result |= PEEK(0xD6E8L) << 8;
  }

  return result;
}

unsigned char free_buffers = 0;
unsigned char last_free_buffers = 0;

unsigned char frame_buffer[2048];
unsigned char frame_bytes[2048 / 4];
char msg[160 + 1];

unsigned short i;

void graphics_clear_screen(void)
{
  lfill(0x40000L, 0, 32768L);
  lfill(0x48000L, 0, 32768L);
}

void graphics_clear_double_buffer(void)
{
  lfill(0x50000L, 0, 32768L);
  lfill(0x58000L, 0, 32768L);
}

void h640_text_mode(void)
{
  // lower case
  POKE(0xD018, 0x16);

  // Normal text mode
  POKE(0xD054, 0x00);
  // H640, V400, fast CPU, extended attributes
  POKE(0xD031, 0xE8);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016, 0xC9);
  // 80 chars per line logical screen layout
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  // Draw 80 chars per row
  POKE(0xD05E, 80);
  // Put 4KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  // 50 lines of text
  POKE(0xD07B, 50);

  lfill(0xc000, 0x20, 4000);
  // Clear colour RAM, while setting all chars to 4-bits per pixel
  lfill(0xff80000L, 0x0E, 4000);
}

unsigned short pixel_addr;
unsigned char pixel_temp;
void plot_pixel(unsigned short x, unsigned char y, unsigned char colour)
{
  pixel_addr = ((x & 0xf) >> 1) + 64 * 25 * (x >> 4);
  pixel_addr += y << 3;
  pixel_temp = lpeek(0x50000L + pixel_addr);
  if (x & 1) {
    pixel_temp &= 0x0f;
    pixel_temp |= colour << 4;
  }
  else {
    pixel_temp &= 0xf0;
    pixel_temp |= colour & 0xf;
  }
  lpoke(0x50000L + pixel_addr, pixel_temp);
}

unsigned char char_code;
void print_text(unsigned char x, unsigned char y, unsigned char colour, char *msg)
{
  pixel_addr = 0xC000 + x * 2 + y * 80;
  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    POKE(pixel_addr + 0, char_code);
    POKE(pixel_addr + 1, 0);
    lpoke(0xff80000 - 0xc000 + pixel_addr + 0, 0x00);
    lpoke(0xff80000 - 0xc000 + pixel_addr + 1, colour);
    msg++;
    pixel_addr += 2;
  }
}

void print_text80(unsigned char x, unsigned char y, unsigned char colour, char *msg)
{
  pixel_addr = 0xC000 + x + y * 80;
  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    else if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    else if (*msg >= 0x60 && *msg <= 0x7A)
      char_code = *msg - 0x20;
    POKE(pixel_addr + 0, char_code);
    lpoke(0xff80000L - 0xc000 + pixel_addr, colour);
    msg++;
    pixel_addr += 1;
  }
}

// this should print the decimal version of an arbitrary number of bytes
// probably already something in c stdlib or this repo but i couldnt find anything
int to_decimal(unsigned char *src, int bytes)
{
  int out = 0;
  int i;
  int x;
  for (i = 0; i < bytes; i++) {
    x = src[bytes - i - 1];
    out |= (x << (8 * i));
  }
  return out;
}

unsigned short frame_counter = 0;
void send_frame(void)
{
  unsigned char i;
  // len=100
  POKE(0xd6e2, 100);
  POKE(0xd6e3, 0);
  for (i = 0; i < 100; i++) {
    lpoke(0xffde800L + i, i);
  }

  // TX
  POKE(0xd6e4, 0x01);

  frame_counter++;
}

void do_dma(void);

void lcopy_fixed_src(long source_address, long destination_address, unsigned int count)
{
  dmalist.option_0b = 0x0b;
  dmalist.option_80 = 0x80;
  dmalist.source_mb = source_address >> 20;
  dmalist.option_81 = 0x81;
  dmalist.dest_mb = (destination_address >> 20);
  dmalist.end_of_options = 0x00;
  dmalist.sub_cmd = 0x00;

  dmalist.command = 0x00; // copy
  dmalist.count = count;
  dmalist.sub_cmd = 0x02; // hold source address F018B

  dmalist.source_addr = source_address & 0xffff;
  dmalist.source_bank = (source_address >> 16) & 0x0f;
  //  dmalist.source_bank|=0x10; // hold source address

  dmalist.dest_addr = destination_address & 0xffff;
  dmalist.dest_bank = (destination_address >> 16) & 0x0f;

  do_dma();
  return;
}

unsigned short mdio0 = 0, mdio1 = 0, last_mdio0 = 0, last_mdio1 = 0, frame_count = 0;
unsigned char phy, last_frame_num = 0, show_hex = 1, last_d6ef = 0;

void main(void)
{
  unsigned short len;
  // since the start of the header varies depending on whether the IPv4 options field is present
  unsigned short start_of_protocol_header;
  // looping
  int i;
  unsigned short x = 0;

  POKE(0x0286, 0x01);

  m65_io_enable();

  // Disable RX debug mode for eth controller
  POKE(0xD6E4, 0xd0);

  // Clear reset on ethernet controller
  POKE(0xD6E0, 0x00);
  POKE(0xD6E0, 0x03);

  // No promiscuous mode, ignore corrupt packets, accept broadcast and multicast
  // default RX and TX phase adjust.
  POKE(0xD6E5, 0x30);

  // Reset ethernet
  POKE(0xd6e0, 0);
  POKE(0xd6e0, 3);
  POKE(0xd6e1, 3);
  POKE(0xd6e1, 0);

  // Set to 10mbit mode
  POKE(0xD6E4, 0x10); // 10mbit
#ifdef RX_DEBUG
  // Keep full sampling rate, so that we can see fine structure of 10mbit
  // frames (otherwise the decimation of the input bits happens first)
  POKE(0xD6E4, 0x11); // 100mbit
#endif

  // And force MDIO renegotiation to 10mbit

  // 1. Advertise 10mbit only
  POKE(0xD6E6, (PEEK(0xD6E6) & 0xe0) + 4); // select register 4
  wait_10ms();
  printf("MDIO reg 4 = $%02x%02x\n", PEEK(0xD6E8), PEEK(0xD6E7));
  POKE(0xD6E7, 0x61); // 10mbit half and full duplex supported
  POKE(0xD6E8, 0x00); // no 100mbit supported, no next page support
  wait_10ms();
  printf("MDIO reg 4 = $%02x%02x\n", PEEK(0xD6E8), PEEK(0xD6E7));

  // 2. Request re-negotation
  POKE(0xD6E6, (PEEK(0xD6E6) & 0xe0) + 0); // select register 0
  wait_10ms();
  printf("MDIO reg 0 = $%02x%02x\n", PEEK(0xD6E8), PEEK(0xD6E7));
  POKE(0xD6E7, 0x00); //
  POKE(0xD6E8, 0x13); // 10mbs, and force re-negotiation
  wait_10ms();
  printf("MDIO reg 0 = $%02x%02x\n", PEEK(0xD6E8), PEEK(0xD6E7));

  // Ack RX buffers of eth controller until all buffers are free
  while (PEEK(0xD6E1) & 0x20) {
    POKE(0xD6E1, 1);
    POKE(0xD6E1, 3);

    // Show state of first 4 buffers
    POKE(0x0400, PEEK(0xD6E3) >> 4);
    // Count of free buffers
    POKE(0x0401, '0' + (PEEK(0xD6E1) >> 1) & 3);
    // CPU and ETH RX buffer number low bits
    POKE(0x0402, '0' + (PEEK(0xD6EF) >> 0) & 3);
    POKE(0x0403, '0' + (PEEK(0xD6EF) >> 2) & 3);

    // So that we can see if we are running or not
    POKE(0x0427, PEEK(0x0427) + 1);
  }

  //  while(1) continue;

  h640_text_mode();

  // Accept broadcast and multicast frames, and enable promiscuous mode
  POKE(0xD6E5, 0x30);
  // XXX Disable RX CRC check for now
  POKE(0xD6E5, 0x31);

  // Find PHY for MDIO access, and select register 1 that has the signals we really care about
  for (phy = 0; phy != 0x20; phy++)
    if (mdio_read_register(phy, 1) != 0xffff)
      break;
  if (phy == 0x20) {
    //    println_text80(7, "WARNING: Could not find PHY address.");
  }

  while (1) {

    if (PEEK(0xD610)) {
      switch (PEEK(0xd610)) {
      case 0x20:
      case 0x48:
      case 0x68:
        show_hex ^= 1;
        break;
      case 0x54:
      case 0x74:
        // Do a TX test of a frame
        // (handy if using a loop-back plug)
        send_frame();
        break;
      }
      POKE(0xD610, 0);
    }

#if 0
    if (PEEK(0xD6EF) != last_d6ef) {
      last_d6ef = PEEK(0xD6EF);
      snprintf(msg, 160, "#$%02x : $D6EF change: CPU buf=%d, ETH buf=%d, toggles=%d, rotates=%d, free=%d, b=%x", PEEK(0xD7FA),
	       (PEEK(0xD6EF) >> 0) & 3, (PEEK(0xD6EF) >> 2) & 3, (PEEK(0xD6EF) >> 6) & 3, (PEEK(0xD6EF) >> 4) & 0x3,
	       (PEEK(0xD6E1)&0x6)>>1,
	       PEEK(0xD6E3)>>4); 
      //      while(!PEEK(0xD610)) continue; POKE(0xD610,0);
    }
#endif

    // Check for new packets
    if (!(PEEK(0xD6E1) & 0x20)) {
#ifdef RX_DEBUG
      // Prime for next debug RX frame
      POKE(0xD6e4, 0xde);
#endif
#if 0
      //      POKE(0xD020,PEEK(0xD020)+1);
      x=0xf00;
      while(x==0xf00) {
	lcopy_fixed_src(0xffd36e0,0xc000,0xf00);
	for(x=0;x<0xf00;x++) if (PEEK(0xc000+x)!=0x81) break;
      }
      while(!PEEK(0xD610)); POKE(0xD610,0);
#endif
      POKE(0xc000, PEEK(0xc000) + 1);
    }
    else {
      char is_broadcast, is_icmp, is_ping;

      //	POKE(0xD6e4,0xd0);

      // Ethernet frame received
      lcopy(0xFFDE800L, (long)frame_buffer, 0x0800);

      if (frame_buffer[2] == 0x00)
        goto empty;

      POKE(0xD020, 1);
      POKE(0xD020, 0);

// 43 = start of eth dest addr
#define ETH_DST_OFS 43

      // Decode frame
      for (i = 0; i < (2048 / 4); i++) {
        unsigned char v = 0;

        v = (frame_buffer[ETH_DST_OFS + i * 4 + 0] & 0xc0) >> 6;
        v |= (frame_buffer[ETH_DST_OFS + i * 4 + 1] & 0xc0) >> 4;
        v |= (frame_buffer[ETH_DST_OFS + i * 4 + 2] & 0xc0) >> 2;
        v |= (frame_buffer[ETH_DST_OFS + i * 4 + 3] & 0xc0) >> 0;

        frame_bytes[i] = v;
      }

      is_broadcast = 1;
      for (i = 0; i < 6; i++)
        if (frame_bytes[i] != 0xff) {
          is_broadcast = 0;
          break;
        }

      is_icmp = 0;
      is_ping = 0;
      if (frame_bytes[16 + 7] == 0x01) {
        is_icmp = 1;
        if (frame_bytes[16 + 18] == 0x08) {
          is_ping = 1;
        }
      }

#define SAMPLES_PER_BYTE 4
#define START_OFS ETH_DST_OFS + (60 * SAMPLES_PER_BYTE)

      //	if (is_broadcast&&is_ping)
      // if (is_broadcast)
      if (1) {
        lcopy(frame_buffer, 0xc000, 80);
        lfill(0xff80000, 1, 80);
        lcopy(frame_buffer, 0xc800, 0x800);

        // Show individual bits and count differences
        for (i = 0; i < 80; i++) {
          char v = ((frame_buffer[START_OFS + i / 4] >> ((0 ^ (i & 3)) << 1)) & 0x02) ? 0xa3 : 0x20;
          if (PEEK(0xc000 + 3 * 80 + i) != v)
            POKE(0xc000 + 7 * 80 + i, PEEK(0xc000 + 7 * 80 + i) + 1);
          POKE(0xc000 + 3 * 80 + i, v);
          v = ((frame_buffer[START_OFS + i / 4] >> ((0 ^ (i & 3)) << 1)) & 0x01) ? 0xa3 : 0x20;
          if (PEEK(0xc000 + 4 * 80 + i) != v)
            POKE(0xc000 + 8 * 80 + i, PEEK(0xc000 + 8 * 80 + i) + 1);
          POKE(0xc000 + 4 * 80 + i, v);
          POKE(0xc000 + 5 * 80 + i, '0' + i / 4);
        }

        // Show hex decode
        for (i = 0; i < 100; i++) {
          unsigned char v = frame_bytes[i];
          unsigned char hex;

          hex = (v & 0xf0) >> 4;
          if (hex < 10)
            hex += '0';
          else
            hex -= 9;
          POKE(0xc000 + (10 + i / 16) * 80 + ((i & 0xf) << 2), hex);
          hex = (v & 0xf);
          if (hex < 10)
            hex += '0';
          else
            hex -= 9;
          POKE(0xc000 + (10 + i / 16) * 80 + 1 + ((i & 0xf) << 2), hex);
        }
      }
    empty:
      // Free the buffer, ready for the next frame
      POKE(0xD6E1, 3);
      POKE(0xD6E1, 1);

      // Clear RX debug status, if it was set
      POKE(0xD6e4, 0xd0);
    }
  }
}
