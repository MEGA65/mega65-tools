/*
  ($nn,SP),Y Unit Test mega65-core Issue #497
*/
#define ISSUE_NUM 497
#define ISSUE_NAME "innspy test"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <memory.h>
#include <tests.h>

/* code snippet
   ; put test addresses on stack
   LDZ #$08
   LDA #$50
   PHA
   PHZ
   LDA #$51
   PHA
   PHZ
   ; test loop for 16 addresses
   LDY #$0F
loop:
   LDA ($03,SP),Y
   STA ($01,SP),Y
   DEY
   BPL loop
   ; remove addresses from stack
   PLA
   PLA
   PLA
   PLA
   RTS
 */
unsigned char code_snippet[64] = {
  0xa3, 0x08, 0xa9, 0x50, 0x48, 0xdb, 0xa9, 0x51, 0x48, 0xdb,
  0xa0, 0x0f, 0xe2, 0x03, 0x82, 0x01, 0x88, 0x10, 0xf9, 0x68,
  0x68, 0x68, 0x68, 0x60
};

unsigned char data_source[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

unsigned char data_target[32] = {
    0xff, 0x55, 0xaa, 0x5f, 0xaf, 0xf5, 0xa5, 0xfa, 0xff, 0x55, 0xaa, 0x5f, 0xaf, 0xf5, 0xa5, 0xfa, 
    0xff, 0x55, 0xaa, 0x5f, 0xaf, 0xf5, 0xa5, 0xfa, 0xff, 0x55, 0xaa, 0x5f, 0xaf, 0xf5, 0xa5, 0xfa, 
};

unsigned char data_expect[32] = {
    0xff, 0x55, 0xaa, 0x5f, 0xaf, 0xf5, 0xa5, 0xfa, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x1f, 0xff, 0x55, 0xaa, 0x5f, 0xaf, 0xf5, 0xa5, 0xfa,
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
  unsigned char *test_area1 = (unsigned char *)0x5000;
  unsigned char *test_area2 = (unsigned char *)0x5100;

  asm("sei");

  init_mega65();
  h640_text_mode();
  keybuffer(0);

  snprintf(msg, 80, "($nn,SP),Y Test #%03d", ISSUE_NUM);
  print_text80(0, line++, 7, msg);
  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  // Pre-install code snippet
  lcopy((long)code_snippet, (long)code_buf, 64);

  // test 1: copy source & target preset
  lcopy((long)data_source, (long)test_area1, 32);
  lcopy((long)data_target, (long)test_area2, 32);

  // Run test
  __asm__("jsr $0340");

  print_text80(0, line++, 1, "Test 1 $5108 -> $5008, count 16:");
  // compare area2 to expected result
  for (m=0; m<32; m++) {
    snprintf(concmsg, 5, "%02x", test_area2[m]);
    print_text80(4 + (m%16)*3, line + (m/16)*2, test_area2[m]==data_expect[m]?5:2, concmsg);
    snprintf(concmsg, 5, "%02x", (unsigned char)data_expect[m]);
    print_text80(4 + (m%16)*3, line + (m/16)*2 + 1, test_area2[m]==data_expect[m]?15:2, concmsg);
    if (test_area2[m]!=data_expect[m]) failed++;
  }
  line += 4;

  print_text80(0, line++, failed>0?2:5, failed>0?"Test 1 failed :(":"Test 1 passed!");
  unit_test_report(ISSUE_NUM, 1, failed>0?TEST_FAIL:TEST_PASS);

  // test 2: page crossing
  test_area1 = (unsigned char *)0x50f0;
  test_area2 = (unsigned char *)0x51f0;
  lcopy((long)data_source, (long)test_area1, 32);
  lcopy((long)data_target, (long)test_area2, 32);
  code_buf[1] = 0xf8;

  // Run test
  __asm__("jsr $0340");

  print_text80(0, line++, 1, "Test 2 $51f8 -> $50f8, count 16 (page crossing):");
  // compare area2 to expected result
  for (m=0; m<32; m++) {
    snprintf(concmsg, 5, "%02x", test_area2[m]);
    print_text80(4 + (m%16)*3, line + (m/16)*2, test_area2[m]==data_expect[m]?5:2, concmsg);
    snprintf(concmsg, 5, "%02x", (unsigned char)data_expect[m]);
    print_text80(4 + (m%16)*3, line + (m/16)*2 + 1, test_area2[m]==data_expect[m]?15:2, concmsg);
    if (test_area2[m]!=data_expect[m]) failed++;
  }
  line += 4;

  print_text80(0, line++, failed>0?2:5, failed>0?"Test 2 failed :(":"Test 2 passed!");
  unit_test_report(ISSUE_NUM, 2, failed>0?TEST_FAIL:TEST_PASS);

  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);

  snprintf(msg, 80, "($nn,SP),Y Test Finished #%03d                                                  ", ISSUE_NUM);
  print_text80(0, (line<24)?line:24, 7, msg);

  keybuffer(1);
  restore_graphics();
}
