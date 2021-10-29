/*
  Issue #458: base page indirect z index addressing messes up

  In the manual all bp-ind-z-idx addressed Q opcodes should ignore Z.
  In VHDL this is only true for STQ.
  
  While it can make sense that certain Q opcodes that do not operate on
  the virtual Q register could use Z, at least the opcodes:
  ADQ, ANDQ, CMPQ, EORQ, ORQ, SBCQ
  must not add Z, as they do operate on Q.

  This test case checks this behavior.
*/
#define ISSUE_NUM 458
#define ISSUE_NAME "q bp-ind-z-idx 0 or not"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>
#include <random.h>
#include <tests.h>

struct test {
  unsigned char rmw;
  unsigned char opcode1;
  unsigned char opcode2;
  unsigned char message[12]; // 11 max!
  unsigned long val1;
  unsigned long val2;
  unsigned long val3;
  unsigned long expected;
  unsigned char flag_mask;
  unsigned char flag_val;
};

struct test tests[]=
  {
    // to test: LDQ, ADCQ, ANDQ, CMPQ, EORQ, ORQ, SBCQ, STQ
    // LDQ - check if right value is loaded
    // 001 - load via 16b pointer value with offset of 2
    {0, 0xb2, 0x00, "ldq ($nn),z", 0x02345678, 0x12345678, 0xffffffff, 0xffff1234, 0x00, 0x00},
    // 002 - load via 32b pointer value with ofsset of 2
    {0, 0xb2, 0x01, "ldq [$nn],z", 0x02345678, 0x12345678, 0xffffffff, 0xffff1234, 0x00, 0x00},

    // 003 - 02345678 ANDQ ($nn) aa5555aa = 02145428
    {0, 0x32, 0x00, "andq ($nn)", 0x02345678, 0xaa5555aa, 0x00000000, 0x02145428, 0x00, 0x00},
    // 004 - 02345678 ANDQ ($nn) aa5555aa = 02145428
    {0, 0x32, 0x01, "andq [$nn]", 0x02345678, 0xaa5555aa, 0x00000000, 0x02145428, 0x00, 0x00},

    // 005 - 02345678 ORQ ($nn) aa55aa55 = aa7557fa
    {0, 0x12, 0x00, "orq ($nn)",  0x02345678, 0xaa5555aa, 0xffffffff, 0xaa7557fa, 0x00, 0x00},
    // 006 - 02345678 ORQ ($nn) aa55aa55 = aa7557fa
    {0, 0x12, 0x01, "orq [$nn]",  0x02345678, 0xaa5555aa, 0xffffffff, 0xaa7557fa, 0x00, 0x00},

    // 007 - 02345678 EORQ ($nn) aa55aa55 = a86103d2
    {0, 0x52, 0x00, "eorq ($nn)", 0x02345678, 0xaa5555aa, 0xffffffff, 0xa86103d2, 0x00, 0x00},
    // 008 - 02345678 EORQ ($nn) aa55aa55 = a86103d2
    {0, 0x52, 0x01, "eorq [$nn]", 0x02345678, 0xaa5555aa, 0xffffffff, 0xa86103d2, 0x00, 0x00},

    // 009 - 02345678 ADCQ ($nn) 12345678 = 1468acf0
    {0, 0x72, 0x00, "adcq ($nn)", 0x02345678, 0x12345678, 0x87654321, 0x1468acf0, 0x00, 0x00},
    // 010 - 02345678 ADCQ ($nn) 12345678 = 1468acf0
    {0, 0x72, 0x01, "adcq [$nn]", 0x02345678, 0x12345678, 0x87654321, 0x1468acf0, 0x00, 0x00},

    // 011 - 02345678 SBCQ ($nn) 01234567 = 01111111
    {0, 0xf2, 0x02, "sbcq ($nn)", 0x02345678, 0x01234567, 0x22222222, 0x01111111, 0x00, 0x00},
    // 012 - 02345678 SBCQ ($nn) 01234567 = 01111111
    {0, 0xf2, 0x03, "sbcq [$nn]", 0x02345678, 0x01234567, 0x22222222, 0x01111111, 0x00, 0x00},

    // 013 - 02345678 CMPQ ($nn) 02345678 => Q unchanged, Flags -N+ZC
    {0, 0xd2, 0x00, "cmpq ($nn)", 0x02345678, 0x02345678, 0xdddddddd, 0x02345678, 0x83, 0x03},
    // 014 - 02345678 CMPQ ($nn) 02345678 => Q unchanged, Flags -N+ZC
    {0, 0xd2, 0x01, "cmpq [$nn]", 0x02345678, 0x02345678, 0xdddddddd, 0x02345678, 0x83, 0x03},

    // 013 - 02345678 STQ ($nn)
    {1, 0x92, 0x00, "stq ($nn)", 0x02345678, 0xdddddddd, 0xdddddddd, 0x02345678, 0x00, 0x00},
    // 014 - 02345678 STQ ($nn)
    {1, 0x92, 0x01, "stq [$nn]", 0x02345678, 0xdddddddd, 0xdddddddd, 0x02345678, 0x00, 0x00},

    {0, 0x00, 0x00, "end", 0, 0, 0, 0 ,0}
  };

unsigned char sub = 0;
char msg[80];
unsigned short i, line = 0;
unsigned char concmsg[81] = "", flagstr[11]="", failstr[3]="";

// Use tape buffer for code snippets
unsigned char* code_buf = (unsigned char*)0x340;

/* Setup our code snippet:
 * 0340-037f - test code
 * 0380 - test value 1
 * 0384 - test value 2 (also place where stq will store)
 * 0388 - test value 3 (if opcode uses shift from z, it will move from 384 up here)
 * 038c - result q (store a,x,y,z via non-q opcodes)
 * 0390 - result f (store flags from after the op test)
 * using real base page, we are not testing tab here
 * c4 - pointer value 1
 * c8 - pointer value 2
 *
   SEI
   CLD
   ; preload registers with garbage
   lda #$fa
   ldx #$5f
   ldy #$af
   ; z is 0, we want to load test value 1 unshifted
   ldz #$00
   CLV
   ; LDQ ($c4),Z
   NEG
   NEG
   LDA ($c4),Z
   ; we need extra opcode space for adcq and sbcq
   SEC
   CLC
   ; Do some Q instruction (clc to have place for [$nn])
   NEG
   NEG
   XXX ($c8),Z
   ; store flags after our op!
   PHP
   ; Store result back
   STA $038C
   STX $038D
   STY $038E
   STZ $038F
   ; don't forget the flags!
   PLA
   STA $0390
   RTS
 */
unsigned char code_snippet[40] = {
    0x78, 0xd8, 0xa9, 0xfa, 0xa2, 0x5f, 0xa0, 0xaf, 0xa3, 0x00,
    0xb8, 0x42, 0x42, 0xb2, 0xc4, 0x38, 0x18, 0x42, 0x42, 0xb2,
    0xc8, 0x08, 0x8d, 0x8c, 0x03, 0x8e, 0x8d, 0x03, 0x8c, 0x8e,
    0x03, 0x9c, 0x8f, 0x03, 0x68, 0x8d, 0x90, 0x03, 0x60, 0x00
};
#define INST_OFFSET 15

unsigned long load_addr;

unsigned long result_q;
unsigned char result_f, status;

unsigned int reslo, reshi;

unsigned char char_code;
unsigned short pixel_addr;

void init_mega65(void) {
  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // Stop all DMA audio first
  POKE(0xD720, 0);
  POKE(0xD730, 0);
  POKE(0xD740, 0);
  POKE(0xD750, 0);
}

unsigned char state[13];

void h640_text_mode(void)
{
  // save state
  state[0] = PEEK(0xD018);
  state[1] = PEEK(0xD054);
  state[2] = PEEK(0xD031);
  state[3] = PEEK(0xD016);
  state[4] = PEEK(0xD058);
  state[5] = PEEK(0xD059);
  state[6] = PEEK(0xD05E);
  state[7] = PEEK(0xD060);
  state[8] = PEEK(0xD061);
  state[9] = PEEK(0xD062);
  state[10] = PEEK(0xD05D);
  state[11] = PEEK(0xD020);
  state[12] = PEEK(0xD021);

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
  // Disable hot registers
  POKE(0xD05D, PEEK(0xD05D) & 0x7f);

  // light grey background, black frame
  POKE(0xD020, 0);
  POKE(0xD021, 0x0b);
}

void restore_graphics(void) {
  // restore saved state
  POKE(0xD05D, state[10]);
  POKE(0xD018, state[0]);
  POKE(0xD054, state[1]);
  POKE(0xD031, state[2]);
  POKE(0xD016, state[3]);
  POKE(0xD058, state[4]);
  POKE(0xD059, state[5]);
  POKE(0xD05E, state[6]);
  POKE(0xD060, state[7]);
  POKE(0xD061, state[8]);
  POKE(0xD062, state[9]);
  POKE(0xD020, state[11]);
  POKE(0xD021, state[12]);
}

void print_text80(unsigned char x, unsigned char y, unsigned char colour, char* msg)
{
  pixel_addr = 0xC000 + x + y*80;
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

unsigned char keybuffer(unsigned char wait) {
  unsigned char key = 0;
  // clear keyboard buffer
  while (PEEK(0xD610))
    POKE(0xD610, 0);

  if (wait) {
    while ((key = PEEK(0xD610)) == 0);
    POKE(0xD610, 0);
  }

  return key;
}

void format_flags(unsigned char flags)
{
    flagstr[0] = '[';
    if (flags & 0x80)
        flagstr[1] = 'n';
    else
        flagstr[1] = '.';
    if (flags & 0x40)
        flagstr[2] = 'v';
    else
        flagstr[2] = '.';
    if (flags & 0x20)
        flagstr[3] = 'e';
    else
        flagstr[3] = '.';
    if (flags & 0x10)
        flagstr[4] = 'b';
    else
        flagstr[4] = '.';
    if (flags & 0x08)
        flagstr[5] = 'd';
    else
        flagstr[5] = '.';
    if (flags & 0x04)
        flagstr[6] = 'i';
    else
        flagstr[6] = '.';
    if (flags & 0x02)
        flagstr[7] = 'z';
    else
        flagstr[7] = '.';
    if (flags & 0x01)
        flagstr[8] = 'c';
    else
        flagstr[8] = '.';
    flagstr[9] = ']';
    flagstr[10] = 0x0;
}

void main(void)
{
  asm("sei");

  init_mega65();
  h640_text_mode();
  keybuffer(0);

  snprintf(msg, 80, "unit test #%03d - %s", ISSUE_NUM, ISSUE_NAME);
  print_text80(0, line++, 1, msg);
  unit_test_setup(ISSUE_NAME, ISSUE_NUM);
  sub++; // 0 is setup, first test is 1

  // Pre-install code snippet
  lcopy((long)code_snippet, (long)code_buf, 40);
  
  // setup zero page pointers
  *(unsigned long*)0xc4 = 0x00000380;
  *(unsigned long*)0xc8 = 0x00000384;

  // Run each test
  for (i = 0; tests[i].opcode1; i++) {
    // Setup input values
    *(unsigned long*)0x380 = tests[i].val1;
    *(unsigned long*)0x384 = tests[i].val2;
    *(unsigned long*)0x388 = tests[i].val3;
    *(unsigned long*)0x38c = 0xb1eb1bbe;

    // change code

    if (tests[i].opcode2==0) {
      // 16 bit prefix sec, clc, 2x neg
      code_buf[INST_OFFSET]   = 0x38;
      code_buf[INST_OFFSET+1] = 0x18;
      code_buf[INST_OFFSET+2] = 0x42;
      code_buf[INST_OFFSET+3] = 0x42;
    } else if (tests[i].opcode2==1) {
      // 32 bit prefix clc, 2x neg, eom
      code_buf[INST_OFFSET]   = 0x18;
      code_buf[INST_OFFSET+1] = 0x42;
      code_buf[INST_OFFSET+2] = 0x42;
      code_buf[INST_OFFSET+3] = 0xea;
    } else if (tests[i].opcode2==2) {
      // 16 bit prefix clc, sec, 2x neg
      code_buf[INST_OFFSET]   = 0x18;
      code_buf[INST_OFFSET+1] = 0x38;
      code_buf[INST_OFFSET+2] = 0x42;
      code_buf[INST_OFFSET+3] = 0x42;
    } else if (tests[i].opcode2==3) {
      // 32 bit prefix sec, 2x neg, eom
      code_buf[INST_OFFSET]   = 0x38;
      code_buf[INST_OFFSET+1] = 0x42;
      code_buf[INST_OFFSET+2] = 0x42;
      code_buf[INST_OFFSET+3] = 0xea;
    }
    code_buf[INST_OFFSET+4] = tests[i].opcode1;

    __asm__("jsr $0340");

    // read results
    if (tests[i].rmw == 1)
      result_q = *(unsigned long*)0x384; // load from test value 2 location
    else
      result_q = *(unsigned long*)0x38c; // load non-q stored result
    result_f = *(unsigned char*)0x390; // the flags after the op

    reslo = (unsigned int) (result_q&0xffff);
    reshi = (unsigned int) (result_q>>16);
    format_flags(result_f);
    // check return values
    if (result_q != tests[i].expected && (result_f&tests[i].flag_mask) != tests[i].flag_val) {
        snprintf(concmsg, 35, "%-.11s q=$%04x%04x %s", tests[i].message, reshi, reslo, flagstr);
        failstr[0] = 'q';
        failstr[1] = 'f';
        failstr[2] = 0x0;
        status = TEST_FAIL;
    } else if (result_q != tests[i].expected) {
        snprintf(concmsg, 35, "%-.11s q=$%04x%04x", tests[i].message, reshi, reslo);
        failstr[0] = 'q';
        failstr[1] = 0x0;
        status = TEST_FAIL;
    } else if ((result_f&tests[i].flag_mask) != tests[i].flag_val) {
        snprintf(concmsg, 35, "%-.11s %s", tests[i].message, flagstr);
        failstr[0] = 'f';
        failstr[1] = 0x0;
        status = TEST_FAIL;
    } else {
        snprintf(concmsg, 35, "%-.11s", tests[i].message);
        failstr[0] = 0x0;
        status = TEST_PASS;
    }

    if (line < 24) {
      if (status == TEST_FAIL)
        snprintf(msg, 80, "*%03d - fail(%-2s) - %-11.11s q=$%04x%04x %s", sub, failstr, tests[i].message, reshi, reslo, flagstr);
      else
        snprintf(msg, 80, "*%03d - pass     - %-11.11s q=$%04x%04x %s", sub, tests[i].message, reshi, reslo, flagstr);
      print_text80(0, line++, 1, msg);
    }
    unit_test_set_current_name(concmsg);
    unit_test_report(ISSUE_NUM, sub++, status);
  }

  unit_test_set_current_name(ISSUE_NAME);
  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);

  keybuffer(1);

  restore_graphics();
}
