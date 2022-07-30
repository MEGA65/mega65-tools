#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PNG_DEBUG 3
#include <png.h>

#define MAXX 128
#define MAXY 88
unsigned char frame[MAXY][MAXX * 4];

int maxx = MAXX;
int maxy = MAXY;

int image_number = 0;

void write_image(int image_number);

unsigned char c64_palette[64] = { 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0xba, 0x13, 0x62, 0x00, 0x66, 0xad, 0xff,
  0x00, 0xbb, 0xf3, 0x8b, 0x00, 0x55, 0xec, 0x85, 0x00, 0xd1, 0xe0, 0x79, 0x00, 0xae, 0x5f, 0xc7, 0x00, 0x9b, 0x47, 0x81,
  0x00, 0x87, 0x37, 0x00, 0x00, 0xdd, 0x39, 0x78, 0x00, 0xb5, 0xb5, 0xb5, 0x00, 0xb8, 0xb8, 0xb8, 0x00, 0x0b, 0x4f, 0xca,
  0x00, 0xaa, 0xd9, 0xfe, 0x00, 0x8b, 0x8b, 0x8b, 0x00 };

unsigned char colour_table[256];

void make_colour_table(void)
{
  // Make colour lookup table
  unsigned char c = 0;
  do {
    colour_table[c] = c;
  } while (++c);

  // Now map C64 colours directly
  colour_table[0x00] = 0x20; // black   ($00 = transparent colour, so we have to use very-dim red instead)
  colour_table[0xff] = 0x01; // white
  colour_table[0xe0] = 0x02; // red
  colour_table[0x1f] = 0x03; // cyan
  colour_table[0xe3] = 0x04; // purple
  colour_table[0x1c] = 0x05; // green
  colour_table[0x03] = 0x06; // blue
  colour_table[0xfc] = 0x07; // yellow
  colour_table[0xec] = 0x08; // orange
  colour_table[0xa8] = 0x09; // brown
  colour_table[0xad] = 0x0a; // pink
  colour_table[0x49] = 0x0b; // grey1
  colour_table[0x92] = 0x0c; // grey2
  colour_table[0x9e] = 0x0d; // lt.green
  colour_table[0x93] = 0x0e; // lt.blue
  colour_table[0xb6] = 0x0f; // grey3

// We should also map colour cube colours 0x00 -- 0x0f to
// somewhere sensible.
// 0x00 = black, so can stay
#if 0
  colour_table[0x01]=0x06;  // dim blue -> blue
  // colour_table[0x02]=0x06;  // medium dim blue -> blue
  // colour_table[0x03]=0x06;  // bright blue -> blue
  colour_table[0x04]=0x00;  // dim green + no blue
  colour_table[0x05]=0x25;  
  colour_table[0x06]=0x26;  
  colour_table[0x07]=0x27;  
  colour_table[0x08]=0x28;  
  colour_table[0x09]=0x29;  
  colour_table[0x0A]=0x2a;  
  colour_table[0x0B]=0x2b;  
  colour_table[0x0C]=0x2c;  
  colour_table[0x0D]=0x2d;  
  colour_table[0x0E]=0x2e;  
  colour_table[0x0F]=0x2f;
#endif
}

void draw_pixel(int x, int y, int colour)
{
  colour = colour_table[colour];

  int r = colour & 0xe0;
  int g = (colour << 3) & 0xe0;
  int b = (colour << 6) & 0xc0;

#if 1
  if (colour < 16) {
    // Put C64 colours as the first 16
    r = (c64_palette[colour * 4 + 0] << 4) | (c64_palette[colour * 4 + 0] >> 4);
    g = (c64_palette[colour * 4 + 1] << 4) | (c64_palette[colour * 4 + 1] >> 4);
    b = (c64_palette[colour * 4 + 2] << 4) | (c64_palette[colour * 4 + 2] >> 4);
  }
#endif

  frame[y][x * 4 + 0] = r;
  frame[y][x * 4 + 1] = g;
  frame[y][x * 4 + 2] = b;

  frame[y][x * 4 + 3] = 0xff;
}

int main(int argc, char **argv)
{

  int count = 0;
  int x, y, r, g, b;

  char line[1024];

  make_colour_table();

  printf("Preparing image...\n");
  for (y = 0; y < MAXY; y++)
    for (x = 0; x < MAXX; x++)
      draw_pixel(x, y, 0x26); // Blue everywhere as in freeze menu

  FILE *f = fopen(argv[1], "r");
  unsigned char slot[1048576];
  int bytes = fread(slot, 1, 1048576, f);
  fclose(f);
  printf("Read Slot of %d bytes.\n", bytes);

  // Now draw the thumbnail
  for (y = 1; y < 47; y++)
    for (x = 6; x < 79; x++)
      draw_pixel(x + 24, y + 8, slot[0x6a000 + 2 + x + 80 * y]); // Blue everywhere as in freeze menu
  for (x = 5; x < 80; x++)
    draw_pixel(x + 24, 0 + 8, 1);
  for (x = 5; x < 80; x++)
    draw_pixel(x + 24, 47 + 8, 1);
  for (y = 1; y < 47; y++) {
    draw_pixel(5 + 24, y + 8, 1);
    draw_pixel(79 + 24, y + 8, 1);
  }

  printf("Writing image...\n");
  write_image(0);

  return 0;
}

void write_image(int image_number)
{
  int y;
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png)
    abort();

  png_infop info = png_create_info_struct(png);
  if (!info)
    abort();

  if (setjmp(png_jmpbuf(png)))
    abort();

  char filename[1024];
  snprintf(filename, 1024, "frame-%d.png", image_number);
  FILE *f = fopen(filename, "wb");
  if (!f)
    abort();

  png_init_io(png, f);

  png_set_IHDR(
      png, info, MAXX, MAXY, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_DEFAULT);

  png_write_info(png, info);

  for (y = 0; y < maxy; y++) {
    png_write_row(png, frame[y]);
  }
  unsigned char empty_row[MAXX * 4];
  bzero(empty_row, sizeof(empty_row));
  for (; y < MAXY; y++) {
    png_write_row(png, empty_row);
  }

  png_write_end(png, info);
  png_destroy_write_struct(&png, &info);

  fclose(f);

  return;
}
