#include <stdio.h>
#include <memory.h>
#include <targets.h>
#include <time.h>

struct m65_tm tm;

unsigned char joy_x = 100;
unsigned char joy_y = 100;

void m65_io_enable(void)
{
  // Gate C65 IO enable
  POKE(0xd02fU, 0x47);
  POKE(0xd02fU, 0x53);
  // Force to full speed
  POKE(0, 65);
}

void wait_10ms(void)
{
  // 16 x ~64usec raster lines = ~1ms
  int c = 160;
  unsigned char b;
  while (c--) {
    b = PEEK(0xD012U);
    while (b == PEEK(0xD012U))
      continue;
  }
}

unsigned char sprite_data[63] = { 0xff, 0, 0, 0xe0, 0, 0, 0xb0, 0, 0, 0x98, 0, 0, 0x8c, 0, 0, 0x86, 0, 0, 0x83, 0, 0, 0x81,
  0x80, 0,

  0, 0xc0, 0, 0, 0x60, 0, 0, 0x30, 0, 0, 0x18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

short x, y, z;
short a1, a2, a3;
unsigned char n = 0;

void main(void)
{

  m65_io_enable();

  printf("%c%cMEGA65 ROM Patch Loader v0.1\n", 0x93, 0x05);
}
