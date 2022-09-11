/*
Issue #585 - Some bitstream builds result in faulty attic ram reads
*/
#define ISSUE_NUM 585
#define ISSUE_NAME "faulty attic ram reads"
#define NUM_ITERATIONS_PER_ADDR 10

#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <tests.h>

#define NUM_TESTS 8
long test_address[NUM_TESTS] = {
  0x8000000,
  0x8100000,
  0x8200000,
  0x8300000,
  0x8400000,
  0x8500000,
  0x8600000,
  0x8700000,
};

// in test_585_asm.s
int8_t test_status = -1;
extern void test_memory(void);

void main(void)
{
  unsigned char i;
  unsigned short pos;
  long address;
  char msg[41] = "";

  asm("sei");

  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  POKE(0xD020, 0);
  POKE(0xD021, 0xb);

  unit_test_setup(ISSUE_NAME, ISSUE_NUM);
  printf("%c%c", 147, 5); // clear screen; color white
  printf("issue #%d - %s\n", ISSUE_NUM, ISSUE_NAME);

  for (i = 0; i < NUM_TESTS; i++) {
    printf("Testing Memory At $%07lx\n", test_address[i]);
    // Prime attic ram to avoid first-read issue.
    pos = 0x400 + 40 + 20 + 80*i;
    *(unsigned char *)0x24 = (unsigned char)(pos & 0xff);
    *(unsigned char *)0x25 = (unsigned char)((pos >> 8) & 0xff);
    *(unsigned long *)0xa5 = test_address[i];
    test_memory();

    if (test_status != 0) {
      address = *(unsigned long *)0xa5;
      snprintf(msg, 40, "attic ram test $%07lx", address);
      unit_test_fail(msg);
      printf("%cAttic RAM Test failed at $%07lx%c\n", 28, address, 5);
    }
    else {
      snprintf(msg, 40, "attic ram test $%07lx", test_address[i]);
      unit_test_ok(msg);
      printf("%cAttic RAM Test at $%07lx succeeded%c\n", 30, test_address[i], 5);
    }
  }

  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
