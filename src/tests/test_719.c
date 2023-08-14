/*
Issue #719 - writing to eth tx buffer via mapped d800 is hitting colram instead
*/
#define ISSUE_NUM 719
#define ISSUE_NAME "writing to eth tx buffer via mapped d800 is hitting colram instead"

#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <tests.h>

void set_c64_io()
{
  POKE(0xD02F, 0x0);
}

void set_c65_io()
{
  POKE(0xD02F, 0xA5);
  POKE(0xD02F, 0x96);
}

void set_eth_io()
{
  POKE(0xD02F, 0x45);
  POKE(0xD02F, 0x54);
}

void set_mega65_io()
{
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);
}

void main(void)
{
  uint8_t test_byte = 0;
  uint8_t tmp;

  printf("%c%c", 147, 5); // clear screen; color white
  printf("issue #%d - %s\n", ISSUE_NUM, ISSUE_NAME);

  asm("sei");

  // Fast CPU, M65 IO
  POKE(0, 65);
  set_mega65_io();
  
  POKE(0xD020, 0);
  POKE(0xD021, 0xb);

  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  set_c64_io();
  ++test_byte;
  POKE(0xD800, test_byte);
  set_mega65_io();
  if (lpeek(0x1F800) != test_byte) {
    unit_test_fail("c64 colram d800 write test failed");
  }
  else {
    unit_test_ok("c64 colram d800 write test passed");
  }

  set_c65_io();
  ++test_byte;
  lpoke(0x1F800, 0);
  POKE(0xD800, test_byte);
  set_mega65_io();
  if (lpeek(0x1F800) != test_byte) {
    unit_test_fail("c65 colram d800 write test failed");
  }
  else {
    unit_test_ok("c65 colram d800 write test passed");
  }

  set_eth_io();
  ++test_byte;
  lpoke(0x1F800, 0);
  POKE(0xD800, test_byte);
  set_mega65_io();
  if (lpeek(0x1F800) == test_byte) {
    unit_test_fail("eth d800 write test failed");
  }
  else {
    unit_test_ok("eth d800 write test passed");
  }

  set_mega65_io();
  ++test_byte;
  lpoke(0x1F800, 0);
  POKE(0xD800, test_byte);
  if (lpeek(0x1F800) != test_byte) {
    unit_test_fail("mega65 colram d800 write test failed");
  }
  else {
    unit_test_ok("mega65 colram d800 write test passed");
  }

  POKE(0xD030, PEEK(0xD030) | 1);

  set_c64_io();
  tmp = PEEK(0xDC0B);
  lpoke(0x1FC0B, tmp);
  ++tmp;
  POKE(0xDC0B, tmp);
  set_mega65_io();
  if (lpeek(0x1FC0B) != tmp) {
    unit_test_fail("c64 dc00 write test failed");
  }
  else {
    unit_test_ok("c64 dc00 write test passed");
  }

  set_c65_io();
  ++test_byte;
  lpoke(0x1FC00, 0);
  POKE(0xDC00, test_byte);
  set_mega65_io();
  if (lpeek(0x1FC00) != test_byte) {
    unit_test_fail("c65 colram dc00 write test failed");
  }
  else {
    unit_test_ok("c65 colram dc00 write test passed");
  }

  set_eth_io();
  ++test_byte;
  lpoke(0x1FC00, 0);
  POKE(0xDC00, test_byte);
  set_mega65_io();
  if (lpeek(0x1FC00) == test_byte) {
    unit_test_fail("eth dc00 write test failed");
  }
  else {
    unit_test_ok("eth dc00 write test passed");
  }

  set_mega65_io();
  ++test_byte;
  lpoke(0x1FC00, 0);
  POKE(0xDC00, test_byte);
  if (lpeek(0x1FC00) != test_byte) {
    unit_test_fail("mega65 colram dc00 write test failed");
  }
  else {
    unit_test_ok("mega65 colram dc00 write test passed");
  }

  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
