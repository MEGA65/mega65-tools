/*
  Issue #459: adcq/sbcq/cmpq flags

  The flags after executing a ADCQ, SBCQ, or CMPQ off.

  At least for ADCQ carry is inverted and for all of them
  overflow is never set. This means CMPQ is correct.

  This tests checks the three commands for result and flags.
*/
#define ISSUE_NUM 459
#define ISSUE_NAME "adcq/sbcq/cmpq flags"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <memory.h>
#include <tests.h>

struct test {
  unsigned char rmw;
  unsigned char opcode1;
  unsigned char opcode2;
  unsigned char message[12]; // 11 max!
  unsigned long val1;
  unsigned long val2;
  unsigned long expected;
  unsigned char flag_mask;
  unsigned char flag_val;
};

struct test tests[] = {
  // LDQ - check if right value is loaded, and is Q ops are working
  // 001 - 12345678 + 10fedcba = 22345678 -- NVZC all unset
  { 0, 0xad, 0x18, "ldq check", 0x12345678, 0x12345678, 0x12345678, 0x00, 0x00 },

  // ADCQ - check result and flags
  // 002 - 12345678 + 10fedcba = 22345678 -- NVZC all unset
  { 0, 0x6d, 0x18, "adcq -nvzc", 0x12345678, 0x10fedcba, 0x23333332, 0xc3, 0x00 },
  // 003 - 12345678 + ffffffff = 12345677 -- C set, NVZ unset
  { 0, 0x6d, 0x18, "adcq +c-nvz", 0x12345678, 0xffffffff, 0x12345677, 0xc3, 0x01 },
  // 004 - 7f123456 + 10fedcba = 90111110 -- NV set, ZC unset
  { 0, 0x6d, 0x18, "adcq +nv-zc", 0x7f123456, 0x10fedcba, 0x90111110, 0xc3, 0xc0 },
  // 005 - 81234567 + 8fedcba9 = 11111110 -- VC set, NZ unset
  { 0, 0x6d, 0x18, "adcq +vc-nz", 0x81234567, 0x8fedcba9, 0x11111110, 0xc3, 0x41 },
  // 006 - 12345678 + 81fedcba = a2222219 -- N set, VZC unset
  { 0, 0x6d, 0x18, "adcq +n-vzc", 0x12345678, 0x8fedcba1, 0xa2222219, 0xc3, 0x80 },
  // 007 - 12345678 + edcba988 = 00000000 -- ZC set, NV unset
  { 0, 0x6d, 0x18, "adcq +zc-nv", 0x12345678, 0xedcba988, 0x00000000, 0xc3, 0x03 },
  // 008 - 81234567 + 81234567 = 02468ace -- VC set, NZ unset
  { 0, 0x6d, 0x18, "adcq +n-vzc", 0x81234567, 0x81234567, 0x02468ace, 0xc3, 0x41 },

  // SBCQ - check result and flags
  // 009 - 12345678 - 10fedcba = 22345678 -- C set, NVZ unset
  { 0, 0xed, 0x38, "sbcq +c-nvz", 0x12345678, 0x10fedcba, 0x013579be, 0xc3, 0x01 },
  // 010 - 12345678 - ffffffff = 12345677 -- NVZC all unset
  { 0, 0xed, 0x38, "sbcq -nvzc", 0x12345678, 0xffffffff, 0x12345679, 0xc3, 0x00 },
  // 011 - 10fedcba - 7f123456 = 91eca864 -- N set, VZC unset
  { 0, 0xed, 0x38, "sbcq +n-vzc", 0x10fedcba, 0x7f123456, 0x91eca864, 0xc3, 0x80 },
  // 012 - 10fedcba - 10fedcba = 00000000 -- ZC set, NV unset
  { 0, 0xed, 0x38, "sbcq +zc-nv", 0x10fedcba, 0x10fedcba, 0x00000000, 0xc3, 0x03 },
  // 013 - 80000000 - 12345678 = 6dcba988 -- VC set, NZ unset
  { 0, 0xed, 0x38, "sbcq +vc-nz", 0x80000000, 0x12345678, 0x6dcba988, 0xc3, 0x41 },
  // 014 - 7fedcba9 - fedcba98 = 81111111 -- NV set, ZC unset
  { 0, 0xed, 0x38, "sbcq +nv-zc", 0x7fedcba9, 0xfedcba98, 0x81111111, 0xc3, 0xc0 },

  // CMPQ - check flags
  // 015 - 12345678 cmp 12345678 (equal)
  { 0, 0xcd, 0x18, "cmpq + a=m", 0x12345678, 0x12345678, 0x12345678, 0xc3, 0x03 },
  // 016 - 12345678 cmp 01234567 (a > b)
  { 0, 0xcd, 0x18, "cmpq + a>m", 0x12345678, 0x01234567, 0x12345678, 0xc3, 0x01 },
  // 017 - 12345678 cmp 23456789 (a < b)
  { 0, 0xcd, 0x18, "cmpq + a<m", 0x12345678, 0x23456789, 0x12345678, 0xc3, 0x80 },
  // 018 - fedcba98 cmp fedcba98 (equal)
  { 0, 0xcd, 0x18, "cmpq - a=m", 0xfedcba98, 0xfedcba98, 0xfedcba98, 0xc3, 0x03 },
  // 019 - fedcba98 cmp edcba987 (a > b)
  { 0, 0xcd, 0x18, "cmpq - a>m", 0xfedcba98, 0xedcba987, 0xfedcba98, 0xc3, 0x01 },
  // 020 - edcba987 cmp fedcba98 (a < b)
  { 0, 0xcd, 0x18, "cmpq - a<m", 0xedcba987, 0xfedcba98, 0xedcba987, 0xc3, 0x80 },
  // 021 - 12345678 cmp edcba987 (a < b)
  { 0, 0xcd, 0x18, "cmpq +- a<m", 0x12345678, 0xedcba987, 0x12345678, 0xc3, 0x00 },
  // 022 - edcba987 cmp 12345678 (a > b)
  { 0, 0xcd, 0x18, "cmpq -+ a>m", 0xedcba987, 0x12345678, 0xedcba987, 0xc3, 0x81 },

  { 0, 0x00, 0x18, "end", 0, 0, 0, 0, 0 }
};

unsigned char sub = 0;
char msg[80];
unsigned short i, line = 0;
unsigned char concmsg[81] = "", flagstr[11] = "", failstr[3] = "";

// Use tape buffer for code snippets
unsigned char *code_buf = (unsigned char *)0x340;

/* Setup our code snippet:
   SEI
   CLD
   ; preload registers with garbage
   lda #$fa
   ldx #$5f
   ldy #$af
   ldz #$f5
   CLV
   ; LDQ $0380
   NEG
   NEG
   LDA ($0380)
   ; Do some Q instruction
   CLC
   NEG
   NEG
   XXX $0384
   ; store flags after our op!
   PHP
   ; Store result back
   ; not suing STQ, because we want just test the op above
   STA $0388
   STX $0389
   STY $038A
   STZ $038B
   ; and the flags
   PLA
   STA $038C
   CLI
   RTS
 */
unsigned char code_snippet[42] = { 0x78, 0xd8, 0xa9, 0xfa, 0xa2, 0x5f, 0xa0, 0xaf, 0xa3, 0xf5, 0xb8, 0x42, 0x42, 0xad, 0x80,
  0x03, 0x18, 0x42, 0x42, 0x6d, 0x84, 0x03, 0x08, 0x03, 0x8d, 0x88, 0x03, 0x8e, 0x89, 0x03, 0x8c, 0x8a, 0x03, 0x9c, 0x8b,
  0x03, 0x68, 0x8d, 0x8c, 0x03, 0x60, 0x00 };
#define INST_OFFSET_1 19
#define INST_OFFSET_2 16

unsigned long load_addr;

unsigned long result_q;
unsigned char result_f, status;

unsigned int reslo, reshi;

unsigned char char_code;
unsigned short pixel_addr;

void init_mega65(void)
{
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

void restore_graphics(void)
{
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

unsigned char keybuffer(unsigned char wait)
{
  unsigned char key = 0;
  // clear keyboard buffer
  while (PEEK(0xD610))
    POKE(0xD610, 0);

  if (wait) {
    while ((key = PEEK(0xD610)) == 0)
      ;
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
  print_text80(0, line++, 7, msg);
  unit_test_setup(ISSUE_NAME, ISSUE_NUM);
  sub++; // 0 is setup, first test is 1

  // Pre-install code snippet
  lcopy((long)code_snippet, (long)code_buf, 42);

  // Run each test
  for (i = 0; tests[i].opcode1; i++) {
    // Setup input values
    *(unsigned long *)0x380 = tests[i].val1;
    *(unsigned long *)0x384 = tests[i].val2;
    // preset return values with pattern
    *(unsigned long *)0x388 = 0xb1eb1bbe;
    *(unsigned long *)0x38c = 0xb1eb1bbe;

    // change code
    code_buf[INST_OFFSET_1] = tests[i].opcode1;
    code_buf[INST_OFFSET_2] = tests[i].opcode2;
    __asm__("jsr $0340");

    // read results
    if (tests[i].rmw == 1)
      result_q = *(unsigned long *)0x384;
    else
      result_q = *(unsigned long *)0x388;
    result_f = *(unsigned char *)0x38C;

    reslo = (unsigned int)(result_q & 0xffff);
    reshi = (unsigned int)(result_q >> 16);
    format_flags(result_f);
    // check return values
    if (result_q != tests[i].expected && (result_f & tests[i].flag_mask) != tests[i].flag_val) {
      snprintf(concmsg, 35, "%-.11s q=$%04x%04x %s", tests[i].message, reshi, reslo, flagstr);
      failstr[0] = 'q';
      failstr[1] = 'f';
      failstr[2] = 0x0;
      status = TEST_FAIL;
    }
    else if (result_q != tests[i].expected) {
      snprintf(concmsg, 35, "%-.11s q=$%04x%04x", tests[i].message, reshi, reslo);
      failstr[0] = 'q';
      failstr[1] = 0x0;
      status = TEST_FAIL;
    }
    else if ((result_f & tests[i].flag_mask) != tests[i].flag_val) {
      snprintf(concmsg, 35, "%-.11s %s", tests[i].message, flagstr);
      failstr[0] = 'f';
      failstr[1] = 0x0;
      status = TEST_FAIL;
    }
    else {
      snprintf(concmsg, 35, "%-.11s", tests[i].message);
      failstr[0] = 0x0;
      status = TEST_PASS;
    }

    if (line < 24) {
      if (status == TEST_FAIL)
        snprintf(
            msg, 80, "*%03d - fail(%-2s) - %-11.11s q=$%04x%04x %s", sub, failstr, tests[i].message, reshi, reslo, flagstr);
      else
        snprintf(msg, 80, "*%03d - pass     - %-11.11s q=$%04x%04x %s", sub, tests[i].message, reshi, reslo, flagstr);
      print_text80(0, line++, (status == TEST_FAIL) ? 2 : 5, msg);
    }
    unit_test_set_current_name(concmsg);
    unit_test_report(ISSUE_NUM, sub++, status);
  }

  unit_test_set_current_name(ISSUE_NAME);
  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);

  keybuffer(1);
  restore_graphics();
}
