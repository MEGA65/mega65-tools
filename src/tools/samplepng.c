#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PNG_DEBUG 3
#include <png.h>

#define MAXX 256
#define MAXY 256
unsigned char frame[MAXY][MAXX * 4];

int maxx = MAXX;
int maxy = MAXY;

int image_number = 0;

void write_image(int image_number);

unsigned char c64_palette[64] = { 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0xba, 0x13, 0x62, 0x00, 0x66, 0xad, 0xff,
  0x00, 0xbb, 0xf3, 0x8b, 0x00, 0x55, 0xec, 0x85, 0x00, 0xd1, 0xe0, 0x79, 0x00, 0xae, 0x5f, 0xc7, 0x00, 0x9b, 0x47, 0x81,
  0x00, 0x87, 0x37, 0x00, 0x00, 0xdd, 0x39, 0x78, 0x00, 0xb5, 0xb5, 0xb5, 0x00, 0xb8, 0xb8, 0xb8, 0x00, 0x0b, 0x4f, 0xca,
  0x00, 0xaa, 0xd9, 0xfe, 0x00, 0x8b, 0x8b, 0x8b, 0x00 };

int main(int argc, char** argv)
{

  int count = 0;
  int x, y, r, g, b;

  char line[1024];

  printf("Preparing image...\n");

  for (y = 0; y < MAXY; y++)
    for (x = 0; x < MAXX; x++) {
      int colour = (x / 16) + (y / 16) * 16;

      r = colour & 0xe0;
      g = (colour << 3) & 0xe0;
      b = (colour << 6) & 0xc0;

      if (colour < 16) {
        // Put C64 colours as the first 16
        r = (c64_palette[colour * 4 + 0] << 4) | (c64_palette[colour * 4 + 0] >> 4);
        g = (c64_palette[colour * 4 + 1] << 4) | (c64_palette[colour * 4 + 1] >> 4);
        b = (c64_palette[colour * 4 + 2] << 4) | (c64_palette[colour * 4 + 2] >> 4);
      }

      frame[y][x * 4 + 0] = r;
      frame[y][x * 4 + 1] = g;
      frame[y][x * 4 + 2] = b;

      frame[y][x * 4 + 3] = 0xff;
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
  FILE* f = fopen(filename, "wb");
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
