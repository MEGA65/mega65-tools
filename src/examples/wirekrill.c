#include <stdio.h>
#include <memory.h>
#include <targets.h>
#include <time.h>

struct m65_tm tm;

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
  unsigned char i;

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

#define TEST(reg, v, bit, desc)                                                                                             \
  {                                                                                                                         \
    if (v & (1 << bit)) {                                                                                                   \
      snprintf(msg, 160, "   MDIO $%x.%x : %s", reg, bit, desc);                                                            \
      println_text80(8, msg);                                                                                               \
    }                                                                                                                       \
  }

unsigned char free_buffers = 0;
unsigned char last_free_buffers = 0;

unsigned char frame_buffer[1024];
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
void print_text(unsigned char x, unsigned char y, unsigned char colour, char* msg)
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

void print_text80(unsigned char x, unsigned char y, unsigned char colour, char* msg)
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

unsigned char text_row = 0;

void clear_text80(void)
{
  lfill(0xc000, 4000, 0x20);
  lfill(0xff80000L, 4000, 1);
  text_row = 0;
}

void println_text80(unsigned char colour, char* msg)
{
  if (text_row == 49) {
    lcopy(0xc000 + 80, 0xc000, 4000 - 80);
    lcopy(0xff80000 + 80, 0xff80000, 4000 - 80);
    lfill(0xc000 + 4000 - 80, 0x20, 80);
    lfill(0xff80000 + 4000 - 80, 0x01, 80);
  }
  print_text80(0, text_row, colour, msg);
  if (text_row < 49)
    text_row++;
}

unsigned char safe_char(unsigned char in)
{
  if (!in)
    return 0x2e;

  return in;
}

unsigned short mdio0 = 0, mdio1 = 0, last_mdio0 = 0, last_mdio1 = 0, frame_count = 0;
unsigned char phy, last_frame_num = 0, show_hex = 0, last_d6ef = 0;

void main(void)
{
  unsigned short len;

  POKE(0x0286, 0x01);

  m65_io_enable();

  h640_text_mode();

  clear_text80();

  println_text80(1, "WireKrill 0.0.1 Network Analyser.");
  println_text80(1, "(C) Paul Gardner-Stephen, 2020-2021.");

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

  // Accept broadcast and multicast frames, and enable promiscuous mode
  POKE(0xD6E5, 0x30);

  // Find PHY for MDIO access, and select register 1 that has the signals we really care about
  for (phy = 0; phy != 0x20; phy++)
    if (mdio_read_register(phy, 1) != 0xffff)
      break;
  if (phy == 0x20) {
    println_text80(7, "WARNING: Could not find PHY address.");
  }
  snprintf(msg, 160, "Network PHY is at MDIO address $%x", phy);
  println_text80(1, msg);

  while (1) {

    if (PEEK(0xD6EF) != last_d6ef) {
      last_d6ef = PEEK(0xD6EF);
      snprintf(msg, 160, "#$%02x : $D6EF change: CPU buffer=%d, ETH buffer=%d, toggles=%d, rotates=%d", PEEK(0xD7FA),
          (PEEK(0xD6EF) >> 0) & 3, (PEEK(0xD6EF) >> 2) & 3, (PEEK(0xD6EF) >> 6) & 3, (PEEK(0xD6EF) >> 4) & 0x3);
      println_text80(1, msg);
    }

    // Check for new packets
    if (PEEK(0xD6E1) & 0x20) // Packet received
    {
      // Pop a frame from the buffer list
      POKE(0xD6E1, 0x01);
      POKE(0xD6E1, 0x03);
      POKE(0xD6E1, 0x01);
      lcopy(0xFFDE800L, (long)frame_buffer, 0x0800);
      lfill((long)msg, 0, 160);
      len = frame_buffer[0] + ((frame_buffer[1] & 0xf) << 8);
      snprintf(msg, 160, "#$%02x : Ethernet Frame #%d", PEEK(0xD7FA), ++frame_count);
      println_text80(1, msg);

      snprintf(msg, 160, "  %02x:%02x:%02x:%02x:%02x:%02x > %02x:%02x:%02x:%02x:%02x:%02x : len=%d($%x), rxerr=%c",
          frame_buffer[8], frame_buffer[9], frame_buffer[10], frame_buffer[11], frame_buffer[12], frame_buffer[13],
          frame_buffer[2], frame_buffer[3], frame_buffer[4], frame_buffer[5], frame_buffer[6], frame_buffer[7], len, len,
          (frame_buffer[1] & 0x80) ? 'Y' : 'N');
      println_text80(13, msg);
      show_hex = 1;
      if (frame_buffer[16] == 0x45) {
        // IPv4
        snprintf(msg, 160, "  IPv4: %d.%d.%d.%d -> %d.%d.%d.%d", frame_buffer[28 + 0], frame_buffer[28 + 1],
            frame_buffer[28 + 2], frame_buffer[28 + 3], frame_buffer[32 + 0], frame_buffer[32 + 1], frame_buffer[32 + 2],
            frame_buffer[32 + 3]);
        println_text80(13, msg);
        if (frame_buffer[16 + 9] == 0x01) {
          // ICMP
          if (frame_buffer[16 + 20] == 0x08) {
            // PING
            snprintf(msg, 160, "  ICMP: Echo request, id=%d, seq=%d",
                frame_buffer[16 + 20 + 5] + (frame_buffer[16 + 20 + 4] << 8),
                frame_buffer[16 + 20 + 7] + (frame_buffer[16 + 20 + 6] << 8));
            println_text80(13, msg);
            //	    show_hex=0;
          }
        }
      }

      if (show_hex) {
        for (i = 0; i < 256 && i < len; i += 16) {
          lfill((long)msg, 0, 160);
          snprintf(msg, 160,
              "       %04x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x  "
              "%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
              i, frame_buffer[14 + i], frame_buffer[15 + i], frame_buffer[16 + i], frame_buffer[17 + i],
              frame_buffer[18 + i], frame_buffer[19 + i], frame_buffer[20 + i], frame_buffer[21 + i], frame_buffer[22 + i],
              frame_buffer[23 + i], frame_buffer[24 + i], frame_buffer[25 + i], frame_buffer[26 + i], frame_buffer[27 + i],
              frame_buffer[28 + i], frame_buffer[29 + i],

              safe_char(frame_buffer[14 + i]), safe_char(frame_buffer[15 + i]), safe_char(frame_buffer[16 + i]),
              safe_char(frame_buffer[17 + i]), safe_char(frame_buffer[18 + i]), safe_char(frame_buffer[19 + i]),
              safe_char(frame_buffer[20 + i]), safe_char(frame_buffer[21 + i]), safe_char(frame_buffer[22 + i]),
              safe_char(frame_buffer[23 + i]), safe_char(frame_buffer[24 + i]), safe_char(frame_buffer[25 + i]),
              safe_char(frame_buffer[26 + i]), safe_char(frame_buffer[27 + i]), safe_char(frame_buffer[28 + i]),
              safe_char(frame_buffer[29 + i]));
          println_text80(14, msg);
        }
      }
    }

    // Periodically ask for update to MDIO register read
    if (PEEK(0xD7FA) != last_frame_num) {
      last_frame_num = PEEK(0xD7FA);
      POKE(0xD6E6L, 1);
    }

    // Report MDIO changes
    mdio1 = PEEK(0xD6E7L);
    mdio1 |= PEEK(0xD6E8L) << 8;
    // Work around bug where MDIO register 1 some times reads shifted by one bit
    if ((mdio1 & 0xf000) == 0xf000)
      mdio1 = last_mdio1;
    if ((mdio1 != last_mdio1)) {

      //      TEST(1, mdio1, 15, "T4 capable");
      //      TEST(1, mdio1, 14, "100TX FD capable");
      //      TEST(1, mdio1, 13, "100TX HD capable");
      //      TEST(1, mdio1, 12, "10T FD capable");
      //      TEST(1, mdio1, 11, "10T HD capable");
      //      TEST(1, mdio1, 6, "Preamble suppression");
      if ((mdio1 ^ last_mdio1) & (1 << 5)) {
        if (mdio1 & (1 << 5))
          println_text80(5, "PHY: Auto-negotiation completee");
        else
          println_text80(7, "PHY: Auto-negotiation in progress");
      }
      if ((mdio1 ^ last_mdio1) & (1 << 2)) {
        if (mdio1 & (1 << 2))
          println_text80(5, "PHY: Link is up");
        else
          println_text80(2, "PHY: Link DOWN");
      }

      //      TEST(1, mdio1, 5, "Auto-neg complete");
      //      TEST(1, mdio1, 4, "Remote fault");
      //      TEST(1, mdio1, 3, "Can auto-neg");

      //      TEST(1, mdio1, 2, "Link is up");
      //      TEST(1, mdio1, 1, "Jabber detected");
      //      TEST(1, mdio1, 0, "Supports ex-cap regs");

      last_mdio0 = mdio0;
      last_mdio1 = mdio1;
    }
  }
}
