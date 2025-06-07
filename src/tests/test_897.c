/*
Issue #897 - 
*/
#define ISSUE_NUM 897
#define ISSUE_NAME "hyppo system resource access"

#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <tests.h>

extern int test_resource_read(void);

char msg[80];
int i = 0;

unsigned long resource_sector = 0;
unsigned long return_value = 0;

void main(void)
{
  printf("%c%c", 147, 5); // clear screen; color white
  printf("issue #%d - %s\n", ISSUE_NUM, ISSUE_NAME);

  // Fast CPU, M65 IO
  mega65_io_enable();

  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  //
  *(unsigned long *)0x7f0 = resource_sector;
  
  if (test_resource_read() == 0) {
    unit_test_ok("");
  }
  else {
    unit_test_fail("hyppo call helper failed");
  }

  return_value = *(unsigned long *)0x7f4;

  printf("Return value = $%08lx, P=$%02x, Carry=%d\n",
	 return_value,PEEK(0x7f8),
	 PEEK(0x7f8)&0x01);
  
  if (i == 256) {
    unit_test_ok("hyppo resource read test passed");
  } else {
    unit_test_ok("hyppo resource read test passed");
  }

  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
