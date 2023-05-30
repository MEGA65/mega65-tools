/*
  Introduce new C65 bug compatibility flag
*/
#define ISSUE_NUM 685
#define ISSUE_NAME "introduce new c65 bug compatibility flag"

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
  while(1) continue;
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
  // print 2px vertical bar on the left in first row =PETSCII char (0x65)
  POKE(0x0400, 0x65);

  ++test_case_id;
  snprintf(msg, 80, "%s - left border", prefix);
  unit_test_set_current_name(msg);
  if (is_color(LEFT_BORDER, y, border_col) &&
      is_color(LEFT_BORDER + 1, y, char_col) &&
      is_color(LEFT_BORDER + 2, y, char_col) &&
      is_color(LEFT_BORDER + 3, y, char_col) &&
      is_color(LEFT_BORDER + 4, y, char_col) &&
      is_color(LEFT_BORDER + 5, y, bg_col)) {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_PASS);
  }
  else {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_FAIL);
  }
}

void run_test_cases_80cols(uint16_t y, char *prefix)
{
  // print 2px vertical bar on the left in first row =PETSCII char (0x65)
  POKE(0xC000, 0x65);

  ++test_case_id;
  snprintf(msg, 80, "%s - left border", prefix);
  unit_test_set_current_name(msg);
  if (is_color(LEFT_BORDER, y, border_col) &&
      is_color(LEFT_BORDER + 1, y, char_col) &&
      is_color(LEFT_BORDER + 2, y, char_col) &&
      is_color(LEFT_BORDER + 3, y, bg_col)) {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_PASS);
  }
  else {
    unit_test_report(ISSUE_NUM, test_case_id, TEST_FAIL);
  }
}

void main(void)
{
  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  // We are varying pal/ntsc, 40/80 cols, and vic iii bug-compat on/off
  // XSCL is only set to 2 and when bug-compat is enabled
  // (this is what the ROM is also doing as it expects the VIC III bug
  // to be modelled by the hardware).
  // The test case is executed for each combination
  unit_test_vic_get_default_pal(&conf);
  conf.h640 = 0;
  conf.v400 = 0;
  conf.disable_viciii_bug_compatibility = 1;
  conf.xscl = 0;
  conf.cols = 40;
  unit_test_init_vic(&conf);

  run_test_cases_40cols(PAL_TOP_BORDER + 4, "pal, 40 cols, bug-compat off");

  unit_test_vic_get_default_pal(&conf);
  conf.h640 = 1;
  conf.v400 = 0;
  conf.disable_viciii_bug_compatibility = 1;
  conf.xscl = 0;
  conf.cols = 80;
  unit_test_init_vic(&conf);

  run_test_cases_80cols(PAL_TOP_BORDER + 4, "pal, 80 cols, bug-compat off");

  unit_test_vic_get_default_pal(&conf);
  conf.h640 = 0;
  conf.v400 = 0;
  conf.disable_viciii_bug_compatibility = 0;
  conf.xscl = 0;
  conf.cols = 40;
  unit_test_init_vic(&conf);

  run_test_cases_40cols(PAL_TOP_BORDER + 4, "pal, 40 cols, bug-compat on");

  unit_test_vic_get_default_pal(&conf);
  conf.h640 = 1;
  conf.v400 = 0;
  conf.disable_viciii_bug_compatibility = 0;
  conf.xscl = 1;
  conf.cols = 80;
  unit_test_init_vic(&conf);

  run_test_cases_80cols(PAL_TOP_BORDER + 4, "pal, 80 cols, bugcompat on");

  unit_test_vic_get_default_ntsc(&conf);
  conf.h640 = 0;
  conf.v400 = 0;
  conf.disable_viciii_bug_compatibility = 1; 
  conf.xscl = 0;
  conf.cols = 40;
  unit_test_init_vic(&conf);

  run_test_cases_40cols(NTSC_TOP_BORDER + 4, "ntsc, 40 cols, bug-compat off");

  unit_test_vic_get_default_ntsc(&conf);
  conf.h640 = 1;
  conf.v400 = 0;
  conf.disable_viciii_bug_compatibility = 1; 
  conf.xscl = 0;
  conf.cols = 80;
  unit_test_init_vic(&conf);

  run_test_cases_80cols(NTSC_TOP_BORDER + 4, "ntsc, 80 cols, bug-compat off");

  unit_test_vic_get_default_ntsc(&conf);
  conf.h640 = 0;
  conf.v400 = 0;
  conf.disable_viciii_bug_compatibility = 0; 
  conf.xscl = 0;
  conf.cols = 40;
  unit_test_init_vic(&conf);

  run_test_cases_40cols(NTSC_TOP_BORDER + 4, "ntsc, 40 cols, bug-compat on");

  unit_test_vic_get_default_ntsc(&conf);
  conf.h640 = 1;
  conf.v400 = 0;
  conf.disable_viciii_bug_compatibility = 0; 
  conf.xscl = 1;
  conf.cols = 80;
  unit_test_init_vic(&conf);

  run_test_cases_80cols(NTSC_TOP_BORDER + 4, "ntsc, 80 cols, bug-compat on");

  snprintf(msg, 80, "Issue #%d - %s", ISSUE_NUM, ISSUE_NAME);
  unit_test_print(0, 1, 14, msg);

  unit_test_set_current_name(ISSUE_NAME);
  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
