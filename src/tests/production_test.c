#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>
#include <time.h>
#include <targets.h>
#include <tests.h>

unsigned short i;
unsigned char a, b, c, d, test_line = 3;
unsigned short interval_length;
unsigned char retries = 255;

typedef struct {
  int model_id;
  uint8_t slot_mb;
  char *name;
} mega_models_t;

// clang-format off
mega_models_t mega_models[] = {
  { 0x01, 8, "MEGA65 R1" },
  { 0x02, 4, "MEGA65 R2" },
  { 0x03, 8, "MEGA65 R3" },
  { 0x04, 8, "MEGA65 R4" },
  { 0x05, 8, "MEGA65 R5" },
  { 0x06, 8, "MEGA65 R6" },
  { 0x21, 4, "MEGAphone R1" },
  { 0x22, 4, "MEGAphone R4" },
  { 0x40, 4, "Nexys4" },
  { 0x41, 4, "Nexys4DDR" },
  { 0x42, 4, "Nexys4DDR-widget" },
  { 0x60, 4, "QMTECH A100T"},
  { 0x61, 8, "QMTECH A200T"},
  { 0x62, 8, "QMTECH A325T"},
  { 0xFD, 4, "Wukong A100T" },
  { 0xFE, 8, "Simulation" },
  { 0x00, 0, "Unknown" }
};
// clang-format on

char *get_model_name(uint8_t model_id)
{
  uint8_t k;

  for (k = 0; mega_models[k].model_id; k++)
    if (model_id == mega_models[k].model_id)
      return mega_models[k].name;

  return NULL;
}

void get_interval(void)
{
  // Make sure we start measuring a fresh interval
  a = PEEK(0xD6AA);
  while (a == PEEK(0xD6AA))
    continue;

  do {
    a = PEEK(0xD6AA);
    b = PEEK(0xD6AB);
    c = PEEK(0xD6AA);
    d = PEEK(0xD6AB);
  } while (a != c || b != d);
  interval_length = a + ((b & 0xf) << 8);
}

void graphics_clear_screen(void)
{
  lfill(0x40000l, 0, 32768l);
  lfill(0x48000l, 0, 32768l);
}

void graphics_clear_double_buffer(void)
{
  lfill(0x50000l, 0, 32768l);
  lfill(0x58000l, 0, 32768l);
}

void graphics_mode(void)
{
  // Lower case
  POKE(0xD018, 0x17);
  // 16-bit text mode, full-colour text for high chars
  POKE(0xD054, 0x05);
  // H640, fast CPU
  POKE(0xD031, 0xC0);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016, 0xC9);
  // 640x200 16bits per char, 16 pixels wide per char
  // = 640/16 x 16 bits = 80 bytes per row
  POKE(0xD058, 80);
  POKE(0xD059, 80 >> 8);
  // Draw 40 (double-wide) chars per row
  POKE(0xD05E, 40);
  // Put 2KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);
  // Disable hot regs
  POKE(0xD05D, PEEK(0xD05D) & 0x7f);

  // Enable temperature compensation for internal RTC
  lpoke(0xffd311d, lpeek(0xffd311d) | 0xe0);

  // Layout screen so that graphics data comes from $40000 -- $4FFFF

  i = 0x40000 / 0x40;
  for (a = 0; a < 40; a++)
    for (b = 0; b < 25; b++) {
      POKE(0xC000 + b * 80 + a * 2 + 0, i & 0xff);
      POKE(0xC000 + b * 80 + a * 2 + 1, i >> 8);

      i++;
    }

  // Clear colour RAM, while setting all chars to 4-bits per pixel
  // Actually set colour to 15, so that 4-bit graphics mode picks up colour 15
  // when using 0xf as colour (as VIC-IV uses char foreground colour in that case)
  for (i = 0; i < 2000; i += 2) {
    lpoke(0xff80000L + 0 + i, 0x08);
    lpoke(0xff80000L + 1 + i, 0x0f);
  }
  POKE(0xD020, 0);
  POKE(0xD021, 0);

  graphics_clear_screen();
}

unsigned short pixel_addr;
unsigned char pixel_temp;
void plot_pixel(unsigned short x, unsigned char y, unsigned char colour)
{
  pixel_addr = ((x & 0xf) >> 1) + 64 * 25 * (x >> 4);
  pixel_addr += y << 3;
  pixel_temp = lpeek(0x50000L + pixel_addr);
  if (x & 1) {
    pixel_temp &= 0x0f;
    pixel_temp |= colour << 4;
  }
  else {
    pixel_temp &= 0xf0;
    pixel_temp |= colour & 0xf;
  }
  lpoke(0x50000L + pixel_addr, pixel_temp);
}

unsigned char char_code;
void print_text(unsigned char x, unsigned char y, unsigned char colour, char *msg)
{
  pixel_addr = 0xC000 + x * 2 + y * 80;
  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    POKE(pixel_addr + 0, char_code);
    POKE(pixel_addr + 1, 0);
    lpoke(0xff80000 - 0xc000 + pixel_addr + 0, 0x00);
    lpoke(0xff80000 - 0xc000 + pixel_addr + 1, colour);
    msg++;
    pixel_addr += 2;
  }
}

void activate_double_buffer(void)
{
  lcopy(0x50000, 0x40000, 0x8000);
  lcopy(0x58000, 0x48000, 0x8000);
}

unsigned char floppy_interval_first = 0;
unsigned char floppy_active = 0;
unsigned char eth_pass = 0;
unsigned char iec_pass = 0, v, y;
unsigned int x;
struct m65_tm tm, tm2;
char msg[80];

unsigned char sin_table[32] = {
  //  128,177,218,246,255,246,218,177,
  //  128,79,38,10,0,10,38,79
  128, 152, 176, 198, 217, 233, 245, 252, 255, 252, 245, 233, 217, 198, 176, 152, 128, 103, 79, 57, 38, 22, 10, 3, 1, 3, 10,
  22, 38, 57, 79, 103
};

void play_sine(unsigned char ch, unsigned long time_base)
{
  unsigned ch_ofs = ch << 4;

  if (ch > 3)
    return;

  // Play sine wave for frequency matching
  POKE(0xD721 + ch_ofs, ((unsigned short)&sin_table) & 0xff);
  POKE(0xD722 + ch_ofs, ((unsigned short)&sin_table) >> 8);
  POKE(0xD723 + ch_ofs, 0);
  POKE(0xD72A + ch_ofs, ((unsigned short)&sin_table) & 0xff);
  POKE(0xD72B + ch_ofs, ((unsigned short)&sin_table) >> 8);
  POKE(0xD72C + ch_ofs, 0);
  // 16 bytes long
  POKE(0xD727 + ch_ofs, ((unsigned short)&sin_table + 32) & 0xff);
  POKE(0xD728 + ch_ofs, ((unsigned short)&sin_table + 32) >> 8);
  // 1/4 Full volume
  POKE(0xD729 + ch_ofs, 0x3F);
  // Enable playback+looping of channel 0, 8-bit samples, signed
  POKE(0xD720 + ch_ofs, 0xE2);
  // Enable audio dma
  POKE(0xD711 + ch_ofs, 0x80);

  // time base = $001000
  POKE(0xD724 + ch_ofs, time_base & 0xff);
  POKE(0xD725 + ch_ofs, time_base >> 8);
  POKE(0xD726 + ch_ofs, time_base >> 16);
}

void audioxbar_setcoefficient(uint8_t n, uint8_t value)
{
  // Select the coefficient
  POKE(0xD6F4, n);

  // Now wait at least 16 cycles for it to settle
  POKE(0xD020U, PEEK(0xD020U));
  POKE(0xD020U, PEEK(0xD020U));

  POKE(0xD6F5U, value);
}

unsigned char fast_flags = 0x70; // 0xb0;
unsigned char slow_flags = 0x00;
unsigned char cache_bit = 0x80; // =0x80;
unsigned long addr, upper_addr, time, speed;

unsigned char joya_up = 0, joyb_up = 0, joya_down = 0, joyb_down = 0;

void bust_cache(void)
{
  lpoke(0xbfffff2UL, fast_flags & (0xff - cache_bit));
  lpoke(0xbfffff2UL, fast_flags | cache_bit);
}

unsigned char check_sdram_speed()
{
  for (i = 0; i < 16; i++) {
    lpoke(0x8000000UL + i, i);
  }
  for (i = 0; i < 16; i++) {
    if (lpeek(0x8000000UL + i) != i)
      return 1;
  }
  return 0;
}

unsigned char attic_ram_test(unsigned char test_sdram)
{
  /*
   * Test AtticRAM
   *
   * mode 0 - HyperRAM
   * mode 1 - SDRAM
   *
   */
  addr = 0UL;
  if (!test_sdram)
    POKE(0xD7FEU, 0x00);
  else {
    // figure out which sdram mode to use
    POKE(0xD7FEU, 0x30);
    if (check_sdram_speed()) {
      POKE(0xD7FEU, 0x10);
      if (check_sdram_speed()) {
        return 1;
      }
    }
  }

  retries = 255;
  lpoke(0x8000000UL, 0xbd);
  while (lpeek(0x8000000UL) != 0xbd) {
    lpoke(0x8000000UL, 0xbd);
    retries--;
    if (!retries)
      return 2;
  }

  for (addr = 0x8001000UL; addr != (test_sdram ? 0xc000000UL : 0x9000000UL); addr += 0x1000UL) {
    // XXX There is still some cache consistency bugs,
    // so we bust the cache before checking various things
    bust_cache();
    if (lpeek(0x8000000UL) != 0xbd) {
      // Memory location didn't hold value
      return 3;
    }
    bust_cache();

    lpoke(addr, 0x55);

    bust_cache();
    i = lpeek(addr);
    if (i != 0x55) {
      if (test_sdram || ((addr != 0x8800000UL) && (addr != 0x9000000UL))) { // HyperRAM size detection
        return 4;
      }
      break;
    }
    bust_cache();

    lpoke(addr, 0xaa);

    bust_cache();
    i = lpeek(addr);
    if (i != 0xaa) {
      if (test_sdram || ((addr != 0x8800000UL) && (addr != 0x9000000UL))) { // HyperRAM size detection
        return 5;
      }
      break;
    }
  }

  // check invalid end adresses
  if ((addr != 0x8800000UL) && (addr != 0x9000000UL) && (addr != 0xc000000UL)) {
    return 6;
  }

  upper_addr = addr;

  lpoke(0xbfffff2, fast_flags | cache_bit);

  return 0;
}

unsigned char joy_test(
    unsigned char port, unsigned char maj, unsigned char min, unsigned char val, char y, char *dir, char *shortdir)
{
  unsigned char res = 0;

  snprintf(msg, 80, "TEST Joystick Port %d: Push %s", port ? 1 : 2, dir);
  print_text(0, y, 7, msg);
  snprintf(msg, 80, "joy %d-%s", port ? 1 : 2, shortdir);
  unit_test_set_current_name(msg);

  // Wait for joystick to go idle again
  usleep(50000l);
  while ((PEEK(0xDC00 + port) & 0x1f) != 0x1f);
  // Allow for de-bounce
  usleep(50000l);

  // we only accept single direction pushes!
  do {
    a = PEEK(0xDC00 + port) & 0x1f;
  } while (a != 0x1e && a != 0x1d && a != 0x1b && a != 0x17 && a != 0x0f);

  if (a != val) {
    unit_test_report(maj, min, TEST_FAIL);
    res = 1;
  }
  else {
    unit_test_report(maj, min, TEST_PASS);
  }

  // Wait for joystick to go idle again
  while ((PEEK(0xDC00 + port) & 0x1f) != 0x1f);
  // Allow for de-bounce
  usleep(50000l);

  return res;
}

unsigned char errs = 0;

unsigned char rtc_bad = 1;
unsigned char frame_prev, frame_count, frame_num;

void test_rtc(void)
{
  getrtc(&tm);
  frame_prev = PEEK(0xD7FA);
  frame_count = 0;
  rtc_bad = 1;
  unit_test_set_current_name("rtc ticks");
  while (rtc_bad) {
    // snprintf(msg,80,"%d frames, %d vs %d seconds.   ",frame_count,tm.tm_sec,tm2.tm_sec);
    // print_text(0, 13, 1, msg);

    frame_num = PEEK(0xD7FA);
    if (frame_num != frame_prev)
      frame_count += (frame_num - frame_prev);
    frame_prev = frame_num;
    if (frame_count > 52) {
      rtc_bad = 1;
      break;
    }
    getrtc(&tm2);
    if (tm.tm_sec != tm2.tm_sec) {
      // Reset frame counter on first tick
      if (frame_count < 48) {
        frame_count = 0;
        tm = tm2;
      }
      else if (frame_count < 52) {
        // 48--52 frames per tick = seems to be ticking right
        rtc_bad = 0;
        break;
      }
    }
  }
}

void main(void)
{
  unsigned char fails = 0, target;

  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // PAL mode for 50Hz video
  POKE(0xD06F, 0x00);
  POKE(0xD072, 0x00);
  POKE(0xD048, 0x68);
  POKE(0xD049, 0x0 | (PEEK(0xD049) & 0xf0));
  POKE(0xD04A, 0xF8);
  POKE(0xD04B, 0x1 | (PEEK(0xD04B) & 0xf0));
  POKE(0xD04E, 0x68);
  POKE(0xD04F, 0x0 | (PEEK(0xD04F) & 0xf0));
  POKE(0xD072, 0);
  // switch CIA TOD 50/60
  POKE(0xDD0E, PEEK(0xDC0E) | 0x80);
  POKE(0xDD0E, PEEK(0xDD0E) | 0x80);

  // Floppy motor on
  POKE(0xD080, 0x60);

  // Reset ethernet controller
  POKE(0xD6E0, 0);
  POKE(0xD6E0, 3);

  // Stop all DMA audio first
  POKE(0xD720, 0);
  POKE(0xD730, 0);
  POKE(0xD740, 0);
  POKE(0xD750, 0);

  // Audio cross-bar to full channel seperation
  for (i = 0; i < 4; i++) {
    audioxbar_setcoefficient(0x00 + i, i < 2 ? 0xff : 0x00);
    audioxbar_setcoefficient(0x10 + i, i < 2 ? 0xff : 0x00);
    audioxbar_setcoefficient(0xc0 + i, i < 2 ? 0xff : 0x00);
    audioxbar_setcoefficient(0xd0 + i, i < 2 ? 0xff : 0x00);
    audioxbar_setcoefficient(0x20 + i, i < 2 ? 0x00 : 0xff);
    audioxbar_setcoefficient(0x30 + i, i < 2 ? 0x00 : 0xff);
    audioxbar_setcoefficient(0xe0 + i, i < 2 ? 0x00 : 0xff);
    audioxbar_setcoefficient(0xf0 + i, i < 2 ? 0x00 : 0xff);
  }
  // master full power
  audioxbar_setcoefficient(0x1e, 0xff);
  audioxbar_setcoefficient(0x1f, 0xff);
  audioxbar_setcoefficient(0x3e, 0xff);
  audioxbar_setcoefficient(0x3f, 0xff);
  audioxbar_setcoefficient(0xde, 0xff);
  audioxbar_setcoefficient(0xdf, 0xff);
  audioxbar_setcoefficient(0xfe, 0xff);
  audioxbar_setcoefficient(0xff, 0xff);

  graphics_mode();
  graphics_clear_double_buffer();

  // set rtc, so it can tick
  tm.tm_sec = 1;
  tm.tm_min = 2;
  tm.tm_hour = 3;
  tm.tm_mday = 1;
  tm.tm_mon = 3;
  tm.tm_year = 2024-1900;
  setrtc(&tm);

  target = detect_target();
  print_text(0, 0, 1, "MEGA65 R3+ PCB Production Test V3");
  snprintf(msg, 80, "Hardware model: %s ($%02x)", get_model_name(target), target);
  print_text(0, 1, 1, msg);

  unit_test_setup("prodtest", 0);

  floppy_interval_first = PEEK(0xD6AA);

  // Draw colour bars
  for (x = 0; x < 640; x++) {
    for (y = 150; y < 200; y++) {
      plot_pixel(x, y, (x / 40) & 0xf);
    }
  }
  activate_double_buffer();

  POKE(0xD020, 1);
  unit_test_set_current_name("hyperram");
  if ((a = attic_ram_test(0))) {
    print_text(0, test_line++, 2, "FAIL HyperRAM Probe");
    unit_test_report(2, 1, TEST_FAIL);
    fails++;
  }
  else {
    print_text(0, test_line++, 5, "PASS HyperRAM Probe");
    unit_test_report(2, 1, TEST_PASS);
  }
  snprintf(msg, 80, "%08lx %d %02x %02x", addr, a, i, retries);
  print_text(0, 15, 12, msg);
  if (target < 4 || target > 10)
    print_text(0, test_line++, 7, "SKIP SDRAM Probe (unsupported)");
  else {
    unit_test_set_current_name("sdram");
    if ((a = attic_ram_test(1))) {
      print_text(0, test_line++, 2, "FAIL SDRAM Probe");
      unit_test_report(2, 2, TEST_FAIL);
      fails++;
    }
    else {
      print_text(0, test_line++, 5, "PASS SDRAM Probe");
      unit_test_report(2, 2, TEST_PASS);
    }
    snprintf(msg, 80, "%08lx %d %02x %02x", addr, a, i, retries);
    print_text(0, 16, 12, msg);
  }

  // Internal floppy connector
  POKE(0xD020, 3);
  if (PEEK(0xD6AA) != floppy_interval_first)
    floppy_active = 1;
  unit_test_set_current_name("floppy");
  if (!floppy_active) {
    print_text(0, test_line++, 2, "FAIL Floppy (is a disk inserted?)");
    unit_test_report(3, 1, TEST_FAIL);
    fails++;
  }
  else {
    print_text(0, test_line++, 5, "PASS Floppy                          ");
    unit_test_report(3, 1, TEST_PASS);
  }

  // Floppy motor off
  POKE(0xD080, 0x0);

  // Try toggling the IEC lines to make sure we can pull CLK and DATA low.
  // (ATN can't be read.)
  POKE(0xD020, 4);
  POKE(0xDD00, 0xC3); // let the lines float high
  v = PEEK(0xDD00);
  usleep(1000);
  if ((v & 0xc0) == 0xc0) {
    POKE(0xDD00, 0x13); // pull $40 low via $10
    usleep(1000);
    if ((PEEK(0xDD00) & 0xc0) == 0x80) {
      POKE(0xDD00, 0x23); // pull $80 low via $20
      usleep(1000);
      if ((PEEK(0xDD00) & 0xc0) == 0x40) {
        POKE(0xDD00, 0x33); // pull $80 and $40 low via $30
        usleep(1000);
        unit_test_set_current_name("iec c+d both");
        if ((PEEK(0xDD00) & 0xc0) == 0x00) {
          iec_pass = 1;
          unit_test_report(4, 1, TEST_PASS);
        }
        else {
          print_text(0, test_line, 2, "FAIL IEC CLK+DATA (CLK+DATA)");
          unit_test_report(4, 1, TEST_FAIL);
          fails++;
        }
        unit_test_set_current_name("iec c+d clk");
        unit_test_report(4, 2, TEST_PASS);
      }
      else {
        unit_test_set_current_name("iec c+d clk");
        unit_test_report(4, 2, TEST_FAIL);
        fails++;
        print_text(0, test_line, 2, "FAIL IEC CLK+DATA (CLK)");
      }
      unit_test_set_current_name("iec c+d data");
      unit_test_report(4, 3, TEST_PASS);
    }
    else {
      unit_test_set_current_name("iec c+d data");
      unit_test_report(4, 3, TEST_FAIL);
      fails++;
      print_text(0, test_line, 2, "FAIL IEC CLK+DATA (DATA)");
    }
    unit_test_set_current_name("iec c+d float");
    unit_test_report(4, 4, TEST_PASS);
  }
  else {
    snprintf(msg, 80, "iec c+d fl-$%02x", v);
    unit_test_report(4, 4, TEST_FAIL);
    fails++;
    snprintf(msg, 80, "FAIL IEC CLK+DATA (float $%02x)", v);
    print_text(0, test_line, 2, msg);
  }
  unit_test_set_current_name("iec c+d all");
  if (iec_pass) {
    unit_test_report(4, 5, TEST_PASS);
    print_text(0, test_line, 5, "PASS IEC CLK+DATA                     ");
  }
  else {
    unit_test_report(4, 5, TEST_FAIL);
    fails++;
  }
  test_line++;

  // Real-time clock
  POKE(0xD020, 5);
  test_rtc();

  if (rtc_bad == 0) {
    unit_test_report(5, 1, TEST_PASS);
    snprintf(msg, 80, "PASS RTC Ticks (%02d:%02d.%02d)   ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    print_text(0, test_line++, 5, msg);
  }
  else {
    // But try to set it running if it isn't running
    tm.tm_sec = 2;
    tm.tm_min = 3;
    tm.tm_hour = 4;
    tm.tm_mday = 2;
    tm.tm_mon = 3;
    tm.tm_year = 2024-1900;
    setrtc(&tm);

    // Now wait a couple of seconds and try again
    for (a = 0; a < 50; a++)
      usleep(50000l);

    POKE(0xD020, 6);
    test_rtc();

    getrtc(&tm);
    if (rtc_bad == 0) {
      unit_test_report(5, 1, TEST_PASS);
      snprintf(msg, 80, "PASS RTC Ticks (%02d:%02d.%02d)   ", tm.tm_hour, tm.tm_min, tm.tm_sec);
      print_text(0, test_line++, 5, msg);
    }
    else {
      unit_test_report(5, 1, TEST_FAIL);
      fails++;
      print_text(0, test_line++, 2, "FAIL RTC Not running             ");
    }
  }

  // Play two different tones out of the left and right speakers alternately
  POKE(0xD020, 2);

  unit_test_set_current_name("speaker left");
  print_text(0, test_line, 7, "TEST Left speaker (P=PASS,F=FAIL)");
  play_sine(0, 2000);
  play_sine(3, 1);

  while (PEEK(0xD610))
    POKE(0xD610, 0);

  while (!PEEK(0xD610))
    POKE(0xD020, PEEK(0xD020 + 1));
  switch (PEEK(0xD610)) {
  case 0x50:
  case 0x70:
    unit_test_report(6, 1, TEST_PASS);
    print_text(0, test_line, 5, "PASS Left speaker                ");
    break;
  default:
    unit_test_report(6, 1, TEST_FAIL);
    print_text(0, test_line, 2, "FAIL Left speaker                ");
    fails++;
  }
  POKE(0xD610, 0);
  test_line++;

  unit_test_set_current_name("speaker right");
  print_text(0, test_line, 7, "TEST Right speaker (P=PASS,F=FAIL)");
  play_sine(0, 1);
  play_sine(3, 3000);
  while (!PEEK(0xD610))
    POKE(0xD020, PEEK(0xD020 + 1));
  switch (PEEK(0xD610)) {
  case 0x50:
  case 0x70:
    unit_test_report(6, 2, TEST_PASS);
    print_text(0, test_line, 5, "PASS Right speaker                ");
    break;
  default:
    unit_test_report(6, 2, TEST_FAIL);
    print_text(0, test_line, 2, "FAIL Right speaker                ");
    fails++;
  }
  POKE(0xD610, 0);
  test_line++;

  // Turn off sound after
  play_sine(0, 1);
  play_sine(3, 1);

  // Stop all DMA audio first
  POKE(0xD720, 0);
  POKE(0xD730, 0);
  POKE(0xD740, 0);
  POKE(0xD750, 0);

  do {
    errs = 0;
    errs += joy_test(1, 7, 1, 0x1b, test_line, "LEFT ", "l");
    errs += joy_test(1, 7, 2, 0x17, test_line, "RIGHT", "r");
    errs += joy_test(1, 7, 3, 0x1e, test_line, "UP   ", "u");
    errs += joy_test(1, 7, 4, 0x1d, test_line, "DOWN ", "d");
    errs += joy_test(1, 7, 5, 0x0f, test_line, "FIRE ", "f");

    errs += joy_test(0, 8, 1, 0x1b, test_line, "LEFT ", "l");
    errs += joy_test(0, 8, 2, 0x17, test_line, "RIGHT", "r");
    errs += joy_test(0, 8, 3, 0x1e, test_line, "UP   ", "u");
    errs += joy_test(0, 8, 4, 0x1d, test_line, "DOWN ", "d");
    errs += joy_test(0, 8, 5, 0x0f, test_line, "FIRE ", "f");

    // XXX Test POT lines

    i = 0;
    if (errs) {
      print_text(0, test_line, 10, "FAILED! press F1 to restart     ");
      while (PEEK(0xD610U)) {
        POKE(0xD610U, 0);
      }
      while (!(i = PEEK(0xd610U)));
      POKE(0xD610U, 0);
    }
  } while (i == 0xf1);

  if (errs) {
    snprintf(msg, 80, "FAIL Joysticks (%d errors)      ", errs);
    print_text(0, test_line, 2, msg);
    fails++;
  }
  else {
    snprintf(msg, 80, "PASS Joysticks                 ", errs);
    print_text(0, test_line, 5, msg);
  }
  test_line++;

  // Don't test ethernet, as we test it in ethtest.prg instead now
#if 0
    // Ethernet controller
    POKE(0xD020, 6);
    
    if (eth_pass)
      print_text(0, 5, 5, "PASS Ethernet frame RX");
    else
      print_text(0, 5, 2, "FAIL Ethernet frame RX");

    if (PEEK(0xD6E1) & 0x20)
      eth_pass = 1;
#endif

  unit_test_set_current_name("r6prodtest");
  unit_test_report(10, 1, TEST_DONEALL);

  print_text(0, 17, fails > 0 ? 2 : 5, "ALL TESTS COMPLETE");

  while (1)
    continue;

  //  gap_histogram();
  // read_all_sectors();
}
