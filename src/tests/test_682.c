/*
  Right physical pixel of last character in a row was not drawn with H640 mode off
*/
#define ISSUE_NUM 682
#define ISSUE_NAME "right most character pixel not drawn with h640 disabled"

#include <stdio.h>
#include <stdint.h>

#include <memory.h>
#include <tests.h>

static char msg[80 + 1];

uint8_t is_color(uint16_t x, uint16_t y, const uint8_t col[3])
{
  static uint8_t red, green, blue;
  
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
#define LEFT_BORDER 0x55
#define RIGHT_BORDER 0x55 + 640

vic_config conf;
static const uint8_t bg_col[3] = { 0, 0, 240 };
static const uint8_t border_col[3] = { 144, 144, 240 };
static const uint8_t char_col[3] = { 240, 240, 240 };
static uint8_t test_case_id = 0;

void run_test_cases_40cols(uint16_t y, char *prefix)
{
  // print 2px vertical bar on the right in first row =PETSCII char (0x67)
  POKE(0x0427, 0x67);

  ++test_case_id;
  snprintf(msg, 80, "%s - left border", prefix);
  unit_test_set_current_name(msg);
  if (is_color(LEFT_BORDER, y, border_col) &&
      is_color(LEFT_BORDER + 1, y, bg_col)) {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_PASS);
  }
  else {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_FAIL);
  }
  ++test_case_id;
  snprintf(msg, 80, "%s - right border", prefix);
  unit_test_set_current_name(msg);
  if (is_color(RIGHT_BORDER - 4, y, bg_col) &&
      is_color(RIGHT_BORDER - 3, y, char_col) &&
      is_color(RIGHT_BORDER - 2, y, char_col) &&
      is_color(RIGHT_BORDER - 1, y, char_col) &&
      is_color(RIGHT_BORDER + 0, y, char_col) &&
      is_color(RIGHT_BORDER + 1, y, border_col)) {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_PASS);
  }
  else {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_FAIL);
  }
}

void run_test_cases_80cols(uint16_t y, char *prefix)
{
  // print 2px vertical bar on the right in first row =PETSCII char (0x67)
  POKE(0xC04F, 0x67);

  ++test_case_id;
  snprintf(msg, 80, "%s - left border", prefix);
  unit_test_set_current_name(msg);
  if (is_color(LEFT_BORDER, y, border_col) &&
      is_color(LEFT_BORDER + 1, y, bg_col)) {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_PASS);
  }
  else {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_FAIL);
  }
  ++test_case_id;
  snprintf(msg, 80, "%s - right border", prefix);
  unit_test_set_current_name(msg);
  if (is_color(RIGHT_BORDER - 2, y, bg_col) &&
      is_color(RIGHT_BORDER - 1, y, char_col) &&
      is_color(RIGHT_BORDER + 0, y, char_col) &&
      is_color(RIGHT_BORDER + 1, y, border_col)) {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_PASS);
  }
  else {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_FAIL);
  }
}

void main(void)
{
  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  unit_test_vic_get_default_pal(&conf);
  conf.h640 = 0;
  conf.v400 = 0;
  conf.crt_emu = 0;
  conf.cols = 40;
  unit_test_init_vic(&conf);

  run_test_cases_40cols(PAL_TOP_BORDER + 4, "pal 40 cols");

  unit_test_vic_get_default_pal(&conf);
  conf.h640 = 1;
  conf.v400 = 0;
  conf.crt_emu = 0;
  conf.cols = 80;
  unit_test_init_vic(&conf);

  run_test_cases_80cols(PAL_TOP_BORDER + 4, "pal 80 cols");

  unit_test_vic_get_default_ntsc(&conf);
  conf.h640 = 0;
  conf.v400 = 0;
  conf.crt_emu = 0; 
  conf.cols = 40;
  unit_test_init_vic(&conf);

  run_test_cases_40cols(NTSC_TOP_BORDER + 4, "ntsc 40 cols");

  unit_test_vic_get_default_ntsc(&conf);
  conf.h640 = 1;
  conf.v400 = 0;
  conf.crt_emu = 0; 
  conf.cols = 80;
  unit_test_init_vic(&conf);

  run_test_cases_80cols(NTSC_TOP_BORDER + 4, "ntsc 80 cols");

  snprintf(msg, 80, "Issue #%d - %s", ISSUE_NUM, ISSUE_NAME);
  unit_test_print(0, 1, 14, msg);

  unit_test_set_current_name(ISSUE_NAME);
  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
