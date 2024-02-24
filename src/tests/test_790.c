/*
Issue #790 - rts immediate mode instruction completely broken
*/
#define ISSUE_NUM 790
#define ISSUE_NAME "rts immediate mode"

#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <tests.h>

extern int test_simple_rts(void);
extern int test_rts_with_param(uint8_t param);
extern int test_rts_extended_stack(uint8_t increment);

char msg[80];
int i;

void main(void)
{
  printf("%c%c", 147, 5); // clear screen; color white
  printf("issue #%d - %s\n", ISSUE_NUM, ISSUE_NAME);

  // Fast CPU, M65 IO
  mega65_io_enable();

  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  if (test_simple_rts() == 0) {
    unit_test_ok("rts immediate mode simple test passed");
  }
  else {
    unit_test_fail("rts immediate mode simple test failed");
  }

  for (i = 0; i < 256; i++) {
    if (test_rts_with_param((uint8_t)i) != 0) {
      snprintf(msg, 80, "rts immediate mode test with param %d failed", i);
      unit_test_fail(msg);
      break;
    }
  }
  if (i == 256) {
    unit_test_ok("rts immediate mode test with param passed");
  }

  test_rts_extended_stack(0);

  for (i = 0; i < 256; i++) {
    if (test_rts_extended_stack((uint8_t)i) != 0) {
      snprintf(msg, 80, "rts immediate mode extended stack test %d failed", i);
      unit_test_fail(msg);
      break;
    }
  }
  if (i == 256) {
    unit_test_ok("rts immediate mode extended stack test passed");
  }

  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
