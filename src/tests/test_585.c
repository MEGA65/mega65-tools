/*
Issue #585 - Some bitstream builds result in faulty attic ram reads
*/
#define ISSUE_NUM 585
#define ISSUE_NAME "faulty attic ram reads"
#define START_ADDRESS 0x8000000
#define END_ADDRESS 0x8001000
#define NUM_ITERATIONS_PER_ADDR 10

#include <stdio.h>
#include <memory.h>
#include <tests.h>

void main(void)
{
  unsigned char initial_mem_value, read_mem_value;
  long address = START_ADDRESS, end_address = END_ADDRESS;
  char msg[41] = "";

  asm("sei");

  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  POKE(0xD020, 0);
  POKE(0xD021, 0);

  unit_test_setup(ISSUE_NAME, ISSUE_NUM);
  printf("%c%c", 147, 5); // clear screen; color white
  printf("issue #%d - %s\n", ISSUE_NUM, ISSUE_NAME);

  // Prime attic ram to avoid first-read issue.
  lpeek(address);

  while (address <= end_address) {
    int count;
    printf("Testing Memory At $%07lx\n%c", address, 145); // 145=up arrow for overwrite

    // read once to get baseline
    initial_mem_value = lpeek(address);

    for (count = 0; count < NUM_ITERATIONS_PER_ADDR; ++count) {
      read_mem_value = lpeek(address);
      if (read_mem_value != initial_mem_value) {
        snprintf(msg, 40, "failure at %07lx: %x vs %x", address, initial_mem_value, read_mem_value);
        unit_test_fail(msg);
        goto done;
      }
    }
    ++address;
  }

  unit_test_ok("all tests passed!");

done:
  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
