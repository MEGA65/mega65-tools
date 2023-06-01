/*
  Sometimes ADC/SBC mis-behaviour after eth tx trigger
*/
#define ISSUE_NUM 696
#define ISSUE_NAME "sometimes adc/sbc mis-behaviour after eth tx trigger"

#include <stdio.h>
#include <stdint.h>
#include <tests.h>

uint8_t test_loop();

void main(void)
{
  static uint8_t test_case_id = 0;
  static uint8_t result;
  static uint8_t i = 0;
  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  printf("Issue #%d - %s\n", ISSUE_NUM, ISSUE_NAME);

  result = test_loop();
  if (result == 0) {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_FAIL);
  } else {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_PASS);
  }
 
  unit_test_set_current_name(ISSUE_NAME);
  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
