/*
  W-Opcode Unit Test mega65-core Issue #495
  - #355 INW/DEW
  - #306 ASW/ROW
*/
#define ISSUE_NUM 495
#define ISSUE_NAME "w instruction test suite"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <memory.h>
#include <tests.h>

typedef struct {
  // read result value from where?
  //  0 == opcode save area (0x038c)
  //  1 == val2 area (0x384)
  //  2 == base page address (set in suite)
  //  3 == check stack for values
  unsigned char rmw;
  // the opcode!
  unsigned char opcode;
  // which addressing mode / flags:
  //  0x01 - base page
  //  0x02 - absolute
  //  0x04 - base page indirect z indexed
  //  0x08 - 32-bit base page indirect z indexed
  //  0x10 - base bage x indexed
  //  0x20 - absolute x indexed
  //  0x40 - immediate word
  //  0x80 - SEC before executing (without: CLC)
  unsigned char mode;
  // some name
  unsigned char name[16]; // 15 max!
} opcode_mode;

typedef struct {
  // modes to which this test is applied to (mask)
  unsigned char apply_mode;
  // added to mode name
  unsigned char name[10]; // 9 max!
  // our three test values
  unsigned short val1;
  unsigned short val2;
  unsigned short val3;
  // the result that we expect
  unsigned short expected;
  // mask and value for the status register
  unsigned char flag_mask;
  unsigned char flag_val;
  // offset for reading the result back (X indexed)
  unsigned char read_offset;
} opcode_test;

typedef struct {
  // name of the test suite
  unsigned char name[16];
  // offsets into code and basepage address to use
  unsigned char offset1, offset2, basepage;
  // the modes we are testing (only one can be variable)
  opcode_mode modes[6];
  // the test cases we are trying
  opcode_test tests[];
} opcode_suite;

/*
C3   DEW $nn
*/

opcode_suite test_dew = { "dew", 63, 12, 0x64,
  {
      { 2, 0xc3, 0x01, "dew $nn" },
      { 0, 0 },
  },
  { { 0x01, " -nz", 0x1234, 0x1234, 0xaadd, 0x1233, 0xc3, 0x00, 0 },
      { 0x01, " +z-n", 0x1234, 0x0001, 0xaadd, 0x0000, 0xc3, 0x02, 0 },
      { 0x01, " +n-z", 0x1234, 0x0000, 0xaadd, 0xffff, 0xc3, 0x80, 0 }, { 0 } } };

/*
E3   INW $nn
*/

opcode_suite test_inw = { "inw", 63, 12, 0x64,
  {
      { 2, 0xe3, 0x01, "inw $nn" },
      { 0, 0 },
  },
  { { 0x01, " -nz", 0x1234, 0x1234, 0xaadd, 0x1235, 0xc3, 0x00, 0 },
      { 0x01, " +z-n", 0x1234, 0xffff, 0xaadd, 0x0000, 0xc3, 0x02, 0 },
      { 0x01, " +n-z", 0x1234, 0x7fff, 0xaadd, 0x8000, 0xc3, 0x80, 0 }, { 0 } } };

/*
CB   ASW $nnnn
*/

opcode_suite test_asw = { "asw", 63, 12, 0x64,
  {
      { 1, 0xcb, 0x02, "asw $nnnn" },
      { 0, 0 },
  },
  { { 0x02, " -nzc", 0x0234, 0x02c4, 0x1234, 0x0588, 0x83, 0x00, 0 },
      { 0x02, " +zc-n", 0x0234, 0x8000, 0x1234, 0x0000, 0x83, 0x03, 0 },
      { 0x02, " +c-nz", 0x0234, 0x8234, 0x1234, 0x0468, 0x83, 0x01, 0 },
      { 0x02, " +n-zc", 0x0234, 0x7f34, 0x1234, 0xfe68, 0x83, 0x80, 0 },
      { 0x02, " +nc-z", 0x0234, 0xfedc, 0x1234, 0xfdb8, 0x83, 0x81, 0 }, { 0 } } };

/*
EB   ROW $nnnn
*/

opcode_suite test_row0 = { "row c=0", 63, 12, 0x64,
  {
      { 1, 0xeb, 0x02, "row0 $nnnn" },
      { 0, 0 },
  },
  { { 0x02, " -nzc", 0x0234, 0x02c4, 0x1234, 0x0588, 0xc3, 0x00, 0 },
      { 0x02, " +zc-n", 0x0234, 0x8000, 0x1234, 0x0000, 0xc3, 0x03, 0 },
      { 0x02, " +c-nz", 0x0234, 0x8234, 0x1234, 0x0468, 0xc3, 0x01, 0 },
      { 0x02, " +n-zc", 0x0234, 0x7f34, 0x1234, 0xfe68, 0xc3, 0x80, 0 },
      { 0x02, " +nc-z", 0x0234, 0xfedc, 0x1234, 0xfdb8, 0xc3, 0x81, 0 }, { 0 } } };

opcode_suite test_row1 = { "row c=1", 63, 12, 0x64,
  {
      { 1, 0xeb, 0x82, "row1 $nnnn" },
      { 0, 0 },
  },
  { { 0x02, " -nzc", 0x0234, 0x02c4, 0x0000, 0x0589, 0xc3, 0x00, 0 },
      // ROW with c=1 can never be zero!
      //{0x02, " +zc-n", 0x0234, 0x8000, 0x0000, 0x0001, 0xc3, 0x03, 0},
      { 0x02, " +c-nz", 0x0234, 0x8234, 0x0000, 0x0469, 0xc3, 0x01, 0 },
      { 0x02, " +n-zc", 0x0234, 0x7f34, 0x0000, 0xfe69, 0xc3, 0x80, 0 },
      { 0x02, " +nc-z", 0x0234, 0xfedc, 0x0000, 0xfdb9, 0xc3, 0x81, 0 }, { 0 } } };

/*
F4 PHW #$nnnn
FC PHW $nnnn
*/

opcode_suite test_phw = { "phw", 20, 12, 0x64,
  {
      { 3, 0xf4, 0x40, "phw #$nnnn" },
      { 3, 0xfc, 0x02, "phw $nnnn" },
      { 0, 0 },
  },
  { { 0x42, " !-nz", 0x1234, 0x1234, 0xaadd, 0x1234, 0xc3, 0x00, 0 },
      { 0x42, " !+z-n", 0x1234, 0x0000, 0xaadd, 0x0000, 0xc3, 0x00, 0 },
      { 0x42, " !+n-z", 0x1234, 0x8fed, 0xaadd, 0x8fed, 0xc3, 0x00, 0 }, { 0 } } };

// defintion of all the test suites to run
opcode_suite *suites[] = { &test_dew, &test_inw, &test_asw, &test_row0, &test_row1, &test_phw, 0x0 };

/* code snippet
   SEI
   CLD
   ; preload registers with garbage
   LDA #$fa
   LDX #$f5
   LDY #$af
   LDZ #$5f
   ; Test CLC padding
   CLC
   CLC
   ; Test Opcode plus EOM padding for 16 bit addr
   INW/DEW $NN

   ; store flags after our op!
   PHP
   PLA
   STA $0390
   RTS
   BRK...
   ; something to copy over the RTS above
   ; high byte gets pushed last, so we get it back first
   PLA
   STA $038D
   ; now the low byte
   PLA
   STA $038C
   RTS
   BRK
 */
unsigned char code_snippet[64] = {
  0x78,
  0xd8,
  0xa9,
  0xfa,
  0xa2,
  0xf5,
  0xa0,
  0xaf,
  0xa3,
  0x5f,
  // Test Opcode at 0384 (with padding) -- this is the part that gets changed every test
  0x18,
  0x18,
  0xc3,
  0x64,
  0xea,
  // save flags via stack to 0x390
  0x08,
  0x68,
  0x8d,
  0x90,
  0x03,
  // rts or pop pushed word
  0x60,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  // pop word
  0x68,
  0x8d,
  0x8d,
  0x03,
  0x68,
  0x8d,
  0x8c,
  0x03,
  0x60,
  0x00,
};

unsigned short line = 0, total = 0, total_failed = 0;
unsigned char msg[81] = "";

// save vic state for restoring later...
unsigned char state[13];

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
  unsigned char char_code;
  unsigned short pixel_addr = 0xC000 + x + y * 80;

  if (y > 24)
    return; // don't print beyond screen

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

void run_suite(unsigned char issue_num, opcode_suite *suite)
{
  unsigned short result_w;
  unsigned char result_f, status;
  unsigned char m, t, count = 0, sub = 1, failed = 0, fi, flagmask;
  unsigned char concmsg[81] = "", failstr[3] = "", testname[21] = "", status_flags[] = "nvebdizc",
                flagstr[11] = "[........]";
  unsigned char *code_buf = (unsigned char *)0x340; // Tape Buffer

  // Pre-install code snippet
  lcopy((long)code_snippet, (long)code_buf, 64);

  // Run each test
  for (m = 0; suite->modes[m].opcode; m++) {
    for (t = 0; suite->tests[t].apply_mode; t++) {
      if ((suite->modes[m].mode & suite->tests[t].apply_mode)) {
        // if ((suite->modes[m].mode & suite->tests[t].apply_mode)) &&
        //     !(suite->modes[m].mode & 0x40)) { // skip Q implied for now
        //  count for test and total
        count++;
        total++;

        // Setup input values, always set val1 to 0x380
        *(unsigned short *)0x380 = suite->tests[t].val1;
        if (suite->modes[m].mode & 0x11) { // $nn / $nn,x
          // for base page opcodes
          *(unsigned short *)0x60 = suite->tests[t].val1;
          *(unsigned short *)0x64 = suite->tests[t].val2;
          *(unsigned short *)0x68 = suite->tests[t].val3;
        }
        else {
          // for absolute opcodes
          *(unsigned short *)0x384 = suite->tests[t].val2;
          *(unsigned short *)0x388 = suite->tests[t].val3;
          // setup zero page pointers - we use part of the Basic FAC range of addresses here
          *(unsigned long *)0x60 = 0x00000380;
          *(unsigned long *)0x64 = 0x00000384;
        }
        // preset return value with pattern
        *(unsigned long *)0x38c = 0xb1eb1bbe;

        // change code for test
        // set or clear carry/overflow?
        code_buf[suite->offset2 - 2] = (suite->modes[m].rmw == 3) ? 0xb8 : 0x18;
        code_buf[suite->offset2 - 1] = (suite->modes[m].mode & 0x80) ? 0x38 : 0x18;
        // opcode
        code_buf[suite->offset2] = suite->modes[m].opcode;
        // operant
        if (suite->modes[m].mode & 0x1d) { // $nn / $nn,x / ($nn) / [$nn]
          code_buf[suite->offset2 + 1] = suite->basepage;
          code_buf[suite->offset2 + 2] = 0xea;
        }
        else if (suite->modes[m].mode & 0x22) { // $nnnn / $nnnn,x
          code_buf[suite->offset2 + 1] = 0x84;
          code_buf[suite->offset2 + 2] = 0x03;
        }
        else if (suite->modes[m].mode & 0x40) { // accumulator
          code_buf[suite->offset2 + 1] = (unsigned char)(suite->tests[t].val2 & 0xff);
          code_buf[suite->offset2 + 2] = (unsigned char)((suite->tests[t].val2 >> 8) & 0xff);
        }

        if (suite->modes[m].rmw != 3) {
          code_buf[suite->offset1] = 0x60; // RTS
        }
        else {
          // need to add some pop and store for the stack operators
          for (fi = 0; fi < 10; fi++)
            code_buf[suite->offset1 + fi] = code_buf[suite->offset1 + 10 + fi];
        }

        __asm__("jsr $0340");

        // read results
        if (suite->modes[m].rmw == 1)
          result_w = *(unsigned short *)(0x384 + suite->tests[t].read_offset);
        else if (suite->modes[m].rmw == 2)
          result_w = *(unsigned short *)(suite->basepage + suite->tests[t].read_offset);
        else
          result_w = *(unsigned short *)0x38c;
        result_f = *(unsigned char *)0x390;

        // format flags
        for (fi = 0, flagmask = 0x80; fi < 8; flagmask >>= 1, fi++)
          flagstr[fi + 1] = (result_f & flagmask) ? status_flags[fi] : '.';

        // check return values
        snprintf(testname, 20, "%s%s", suite->modes[m].name, suite->tests[t].name);
        if (result_w != suite->tests[t].expected && (result_f & suite->tests[t].flag_mask) != suite->tests[t].flag_val) {
          snprintf(concmsg, 80, "#%02d %-.20s w=$%04x %s", sub, testname, result_w, flagstr);
          failstr[0] = 'w';
          failstr[1] = 'f';
          failstr[2] = 0x0;
          status = TEST_FAIL;
        }
        else if (result_w != suite->tests[t].expected) {
          snprintf(concmsg, 80, "#%02d %-.20s w=$%04x", sub, testname, result_w);
          failstr[0] = 'w';
          failstr[1] = 0x0;
          status = TEST_FAIL;
        }
        else if ((result_f & suite->tests[t].flag_mask) != suite->tests[t].flag_val) {
          snprintf(concmsg, 80, "#%02d %-.20s %s", sub, testname, flagstr);
          failstr[0] = 'f';
          failstr[1] = 0x0;
          status = TEST_FAIL;
        }
        else {
          snprintf(concmsg, 80, "#%02d %-.20s", sub, testname);
          failstr[0] = 0x0;
          status = TEST_PASS;
        }

        if (status == TEST_FAIL) {
          snprintf(msg, 80, "#%02d*%02d - fail(%-2s) - %-20.20s w=$%04x %s", issue_num, sub, failstr, testname, result_w,
              flagstr);
          print_text80(0, line++, 1, msg);
          failed++;
          total_failed++;
        }

        unit_test_set_current_name(concmsg);
        unit_test_report(ISSUE_NUM, issue_num, status);
        sub++;
      }
    }
  }

  if (failed > 0)
    snprintf(msg, 80, "#%02d - %-3s - %d/%d tests failed", issue_num, suite->name, failed, count);
  else
    snprintf(msg, 80, "#%02d - %-3s - %d tests passed", issue_num, suite->name, count);
  print_text80(0, line++, failed > 0 ? 2 : 5, msg);

  if (failed > 0)
    snprintf(msg, 80, "%s - %d/%d tests failed", suite->name, failed, count);
  else
    snprintf(msg, 80, "%s - %d tests passed", suite->name, count);
  unit_test_set_current_name(msg);
  unit_test_report(ISSUE_NUM, issue_num, failed > 0 ? TEST_FAIL : TEST_PASS);
}

void main(void)
{
  unsigned char i;

  asm("sei");

  init_mega65();
  h640_text_mode();
  keybuffer(0);

  snprintf(msg, 80, "W-Opcode Test Suite #%03d", ISSUE_NUM);
  print_text80(0, line++, 7, msg);
  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  for (i = 0; suites[i]; i++)
    run_suite(i + 1, suites[i]);

  if (total_failed > 0)
    snprintf(msg, 80, "Total %d/%d tests failed                                                   ", total_failed, total);
  else
    snprintf(msg, 80, "Total %d tests passed                                                      ", total);
  print_text80(0, (line < 23) ? line++ : 23, total_failed > 0 ? 2 : 5, msg);

  if (total_failed > 0)
    snprintf(msg, 80, "total - %d/%d tests failed", total_failed, total);
  else
    snprintf(msg, 80, "total - %d tests passed", total);
  unit_test_set_current_name(msg);
  unit_test_report(ISSUE_NUM, 0, total_failed > 0 ? TEST_FAIL : TEST_PASS);

  unit_test_set_current_name(ISSUE_NAME);
  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
  snprintf(msg, 80, "W-Opcode Test Finished #%03d                                                  ", ISSUE_NUM);
  print_text80(0, (line < 24) ? line : 24, 7, msg);

  keybuffer(1);
  restore_graphics();
}
