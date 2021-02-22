#include <stdio.h>
#include <cbm.h>
#include "memory.h"

unsigned short i;
unsigned char a, b, c;

int main(void)
{
  // Fast CPU
  POKE(0, 64);

  POKE(0x286, 0x0e);
  printf("%c\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n", 0x93);

  // Disable cart first
  POKE(0xDE00, 0x40);

  // MEGA65 IO
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // Reset flash to read mode
  POKE(0xDE00, 0xC2);
  lpoke(0x701F555, 0xAA);
  POKE(0xDE00, 0xC1);
  lpoke(0x701EAAA, 0x55);
  POKE(0xDE00, 0xC2);
  lpoke(0x701F555, 0xF0);

#if 1
  // Do FLASH identify sequence
  POKE(0xDE00, 0xC2);
  lpoke(0x701F555, 0xAA);
  POKE(0xDE00, 0xC1);
  lpoke(0x701EAAA, 0x55);
  POKE(0xDE00, 0xC2);
  lpoke(0x701F555, 0x90);
#endif

  a = lpeek(0x7018000);
  b = lpeek(0x7018001);
  c = lpeek(0x7018002);

  lcopy(0x7018000, 0x0400, 0x100);

  printf("FLASH identification: %02x %02x %02x\n", a, b, c);

  // Allow cart write to finish before we fiddle with $DE00
  for (i = 0; i < 256; i++)
    continue;

  // Read or reset command
  POKE(0xDE00, 0xC0);
  lpoke(0x701e000, 0xf0);

  POKE(0xDE00, 0x00);

  a = lpeek(0x7018000);
  b = lpeek(0x7018001);
  c = lpeek(0x7018002);

  lcopy(0x7018000, 0x0400 + 8 * 40, 0x100);

  printf("First bytes of cart: %02x %02x %02x\n", a, b, c);

  // Some how we corrupt $Dxxx sometimes. Is it CC65 not liking the cart appearing at $8xxx?
  POKE(0xD011, 0x1b);
  POKE(0xD016, 0xC8);
}
