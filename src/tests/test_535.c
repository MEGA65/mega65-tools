/*
  32bit indirect followup bug Unit Test mega65-core Issue #535
*/
#define ISSUE_NUM 535
#define ISSUE_NAME "32bit indirect followup bug"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <memory.h>
#include <tests.h>

/* code snippet LDA/STA
    SEI
    LDA #$30
    TAB
    LDZ #$02
    LDA [$00],Z
    STA ($04),Z // this one should *not* use the 32bit address
    LDA #$00
    TAB
    CLI
    RTS
 */
unsigned char code_ldasta[64] = {
  0x78, 0xa9, 0x30, 0x5b, 0xa3, 0x02, 0xea, 0xb2, 0x00, 0x92,
  0x04, 0xa9, 0x00, 0x5b, 0x58, 0x60
};

/* code snippet STA/LDA
    SEI
    LDA #$30
    TAB
    LDZ #$02
    STA [$00],Z
    LDA ($04),Z // this one should *not* use the 32bit address
    STA $2f0f   // store what we loaded to check
    LDA #$00
    TAB
    CLI
    RTS
 */
unsigned char code_stalda[64] = {
  0x78, 0xa9, 0x30, 0x5b, 0xa3, 0x02, 0xea, 0x92, 0x00, 0xb2,
  0x04, 0x8d, 0x0f, 0x2f, 0xa9, 0x00, 0x5b, 0x58, 0x60
};

/* code snippet LDQ/STQ
    SEI
    LDA #$30
    TAB
    LDZ #$02
    LDQ [$00],Z
    STQ ($04),Z // this one should *not* use the 32bit address
    LDA #$00
    TAB
    CLI
    RTS
 */
unsigned char code_ldqstq[64] = {
  0x78, 0xa9, 0x30, 0x5b, 0xa3, 0x02,
  0x42, 0x42, 0xea, 0xb2, 0x00,
  0x42, 0x42, 0x92, 0x04,
  0xa9, 0x00, 0x5b, 0x58, 0x60
};

/* code snippet STQ/LDQ
    SEI
    LDA #$30
    TAB
    LDZ #$02
    STQ [$00],Z
    LDQ ($04),Z // this one should *not* use the 32bit address
    STA $2f10
    STX $2f11
    STY $2f12
    STZ $2f13
    LDA #$00
    TAB
    CLI
    RTS
 */
unsigned char code_stqldq[64] = {
  0x78, 0xa9, 0x30, 0x5b, 0xa3, 0x02,
  0x42, 0x42, 0xea, 0x92, 0x00,
  0x42, 0x42, 0xb2, 0x04,
  0x8d, 0x10, 0x2f,
  0x8e, 0x11, 0x2f,
  0x8c, 0x12, 0x2f,
  0x9c, 0x13, 0x2f,
  0xa9, 0x00, 0x5b, 0x58, 0x60
};

unsigned char test_data_near[16] = {
  0xaa, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

unsigned char test_data_far[16] = {
  0x55, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 
  0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0
};

unsigned short line=0, total=0, total_failed=0;
unsigned char msg[81]="";

// save vic state for restoring later...
unsigned char state[13];

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
  unsigned char char_code;
  unsigned short pixel_addr = 0xC000 + x + y*80;

  if (y > 24) return; // don't print beyond screen

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

void main(void)
{
  unsigned char m, failed=0;
  unsigned char concmsg[81]="";
  unsigned char *code_buf   = (unsigned char *)0x340;

  asm("sei");

  init_mega65();
  h640_text_mode();
  keybuffer(0);

  snprintf(msg, 80, "%s Test #%03d", ISSUE_NAME, ISSUE_NUM);
  print_text80(0, line++, 7, msg);
  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  // setup pointers in $3000 and $3004
  POKE(0x3000, 0x03);
  POKE(0x3001, 0x2f);
  POKE(0x3002, 0x04);
  POKE(0x3003, 0x00);
  POKE(0x3004, 0x05);
  POKE(0x3005, 0x2f);
  POKE(0x3006, 0x04);
  POKE(0x3007, 0x00);

  // Pre-install code snippet
  lcopy((long)code_ldasta, (long)code_buf, 64);

  // test 1: copy source & target preset
  lcopy((long)test_data_near, 0x2f00UL, 16);
  lcopy((long)test_data_far, 0x42f00UL, 16);

  // Run test
  __asm__("jsr $0340");

  unit_test_set_current_name("lda/sta");
  if (PEEK(0x2f07U) != 0x50) {
    failed++;
    print_text80(0, line++, 2, "Test 1: LDA[$00],Z ; STA($04),Z -- FAILURE");
    unit_test_report(ISSUE_NUM, 1, TEST_FAIL);
  } else {
    print_text80(0, line++, 5, "Test 1: LDA[$00],Z ; STA($04),Z -- SUCCESS");
    unit_test_report(ISSUE_NUM, 1, TEST_PASS);
  }

  // Pre-install code snippet
  lcopy((long)code_stalda, (long)code_buf, 64);

  // test 1: copy source & target preset
  lcopy((long)test_data_near, 0x2f00UL, 16);
  lcopy((long)test_data_far, 0x42f00UL, 16);

  // Run test
  __asm__("jsr $0340");

  unit_test_set_current_name("sta/lda");
  if (PEEK(0x2f0fU) != 0x07) {
    failed++;
    print_text80(0, line++, 2, "Test 2: STA[$00],Z ; LDA($04),Z -- FAILURE");
    unit_test_report(ISSUE_NUM, 2, TEST_FAIL);
  } else {
    print_text80(0, line++, 5, "Test 2: STA[$00],Z ; LDA($04),Z -- SUCCESS");
    unit_test_report(ISSUE_NUM, 2, TEST_PASS);
  }

  // Pre-install code snippet
  lcopy((long)code_ldqstq, (long)code_buf, 64);

  // test 1: copy source & target preset
  lcopy((long)test_data_near, 0x2f00UL, 16);
  lcopy((long)test_data_far, 0x42f00UL, 16);

  // Run test
  __asm__("jsr $0340");

  unit_test_set_current_name("ldq/stq");
  if ((PEEK(0x2f05U) != 0x50) ||
      (PEEK(0x2f06U) != 0x60) ||
      (PEEK(0x2f07U) != 0x70) ||
      (PEEK(0x2f08U) != 0x80)) {
    failed++;
    print_text80(0, line++, 2, "Test 3: LDQ[$00],Z ; STQ($04),Z -- FAILURE");
    unit_test_report(ISSUE_NUM, 3, TEST_FAIL);
  } else {
    print_text80(0, line++, 5, "Test 3: LDQ[$00],Z ; STQ($04),Z -- SUCCESS");
    unit_test_report(ISSUE_NUM, 3, TEST_PASS);
  }

  // Pre-install code snippet
  lcopy((long)code_stqldq, (long)code_buf, 64);

  // test 1: copy source & target preset
  lcopy((long)test_data_near, 0x2f00UL, 16);
  lcopy((long)test_data_far, 0x42f00UL, 16);

  // Run test
  __asm__("jsr $0340");

  unit_test_set_current_name("stq/ldq");
  if ((PEEK(0x2f10U) != 0x07) ||
      (PEEK(0x2f11U) != 0x08) ||
      (PEEK(0x2f12U) != 0x09) ||
      (PEEK(0x2f13U) != 0x0A)) {
    failed++;
    print_text80(0, line++, 2, "Test 4: STQ[$00],Z ; LDQ($04),Z -- FAILURE");
    unit_test_report(ISSUE_NUM, 4, TEST_FAIL);
  } else {
    print_text80(0, line++, 5, "Test 4: STQ[$00],Z ; LDQ($04),Z -- SUCCESS");
    unit_test_report(ISSUE_NUM, 4, TEST_PASS);
  }

  unit_test_set_current_name(ISSUE_NAME);
  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);

  snprintf(msg, 80, "%s Test Finished #%03d                                                  ", ISSUE_NAME, ISSUE_NUM);
  print_text80(0, (line<24)?line:24, 7, msg);

  keybuffer(1);
  restore_graphics();
}
