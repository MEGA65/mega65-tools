/*
  16-colour sprites with 16-bit sprite pointers result in wrong colours for
  sprite pixels of sprites below the top-most sprite (see issue #332 with a
  video.

  This programme attempts to reproduce this problem.
*/
#define ISSUE_NUM 332
#define ISSUE_NAME "16-colour sprite transparency"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <memory.h>
#include <tests.h>

char msg[64 + 1];

unsigned short i, j, pixel_addr;
unsigned char a, b, c, d, char_code;

void init_mega65(void)
{
  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // Stop all DMA audio first
  POKE(0xD720, 0);
  POKE(0xD730, 0);
  POKE(0xD740, 0);
  POKE(0xD750, 0);
}

unsigned char state[17];

void h640_text_mode(void)
{
  // save state
  state[0] = PEEK(0xD018);
  state[1] = PEEK(0xD054);
  state[2] = PEEK(0xD031);
  state[3] = PEEK(0xD016);
  state[4] = PEEK(0xD058);
  state[5] = PEEK(0xD059);
  state[6] = PEEK(0xD05E);
  state[7] = PEEK(0xD060);
  state[8] = PEEK(0xD061);
  state[9] = PEEK(0xD062);
  state[10] = PEEK(0xD05D);
  state[11] = PEEK(0xD020);
  state[12] = PEEK(0xD021);
  state[13] = PEEK(0xD06B);
  state[14] = PEEK(0xD06C);
  state[15] = PEEK(0xD06D);
  state[16] = PEEK(0xD06E);

  // lower case
  POKE(0xD018, 0x16);

  // Normal text mode
  POKE(0xD054, 0x00);
  // H640, fast CPU, extended attributes
  POKE(0xD031, 0xE0);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016, 0xC9);
  // 640x200 16bits per char, 16 pixels wide per char
  // = 640/16 x 16 bits = 80 bytes per row
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  // Draw 80 chars per row
  POKE(0xD05E, 80);
  // Put 2KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  lfill(0xc000, 0x20, 2000);

  // Clear colour RAM
  lfill(0xff80000L, 0x0E, 2000);
  // Disable hot registers
  POKE(0xD05D, PEEK(0xD05D) & 0x7f);

  // light grey background, black frame
  POKE(0xD020, 0);
  POKE(0xD021, 0x0b);
}

void restore_graphics(void)
{
  // restore saved state
  POKE(0xD05D, state[10]);
  POKE(0xD018, state[0]);
  POKE(0xD054, state[1]);
  POKE(0xD031, state[2]);
  POKE(0xD016, state[3]);
  POKE(0xD058, state[4]);
  POKE(0xD059, state[5]);
  POKE(0xD05E, state[6]);
  POKE(0xD060, state[7]);
  POKE(0xD061, state[8]);
  POKE(0xD062, state[9]);
  POKE(0xD020, state[11]);
  POKE(0xD021, state[12]);
  POKE(0xD06B, state[13]);
  POKE(0xD06C, state[14]);
  POKE(0xD06D, state[15]);
  POKE(0xD06E, state[16]);
  POKE(0xD015, 0x0);
  POKE(0xD018, 0x14);
  POKE(0xD07F, 0x7f);
}

struct {
  unsigned char r, g, b;
} palette[256];

void fetch_palette(void)
{
  unsigned short i;
  unsigned char c;

  // select palette
  POKE(0xD070, 0xff);

  for (i = 0; i < 256; i++) {
    c = PEEK(0xD100 + i);
    c = ((c & 0xf) << 4) | ((c & 0xf0) >> 4);
    palette[i].r = c;
    c = PEEK(0xD200 + i);
    c = ((c & 0xf) << 4) | ((c & 0xf0) >> 4);
    palette[i].g = c;
    c = PEEK(0xD300 + i);
    c = ((c & 0xf) << 4) | ((c & 0xf0) >> 4);
    palette[i].b = c;
  }
}

void read_pixel(short x, short y, unsigned char *red, unsigned char *green, unsigned char *blue)
{
  unsigned char frame_num;
  // Select (128,128) as pixel to read back
  POKE(0xD07D, x);
  POKE(0xD07E, y);
  POKE(0xD07F, (x >> 8) + ((y >> 8) << 4));

  // Wait at least one whole frame
  frame_num = PEEK(0xD7FA);
  while (PEEK(0xD7FA) == frame_num)
    continue;
  frame_num = PEEK(0xD7FA);
  while (PEEK(0xD7FA) == frame_num)
    continue;

  POKE(0xD07C, 0x52);
  *red = PEEK(0xD07D);
  POKE(0xD07C, 0x92);
  *green = PEEK(0xD07D);
  POKE(0xD07C, 0xD2);
  *blue = PEEK(0xD07D);
}

void print_text80(unsigned char x, unsigned char y, unsigned char colour, char *msg)
{
  pixel_addr = 0xC000 + x + y * 80;
  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    else if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    else if (*msg >= 0x60 && *msg <= 0x7A)
      char_code = *msg - 0x20;
    POKE(pixel_addr + 0, char_code);
    lpoke(0xff80000L - 0xc000 + pixel_addr, colour);
    msg++;
    pixel_addr += 1;
  }
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

unsigned char keybuffer(unsigned char wait)
{
  unsigned char key = 0;
  // clear keyboard buffer
  while (PEEK(0xD610))
    POKE(0xD610, 0);

  if (wait) {
    while ((key = PEEK(0xD610)) == 0)
      ;
    POKE(0xD610, 0);
  }

  return key;
}

void main(void)
{
  unsigned char r1, g1, b1, r2, g2, b2;
  unsigned short line = 0, test_pass, sub = 1;

  init_mega65();
  h640_text_mode();
  keybuffer(0);

  snprintf(msg, 80, "unit test #%03d - %s", ISSUE_NUM, ISSUE_NAME);
  print_text80(0, line++, 7, msg);
  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  print_text80(40, 10, 1, "Overlapping 16-colour sprites");
  print_text80(40, 11, 1, "result in wrong colours.");
  print_text80(40, 13, 2, "Fault is outside lower right corner of");
  print_text80(40, 14, 2, "circles is filled with the edge colour.");
  print_text80(40, 16, 5, "Correct behaviour is the circles and");
  print_text80(40, 17, 5, "their edges are all symmetrical.");

  // fetch colour data
  fetch_palette();

  // Enable 16-bit sprite pointers
  POKE(0xD06E, 0x80);

  // Set sprite pointer address to $0340
  POKE(0xD06C, 0x40);
  POKE(0xD06D, 0x03);

  // Set transparency colour to 0 for all
  // Fault will not occur without this, as it is the transparency logic that was faulty
  for (i = 0; i < 8; i++) {
    POKE(0xD027 + i, 0);
  }

  // Set sprites 0 through 7 to use memory at $8000 ( = $200 x $40)
  for (i = 0; i < 8; i++) {
    POKE(0x0340 + i * 2, 0x00);
    POKE(0x0341 + i * 2, 0x02);
  }

  // Set 16-colour sprites
  POKE(0xD06B, 0xFF);

  // Make 16-colour sprites 16 pixels wide
  POKE(0xD057, 0xFF);

  // Draw some sprite data into $8000
  // Make a masked sprite shape so that we get transparent bits
  // Result should be a circle with the outside edge in a different colour.
  for (i = 0; i < 21; i++)
    for (j = 0; j < 16; j++) {
      POKE(0x8000 + i * 8 + (j >> 1), 0);
    }
  for (i = 0; i < 21; i++)
    for (j = 0; j < 16; j++) {
      if (((i - 11) * (i - 11) + (j - 8) * (j - 8)) <= 64) {
        if ((j & 1))
          POKE(0x8000 + i * 8 + (j >> 1), PEEK(0x8000 + i * 8 + (j >> 1)) | 0x06);
        else
          POKE(0x8000 + i * 8 + (j >> 1), PEEK(0x8000 + i * 8 + (j >> 1)) | 0x60);
      }
      if (((i - 11) * (i - 11) + (j - 8) * (j - 8)) <= 49) {
        if ((j & 1))
          POKE(0x8000 + i * 8 + (j >> 1), PEEK(0x8000 + i * 8 + (j >> 1)) | 0x01);
        else
          POKE(0x8000 + i * 8 + (j >> 1), PEEK(0x8000 + i * 8 + (j >> 1)) | 0x10);
      }
    }

  // Set sprite locations
  for (i = 0; i < 8; i++) {
    POKE(0xD000 + i * 2, 0x40 + 10 * i);
    POKE(0xD001 + i * 2, 0x70 + 10 * i);
  }

  // Enable all sprites
  POKE(0xD015, 0xFF);

  /*
    At this point, we are able to see the problem:
    Any pixel in a sprite which is in front of another sprite is displayed,
    but it seems the X counter of that sprite is not advanced, and thus the same
    pixel value smears across to the right edge of the sprite that is in front.

    This is visible by the outside edge of the bottom corner of each sprite extending
    to the right-hand edge of the sprite.

    AH! It looks like the transparent pixels are still being marked as set by the
    foreground sprite, so when the background sprite is behind it, the colour of the
    (supposedly transparent) foreground sprite pixel is used instead.

  */

  // fix last corner colour to 0xb (background)
  palette[8 * 16 + 7].r = palette[0xb].r;
  palette[8 * 16 + 7].g = palette[0xb].g;
  palette[8 * 16 + 7].b = palette[0xb].b;

  // check pixels and report back
  for (i = 0; i < 8; i++) {
    read_pixel(183 + 20 * i, 188 + 20 * i, &r1, &g1, &b1);
    read_pixel(198 + 20 * i, 203 + 20 * i, &r2, &g2, &b2);
    if (palette[i * 16 + 7].r == r1 && palette[i * 16 + 7].g == g1 && palette[i * 16 + 7].b == b1) {
      if (palette[(i + 1) * 16 + 7].r == r2 && palette[(i + 1) * 16 + 7].g == g2 && palette[(i + 1) * 16 + 7].b == b2) {
        test_pass = 1;
        snprintf(msg, 80, "sprite %d: colours as expected", i + 1);
      }
      else {
        test_pass = 0;
        snprintf(msg, 80, "sprite %d: corner colour wrong", i + 1);
      }
    }
    else {
      if (palette[(i + 1) * 16 + 7].r == r2 && palette[(i + 1) * 16 + 7].g == g2 && palette[(i + 1) * 16 + 7].b == b2) {
        test_pass = 0;
        snprintf(msg, 80, "sprite %d: center colour wrong, corner ok (check display)", i + 1);
      }
      else {
        test_pass = 0;
        snprintf(msg, 80, "sprite %d: all colours wrong (check display)", i + 1);
      }
    }
    print_text80(0, line++, test_pass ? 5 : 2, msg);
    unit_test_log(msg);
    unit_test_report(ISSUE_NUM, sub++, test_pass ? TEST_PASS : TEST_FAIL);
  }
  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
  keybuffer(1);
  restore_graphics();
}
