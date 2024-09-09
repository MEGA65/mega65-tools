/*
Issue #826 - allow dma jobs to cross mb boundaries
*/
#define ISSUE_NUM 826
#define ISSUE_NAME "allow dma jobs to cross mb boundaries"

#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <tests.h>

uint8_t buf[256];
void do_dma(void);

void set_mega65_io()
{
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);
}

void main(void)
{
  uint16_t i;

  printf("%c%c", 147, 5); // clear screen; color white
  printf("issue #%d - %s\n", ISSUE_NUM, ISSUE_NAME);

  asm("sei");

  // Fast CPU, M65 IO
  POKE(0, 65);
  set_mega65_io();
  
  POKE(0xD020, 0);
  POKE(0xD021, 0xb);

  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  for (i = 0; i < 256; ++i) {
    buf[i] = i;
  }

  dmalist.option_0b = 0x0b;
  dmalist.option_80 = 0x80;
  dmalist.source_mb = 0x00;
  dmalist.option_81 = 0x81;
  dmalist.dest_mb = 0x00;
  dmalist.option_85 = 0x85;
  dmalist.dest_skip = 1;
  dmalist.end_of_options = 0x00;
  dmalist.sub_cmd = 0x00;

  dmalist.command = 0x00; // copy
  dmalist.count = 256;
  dmalist.source_addr = (uint16_t)buf;
  dmalist.source_bank = 0x00;

  POKE(0xD703, 1); // disable dma mb crossing globally

  lpoke(0x8100000, 0);

  // Test 1: Copy from bank 0 to attic without MB crossing enabled
  dmalist.dest_mb = 0x80;
  dmalist.dest_bank = 0x0f;
  dmalist.dest_addr = 0xff80;
  do_dma();
  if (lpeek(0x8100000) == 0x80) {
    unit_test_fail("dma job crossed mb boundary although not allowed");
  }
  else {
    unit_test_ok("dma job wraps at mb boundary by default");
  }

  // Test 2: Copy with MB crossing enabled
  POKE(0xD703, 3); // enable dma mb crossing globally
  lpoke(0x8100000, 0);
  do_dma();
  if (lpeek(0x8100000) == 0x80) {
    unit_test_ok("dma job crossed mb boundary (globally enabled)");
  }
  else {
    unit_test_fail("dma job did not cross mb boundary (globally enabled)");
  }

  // Test 3: Copy from bank 0 to attic with MB crossing enabled via job option
  POKE(0xD703, 1); // disable dma mb crossing globally
  lpoke(0x8100000, 0);
  dmalist.option_85 = 0x01; // re-use option 85 field for option 01
  dmalist.dest_skip = 0x01; // re-use option argument to enable mb crossing
  do_dma();
  if (lpeek(0x8100000) == 0x80) {
    unit_test_ok("dma job with option 01 crossed mb boundary");
  }
  else {
    unit_test_fail("dma job with option 01 did not cross mb boundary");
  }

  // Test 4: Copy from bank 0 to attic with MB crossing explicitly disabled via job option
  POKE(0xD703, 1); // disable dma mb crossing globally
  lpoke(0x8100000, 0);
  dmalist.option_85 = 0x01; // re-use option 85 field for option 01
  dmalist.dest_skip = 0x00; // re-use option argument to disable mb crossing
  do_dma();
  if (lpeek(0x8100000) == 0x80) {
    unit_test_fail("dma job with option 01 disabled crossed mb boundary");
  }
  else {
    unit_test_ok("dma job with option 01 disabled did not cross mb boundary");
  }

  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
