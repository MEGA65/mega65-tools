/*
  Last raster line was not showing in 80x50 char mode
*/
#define ISSUE_NUM 671
#define ISSUE_NAME "80x50 text mode missing bottommost pixel row in ntsc only"

#include <stdio.h>
#include <stdint.h>

#include <memory.h>
#include <tests.h>

char msg[80 + 1];

int8_t test_status = -1;

void init(uint8_t ntsc)
{
  vic_config conf;
  if (ntsc) {
    unit_test_vic_get_default_ntsc(&conf);
  }
  else {
    unit_test_vic_get_default_pal(&conf);
  }
  conf.cols = 80;
  conf.rows = 50;
  conf.h640 = 1;
  conf.v400 = 1;
  conf.crt_emu = 0;
  unit_test_init_vic(&conf);
}

uint8_t red, green, blue;

uint8_t bg_col[3] = { 0, 0, 240 };
uint8_t border_col[3] = { 144, 144, 240 };
uint8_t char_col[3] = { 240, 240, 240 };

uint8_t is_color(uint16_t x, uint16_t y, uint8_t col[3])
{
  unit_test_read_pixel(x, y, &red, &green, &blue);
  if (red == col[0] && green == col[1] && blue == col[2]) {
    return 1;
  }
  snprintf(msg, 80, "Read colour (r/g/b): %d/%d/%d", red, green, blue);
  unit_test_print(0, 3, 14, msg);
  snprintf(msg, 80, "Expected colour (r/g/b): %d/%d/%d", col[0], col[1], col[2]);
  unit_test_print(0, 4, 14, msg);
  return 0;
}

#define NTSC_TOP_BORDER 0x2A
#define PAL_TOP_BORDER 0x68

static uint8_t test_case_id = 0;

void run_test_cases(uint16_t top_border, char *prefix)
{
  // print 1px high lower bar PETSCII char (0x64) in last line
  POKE(0xC000 + 49 * 80 + 1, 0x64);

  ++test_case_id;
  snprintf(msg, 80, "%s - top border", prefix);
  unit_test_set_current_name(msg);
  if (is_color(98, top_border - 1, border_col) &&
      is_color(98, top_border, bg_col)) {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_PASS);
  }
  else {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_FAIL);
  }
  ++test_case_id;
  snprintf(msg, 80, "%s - bottom border", prefix);
  unit_test_set_current_name(msg);
  if (is_color(98, top_border + 398, bg_col) &&
      is_color(98, top_border + 399, char_col) &&
      is_color(98, top_border + 400, border_col)) {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_PASS);
  }
  else {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_FAIL);
  }
}

void main(void)
{
  uint8_t use_ntsc = 1;

  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  init(use_ntsc);
  run_test_cases(NTSC_TOP_BORDER, "ntsc");

  use_ntsc = 0;
  init(use_ntsc);
  run_test_cases(PAL_TOP_BORDER, "pal");

  snprintf(msg, 80, "Issue #%d - %s", ISSUE_NUM, ISSUE_NAME);
  unit_test_print(0, 1, 14, msg);

  unit_test_set_current_name(ISSUE_NAME);
  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
