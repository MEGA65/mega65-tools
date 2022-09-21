/*
  Fine raster IRQs (compare registers $d079/$d07a) do not behave as are known
  from a VIC-II. They compare and set the relevant bit in $d019 both at the
  beginning of a raster line as well as at the very end. This leads to odd
  effects if the IRQ is acknowledged when still in the line where it was
  triggered (eg. if the ack is happening at the beginning of the IRQ handler).
  The result is the IRQ is triggered twice instead of once (see issue #604).

  This programme tests if the raster IRQ is exactly triggered once per frame.
*/
#define ISSUE_NUM 604
#define ISSUE_NAME "vic-iv raster irq incorrectly triggered twice"

#include <stdio.h>
#include <stdint.h>

#include <memory.h>
#include <tests.h>

char msg[80 + 1];

int8_t test_status = -1;
uint8_t last_frame_counter;

void init(void)
{
  // Fast CPU, M65 IO
  mega65_io_enable();

  // black screen
  POKE(0xD020, 0);
  POKE(0xD021, 0);
}

extern void setup_irq(void);

void main(void)
{
  uint8_t frames_to_go = 50;
  uint8_t cur_frame, last_frame;

  init();
  printf("%cIssue #%d - %s\n\n", 0x93 /*clrscr*/, ISSUE_NUM, ISSUE_NAME);

  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  setup_irq();

  last_frame = PEEK(0xD7FA);
  while (frames_to_go != 0) {
    cur_frame = PEEK(0xD7FA);
    if (cur_frame != last_frame) {
      last_frame = cur_frame;
      --frames_to_go;
    }
  }

  switch (test_status) {
  case -1:
    snprintf(msg, 80, "irq handler was not able to execute");
    unit_test_fail(msg);
    break;
  case 0:
    snprintf(msg, 80, "raster irq working as expected");
    unit_test_ok(msg);
    break;
  case 1:
    snprintf(msg, 80, "raster irq behaviour wrong, called multiple times per frame");
    unit_test_fail(msg);
    break;
  default:
    snprintf(msg, 80, "internal error in test case");
    unit_test_fail(msg);
  }

  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
