/*
  Issue #378: Q virtual instruction problems
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
unsigned short errors = 0;

unsigned char line_num = 5;

struct test {
  unsigned char rmw;
  unsigned char opcode;
  unsigned char instruction[5];
  unsigned long val1;
  unsigned long val2;
  unsigned long expected;
};

// clang-format off
struct test tests[]=
  {
   // ADC - Check carry chain works properly
   {0,0x6d,"ADC",0x12345678,0x00000000,0x12345678},
   {0,0x6d,"ADC",0x12345678,0x00000001,0x12345679},
   {0,0x6d,"ADC",0x12345678,0x00000100,0x12345778},
   {0,0x6d,"ADC",0x12345678,0x00000101,0x12345779},
   {0,0x6d,"ADC",0x12345678,0x000000FF,0x12345777},
   {0,0x6d,"ADC",0x12345678,0x0000FF00,0x12355578},
   {0,0x6d,"ADC",0x12345678,0x0DCBA989,0x20000001},
   // EOR 
   {0,0x4d,"EOR",0x12345678,0x12340000,0x00005678},
   {0,0x4d,"EOR",0x12345678,0x00005678,0x12340000},
   // AND 
   {0,0x2d,"AND",0x12345678,0x0000FFFF,0x00005678},
   {0,0x2d,"AND",0x12345678,0xFFFF0000,0x12340000},
   // ORA 
   {0,0x2d,"AND",0x12340000,0x00005678,0x00000000},
   {0,0x2d,"AND",0x12345600,0x00005678,0x00005600},
   // INC
   {1,0xEE,"INC",0,0x12345678,0x12345679},
   {1,0xEE,"INC",0,0x00000000,0x00000001},
   {1,0xEE,"INC",0,0x00FFFFFF,0x01000000},
   // DEC
   {1,0xCE,"DEC",0,0x12345678,0x12345677},
   {1,0xCE,"DEC",0,0x00000000,0xFFFFFFFF},
   {1,0xCE,"DEC",0,0x00FFFFFF,0x00FFFFFE},
   
   {0,0x00,"END",0,0,0}
  };
// clang-format on

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

// Use tape buffer for code snippets
unsigned char* code_buf = (unsigned char*)0x340;

/* Setup our code snippet:
   SEI
   ; LDQ $0380
   NEG
   NEG
   LDA $0380
   ; Do some Q instruction
   CLC
   NEG
   NEG
   XXX $0384
   ; Store result back
   ; STQ $0388
   NEG
   NEG
   STA $0388
   ; And store back safely as well
   STA $038C
   STX $038D
   STY $038E
   STZ $038F
   CLI
   RTS
 */
unsigned char code_snippet[31] = { 0x78, 0x42, 0x42, 0xAD, 0x80, 0x03, 0x18, 0x42, 0x42, 0x6D, 0x84, 0x03, 0x42, 0x42, 0x8d,
  0x88, 0x03, 0x8d, 0x8c, 0x03, 0x8e, 0x8d, 0x03, 0x8c, 0x8e, 0x03, 0x9c, 0x8f, 0x03, 0x60, 0x00 };
#define INSTRUCTION_OFFSET 9

unsigned long load_addr;

unsigned char line_dmalist[256];

unsigned long result_q, expected;

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

  print_text(0, 0, 1, "Issue #378: Q 32-bit virtual reg");
  print_text(0, 1, 1, "Many instructions give incorrect");
  print_text(0, 2, 1, "results due to timing closure");
  print_text(0, 3, 1, "problems.");
  print_text(0, 4, 7, "All tests should report OK");

  // Pre-install code snippet
  lcopy((long)code_snippet, (long)code_buf, 31);

  // Run each test
  for (i = 0; tests[i].opcode; i++) {
    expected = tests[i].expected;
    // Setup input values
    *(unsigned long*)0x380 = tests[i].val1;
    *(unsigned long*)0x384 = tests[i].val2;

    code_buf[INSTRUCTION_OFFSET] = tests[i].opcode;
    __asm__("jsr $0340");
    if (tests[i].rmw)
      result_q = *(unsigned long*)0x384;
    else
      result_q = *(unsigned long*)0x388;
    if (result_q != expected) {
      snprintf(msg, 64, "FAIL:#%d:$%02X:%s", (int)i, (int)tests[i].opcode, tests[i].instruction);
      print_text(0, line_num++, 2, msg);
      snprintf(msg, 64, "     Expect=$%08lx, Saw=$%08lx", expected, result_q);
      print_text(0, line_num++, 2, msg);
      errors++;
      if (line_num >= 23) {
        print_text(0, line_num, 8, "TOO MANY ERRORS: Aborting");
        while (1)
          continue;
      }
    }
  }
  snprintf(msg, 64, "%d tests complete, with %d errors.", i, errors);
  print_text(0, 24, 7, msg);
}
