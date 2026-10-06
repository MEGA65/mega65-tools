/*
  Issue #873 / tools #239 - DMA Line Drawing calculated length is incorrect.

  Core PR #887 fixed an off-by-two in DMAgic line mode startup delay that
  shortened drawn lines. This unittest draws axis-aligned lines with an
  explicit pixel count and verifies endpoints (and near-endpoints) in the
  FCM framebuffer via lpeek.
*/
#define ISSUE_NUM 873
#define ISSUE_NAME "dma line drawing length"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <memory.h>
#include <tests.h>

/* FCM tile layout matching other line-draw tests (320x200, 8x8 tiles @ $40000) */
#define FCM_BASE 0x40000L
#define SCREEN_W 320
#define SCREEN_H 200
#define COLOUR_H 2
#define COLOUR_V 5

static unsigned char line_dmalist[256];
static unsigned char slope_ofs, line_mode_ofs, cmd_ofs, count_ofs;
static unsigned char src_ofs, dst_ofs;

static void init_mega65(void)
{
  /* Fast CPU, M65 IO */
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  /* Stop all DMA audio first */
  POKE(0xD720, 0);
  POKE(0xD730, 0);
  POKE(0xD740, 0);
  POKE(0xD750, 0);
}

static void graphics_mode(void)
{
  /* 16-bit text mode, full-colour text for high chars */
  POKE(0xD054, 0x05);
  /* H320, fast CPU */
  POKE(0xD031, 0x40);
  /* 320x200, 8 pixels wide per char => 40 chars * 2 bytes = 80 bytes/row */
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  POKE(0xD05E, 40);
  /* Screen at $C000 */
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  /* Map screen chars to FCM data at $40000 */
  {
    unsigned short i = (unsigned short)(FCM_BASE / 0x40);
    unsigned char a, b;
    for (a = 0; a < 40; a++) {
      for (b = 0; b < 25; b++) {
        POKE(0xC000 + b * 80 + a * 2 + 0, i & 0xff);
        POKE(0xC000 + b * 80 + a * 2 + 1, i >> 8);
        i++;
      }
    }
  }

  /* Clear colour RAM attributes */
  lfill(0xff80000L, 0x00, 2000);

  POKE(0xD020, 0);
  POKE(0xD021, 0);
}

static void graphics_clear(void)
{
  lfill(FCM_BASE, 0, 32000L);
  lfill(FCM_BASE + 32000L, 0, 32000L);
}

/* Pixel address in the vertical-stripe FCM layout used by DMA line mode */
static unsigned long pixel_addr(unsigned short x, unsigned short y)
{
  return FCM_BASE + ((unsigned long)(y) << 3) + (x & 7) + ((unsigned long)(x >> 3) * 64L * 25L);
}

static unsigned char read_fb_pixel(unsigned short x, unsigned short y)
{
  return lpeek(pixel_addr(x, y));
}

static void setup_line_dmalist(void)
{
  unsigned char ofs = 0;

  /* X column step for vertical-stripe FCM: 25*64 - 8 = 1592 */
  line_dmalist[ofs++] = 0x87;
  line_dmalist[ofs++] = (1600 - 8) & 0xff;
  line_dmalist[ofs++] = 0x88;
  line_dmalist[ofs++] = (1600 - 8) >> 8;

  /* Slope (unused for axis-aligned pure H/V with slope 0) */
  line_dmalist[ofs++] = 0x8b;
  slope_ofs = ofs++;
  line_dmalist[slope_ofs] = 0;
  line_dmalist[ofs++] = 0x8c;
  line_dmalist[ofs++] = 0;

  /* Line mode options */
  line_dmalist[ofs++] = 0x8f;
  line_mode_ofs = ofs++;

  /* F018A list format */
  line_dmalist[ofs++] = 0x0a;
  line_dmalist[ofs++] = 0x00; /* end of options */

  cmd_ofs = ofs++;
  count_ofs = ofs;
  ofs += 2;
  src_ofs = ofs;
  ofs += 3;
  dst_ofs = ofs;
  ofs += 3;
  line_dmalist[ofs++] = 0x00; /* modulo lo */
  line_dmalist[ofs++] = 0x00; /* modulo hi */
}

/*
 * Draw an axis-aligned line with DMA line mode.
 * count: number of pixels to plot (the qty field under test for #873)
 * line_opts: bits for option 0x8f (0x80 enable line mode, 0x40 Y major,
 *            0x20 minor axis negative)
 */
static void dma_line_fill(unsigned short x, unsigned short y, unsigned short count,
                          unsigned char colour, unsigned char line_opts)
{
  unsigned long addr = pixel_addr(x, y);

  line_dmalist[slope_ofs] = 0;
  line_dmalist[slope_ofs + 2] = 0;

  line_dmalist[dst_ofs + 0] = (unsigned char)(addr & 0xff);
  line_dmalist[dst_ofs + 1] = (unsigned char)((addr >> 8) & 0xff);
  line_dmalist[dst_ofs + 2] = (unsigned char)((addr >> 16) & 0x0f);

  line_dmalist[src_ofs] = colour & 0x0f;
  line_dmalist[src_ofs + 1] = 0;
  line_dmalist[src_ofs + 2] = 0;

  line_dmalist[count_ofs] = (unsigned char)(count & 0xff);
  line_dmalist[count_ofs + 1] = (unsigned char)(count >> 8);

  line_dmalist[cmd_ofs] = 0x03; /* FILL */
  line_dmalist[line_mode_ofs] = line_opts;

  POKE(0xD701, ((unsigned int)(&line_dmalist)) >> 8);
  POKE(0xD705, ((unsigned int)(&line_dmalist)) >> 0);
}

/* Expect pixel set; report via unit test framework */
static void expect_set(unsigned short x, unsigned short y, unsigned char colour, char *msg)
{
  unsigned char p = read_fb_pixel(x, y);
  if (p == (colour & 0x0f)) {
    unit_test_ok(msg);
  }
  else {
    unit_test_fail(msg);
  }
}

/* Expect pixel still clear (0) */
static void expect_clear(unsigned short x, unsigned short y, char *msg)
{
  unsigned char p = read_fb_pixel(x, y);
  if (p == 0) {
    unit_test_ok(msg);
  }
  else {
    unit_test_fail(msg);
  }
}

void main(void)
{
  /* Horizontal line: y=100, x=0..319, qty=320
   * Pre-fix cores stopped two pixels early (x=318/319 missing). */
  const unsigned short hy = 100;
  const unsigned short hcount = SCREEN_W;

  /* Vertical line: x=160, y=0..199, qty=200 */
  const unsigned short vx = 160;
  const unsigned short vcount = SCREEN_H;

  /* Shorter checks isolate off-by-two without relying on screen edge */
  const unsigned short short_count = 50;
  const unsigned short sx = 10;
  const unsigned short sy = 10;

  asm("sei");
  init_mega65();

  printf("%c%c", 147, 5); /* clear screen; white */
  printf("issue #%d - %s\n", ISSUE_NUM, ISSUE_NAME);

  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  graphics_mode();
  graphics_clear();
  setup_line_dmalist();

  /* --- Test 1: full-width horizontal line length --- */
  /* line mode enable | X major | Y minor positive */
  dma_line_fill(0, hy, hcount, COLOUR_H, 0x80);

  expect_set(0, hy, COLOUR_H, "horiz start pixel set");
  expect_set((unsigned short)(hcount - 3), hy, COLOUR_H, "horiz near-end-2 set");
  expect_set((unsigned short)(hcount - 2), hy, COLOUR_H, "horiz near-end-1 set");
  expect_set((unsigned short)(hcount - 1), hy, COLOUR_H, "horiz end pixel set (qty exact)");

  /* --- Test 2: full-height vertical line length --- */
  graphics_clear();
  /* line mode enable | Y major | X minor positive */
  dma_line_fill(vx, 0, vcount, COLOUR_V, 0x80 + 0x40);

  expect_set(vx, 0, COLOUR_V, "vert start pixel set");
  expect_set(vx, (unsigned short)(vcount - 3), COLOUR_V, "vert near-end-2 set");
  expect_set(vx, (unsigned short)(vcount - 2), COLOUR_V, "vert near-end-1 set");
  expect_set(vx, (unsigned short)(vcount - 1), COLOUR_V, "vert end pixel set (qty exact)");

  /* --- Test 3: short horizontal (isolates off-by-two without screen edge) --- */
  graphics_clear();
  dma_line_fill(sx, sy, short_count, COLOUR_H, 0x80);

  expect_set(sx, sy, COLOUR_H, "short horiz start set");
  expect_set((unsigned short)(sx + short_count - 2), sy, COLOUR_H, "short horiz end-1 set");
  expect_set((unsigned short)(sx + short_count - 1), sy, COLOUR_H, "short horiz end set");
  expect_clear((unsigned short)(sx + short_count), sy, "short horiz past-end clear");
  expect_clear((unsigned short)(sx + short_count + 1), sy, "short horiz past-end+1 clear");

  /* --- Test 4: short vertical --- */
  graphics_clear();
  dma_line_fill(sx, sy, short_count, COLOUR_V, 0x80 + 0x40);

  expect_set(sx, sy, COLOUR_V, "short vert start set");
  expect_set(sx, (unsigned short)(sy + short_count - 2), COLOUR_V, "short vert end-1 set");
  expect_set(sx, (unsigned short)(sy + short_count - 1), COLOUR_V, "short vert end set");
  expect_clear(sx, (unsigned short)(sy + short_count), "short vert past-end clear");
  expect_clear(sx, (unsigned short)(sy + short_count + 1), "short vert past-end+1 clear");

  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}