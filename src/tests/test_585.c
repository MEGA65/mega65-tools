/*
Issue #585 - Some bitstream builds result in faulty attic ram reads
*/
#define ISSUE_NUM 585
#define ISSUE_NAME "faulty attic ram reads"
#define NUM_ITERATIONS_PER_ADDR 10

#include <stdio.h>
#include <memory.h>
#include <tests.h>

#define NUM_TESTS 5
long test_address[NUM_TESTS] = {
  0x8000000,
  0x80f7000,
  0x8253000,
  0x86af000,
  0x87fa000,
};

void main(void)
{
  unsigned char initial_mem_value, read_mem_value, i, count;
  long address;
  unsigned short seq;
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

  // Prime attic ram to avoid first-read issue.
  lpeek(address);

  for (i = 0; i < NUM_TESTS; i++) {
    for (seq = 0, address = test_address[i]; seq < 0x1000; seq++, address++) {
      printf("Testing Memory At $%08lx\n%c", address, 145); // 145=up arrow for overwrite

      // read once to get baseline
      initial_mem_value = lpeek(address);

      for (count = 0; count < NUM_ITERATIONS_PER_ADDR; count++) {
        read_mem_value = lpeek(address);
        if (read_mem_value != initial_mem_value) {
          snprintf(msg, 40, "\n%cAttic RAM Test failure at %07lx: %x vs %x%c\n", 28, address, initial_mem_value, read_mem_value, 5);
          unit_test_fail(msg);
          printf("\n%s\n", msg);
          seq = 0x7ffe;
          break;
        }
      }
    }
    if (seq == 0x1000) {
      snprintf(msg, 40, "attic ram test $%07lx", test_address[i]);
      unit_test_ok(msg);
      printf("\n%cAttic RAM Test at $%07lx succeeded%c\n", 30, test_address[i], 5);
    }
  }

  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
