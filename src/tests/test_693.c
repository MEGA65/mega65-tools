/*
  Ethernet buffer mapping at $d800 not working
*/
#define ISSUE_NUM 693
#define ISSUE_NAME "ethernet buffer mapping at $d800 not working"

#include <stdio.h>
#include <stdint.h>

#include <memory.h>
#include <tests.h>

static char msg[80 + 1];
static uint8_t test_case_id = 0;
static uint16_t idx;
static uint32_t long_address = 0;
static uint16_t address = 0;
static uint8_t data_28, data_16;

#define ETH_BUF_SIZE 2048

void main(void)
{
  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  mega65_io_enable();
   
  // disable IRQs as they won't wotk with Ethernet personality
  __asm__("sei");
  
  // enable Ethernet personality (mapping of Ethernet buffer at $d800)
  POKE(0xD02F, 0x45);
  POKE(0xD02F, 0x54);

  for (idx = 0; idx < ETH_BUF_SIZE; ++idx) {
    long_address = 0xFFDE800ul + idx;
    address = 0xD800u + idx;

    data_28 = lpeek(long_address);
    data_16 = PEEK(address);

    if (data_28 != data_16) {
      mega65_io_enable();
      snprintf(msg, 80, "$%07lx not matching $%04x", long_address, address);
      unit_test_log(msg);
      unit_test_report(ISSUE_NUM, test_case_id, TEST_FAIL);
      break;
    }
  }

  // we need to make sure that Ethernet personality is off again before re-enabling IRQs
  mega65_io_enable();
  __asm__("cli");

  if (idx == ETH_BUF_SIZE) {
    printf("done\n");
    unit_test_report(ISSUE_NUM, test_case_id, TEST_PASS);
  }

  printf("Issue #%d - %s", ISSUE_NUM, ISSUE_NAME);

  unit_test_set_current_name(ISSUE_NAME);
  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
