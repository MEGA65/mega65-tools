/*
  Issue #342: Fix use of alternate palette
*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>
#include <random.h>

char msg[64 + 1];

unsigned short i, j;
unsigned char a, b, c, d;

unsigned short abs2(signed short s)
{
  if (s > 0)
    return s;
  return -s;
}

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
  // H640, fast CPU, extended attributes
  POKE(0xD031, 0xE0);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016, 0xC9);
  // 640x200 16bits per char, 16 pixels wide per char
  // = 640/16 x 16 bits = 80 bytes per row
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  // Draw 80 chars per row
  POKE(0xD05E, 80);
  // Put 2KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  lfill(0xc000, 0x20, 2000);
  // Clear colour RAM
  lfill(0xff80000L, 0x0E, 2000);
}

void graphics_mode(void)
{
  // 16-bit text mode, full-colour text for high chars
  POKE(0xD054, 0x05);
  // H320, fast CPU
  POKE(0xD031, 0x40);
  // 320x200 per char, 16 pixels wide per char
  // = 320/8 x 16 bits = 80 bytes per row
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  // Draw 40 chars per row
  POKE(0xD05E, 40);
  // Put 2KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  // Layout screen so that graphics data comes from $40000 -- $4FFFF

  i = 0x40000 / 0x40;
  for (a = 0; a < 40; a++)
    for (b = 0; b < 25; b++) {
      POKE(0xC000 + b * 80 + a * 2 + 0, i & 0xff);
      POKE(0xC000 + b * 80 + a * 2 + 1, i >> 8);

      i++;
    }

  // Clear colour RAM
  for (i = 0; i < 2000; i++) {
    lpoke(0xff80000L + 0 + i, 0x00);
    lpoke(0xff80000L + 1 + i, 0x00);
  }

  POKE(0xD020, 0);
  POKE(0xD021, 0);
}

void print_text(unsigned char x, unsigned char y, unsigned char colour, char* msg);

unsigned short pixel_addr;
unsigned char pixel_temp;
void plot_pixel(unsigned short x, unsigned char y, unsigned char colour)
{
  pixel_addr = (x & 0x7) + 64 * 25 * (x >> 3);
  pixel_addr += y << 3;

  lpoke(0x50000L + pixel_addr, colour);
}

void plot_pixel_direct(unsigned short x, unsigned char y, unsigned char colour)
{
  pixel_addr = (x & 0x7) + 64 * 25 * (x >> 3);
  pixel_addr += y << 3;

  lpoke(0x40000L + pixel_addr, colour);
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

void activate_double_buffer(void)
{
  lcopy(0x50000, 0x40000, 0x8000);
  lcopy(0x58000, 0x48000, 0x8000);
}

unsigned char fd;
int count;
unsigned char buffer[512];

unsigned long load_addr;

unsigned char line_dmalist[256];

unsigned char ofs;
unsigned char slope_ofs, line_mode_ofs, cmd_ofs, count_ofs;
unsigned char src_ofs, dst_ofs;

int Axx, Axy, Axz;
int Ayx, Ayy, Ayz;
int Azx, Azy, Azz;

void main(void)
{
  unsigned char playing = 0;

  asm("sei");

  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // Setup first palette (in palette bank 3, which we will also use as the chargen palette)
  // with grey gradient, and select palette bank 0 for the alternate palette.
  // top 2 bits select the memory mapped palette, so here we are setting up palette bank 3.
  POKE(0xD070, 0xf0);
  for (i = 16; i < 256; i++) {
    POKE(0xD100 + i, (i >> 4) + (i << 4));
    POKE(0xD200 + i, (i >> 4) + (i << 4));
    POKE(0xD300 + i, (i >> 4) + (i << 4));
  }
  // And setup the alternate palette in palette bank 0 with blue to red transition
  POKE(0xD070, 0x30);
  for (i = 16; i < 256; i++) {
    POKE(0xD100 + i, (i >> 4) + (i << 4));
    POKE(0xD200 + i, 0);
    POKE(0xD300 + i, 0xff);
  }

  while (PEEK(0xD610))
    POKE(0xD610, 0);

  POKE(0xD020, 0);
  POKE(0xD021, 0);

  // Stop all DMA audio first
  POKE(0xD720, 0);
  POKE(0xD730, 0);
  POKE(0xD740, 0);
  POKE(0xD750, 0);

  graphics_mode();
  graphics_clear_screen();

  graphics_clear_double_buffer();
  activate_double_buffer();

  // Set up common structure of the DMA list
  ofs = 0;
  // Screen layout is in vertical stripes, so we need only to setup the
  // X offset step.  64x25 =
  line_dmalist[ofs++] = 0x87;
  line_dmalist[ofs++] = (1600 - 8) & 0xff;
  line_dmalist[ofs++] = 0x88;
  line_dmalist[ofs++] = (1600 - 8) >> 8;
  line_dmalist[ofs++] = 0x8b;
  slope_ofs = ofs++; // remember where we have to put the slope in
  line_dmalist[ofs++] = 0x8c;
  ofs++;
  line_dmalist[ofs++] = 0x8f;
  line_mode_ofs = ofs++;
  line_dmalist[ofs++] = 0x0a; // F018A list format
  line_dmalist[ofs++] = 0x00; // end of options
  cmd_ofs = ofs++;            // command byte
  count_ofs = ofs;
  ofs += 2;
  src_ofs = ofs;
  ofs += 3;
  dst_ofs = ofs;
  ofs += 3;
  line_dmalist[ofs++] = 0x00; // modulo
  line_dmalist[ofs++] = 0x00;

  print_text(0, 0, 1, "Issue #342: Use of alternate palette via");
  print_text(0, 1, 1, "BOLD + REVERSE attributes together.");
  print_text(0, 2, 7, "Upper colour bar should have 16 C64");
  print_text(0, 3, 7, "  colours, then grey gradient.");
  print_text(0, 4, 7, "Lower colour bar should have 16 C64");
  print_text(0, 5, 7, "  colours, then blue to purple.");

  print_text(0, 96 / 8, 15, "   Normal:");
  print_text(0, 128 / 8, 15, "Alternate:");

  // enable extended attributes (with hot regs off)
  POKE(0xD05D, PEEK(0xD05D) & 0x7f);
  POKE(0xD031, PEEK(0xD031) + 0x20);

  // Setup some identical gradients
  for (i = 0; i < 256; i++) {
    for (j = 0; j < 8; j++)
      plot_pixel_direct(96 + i, 96 + j, i);

    for (j = 0; j < 8; j++)
      plot_pixel_direct(96 + i, 128 + j, i);
  }

  // Now set alternate palette flag for the second lot
  load_addr = 0xff80000L + (96 / 8) * 2 + (128 / 8 * (40 * 2));
  for (i = 0; i < 32; i++) {
    // Set reverse and bold flags in 2nd byte of colour RAM
    lpoke(load_addr + i * 2 + 1, 0x60);
  }
}
