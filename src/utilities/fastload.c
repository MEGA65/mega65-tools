#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>

unsigned short i;
unsigned char a, b, c, d;
unsigned short interval_length;

char read_message[41];

unsigned char read_sector(unsigned t, unsigned char s, unsigned char h)
{

  POKE(0xD081, 0x00); // Cancel previous action

  // Wait until busy flag clears
  while (PEEK(0xD082) & 0x80) {
    continue;
  }

  // Schedule a sector read

  // Select track, sector, side
  POKE(0xD084, t);
  POKE(0xD085, s);
  POKE(0xD086, 0);

  // Select correct side of the disk
  if (h)
    POKE(0xD080, 0x68);
  else
    POKE(0xD080, 0x60);

  // Issue read command
  POKE(0xD081, 0x40);

  // Wait for busy to assert
  while (!PEEK(0xD082) & 0x80)
    continue;

  // Wait until busy flag clears
  while (PEEK(0xD082) & 0x80) {
    continue;
  }

  // XXX Why on earth do we need to wait after reading a sector?
  // The above loop should wait until read is fully complete.
  usleep(1000);

  return PEEK(0xD082);
}

unsigned char disk_header[25 + 1];
unsigned char file_count = 0;

void scan_directory(void)
{
  unsigned char t = 39, h = 1, s = 1;
  unsigned short half = 0, i;

  // Get directory header
  read_sector(t, s, h);

  // Get track and sector of next dir block
  t = lpeek(0xffd6000L);
  s = lpeek(0xffd6001L);
  if (s & 1)
    half = 0x100;
  else
    half = 0;
  t--;
  h = 1;
  if (s > 19) {
    s -= 20;
    h = 0;
  }
  s = s >> 1;
  s++;

  // Get disk name
  lcopy(0xffd6004L, disk_header, 25);

  printf("Disk: '%s'\n", disk_header);
  printf("First: T%d, S%d\n", lpeek(0xffd6000L), lpeek(0xffd6001L));

  //  while(1) continue;

  while (t && (t != 0xff)) {
    read_sector(t, s, h);

    lcopy(0xffd6000L + half, 0x0400, 0x100);

    for (i = 0; i < 256; i += 32) {
      if (lpeek(0xffd6002L + half + i) == 0x82) {
        lcopy(0xffd6002L + half + i, 0x40000L + (file_count * 32), 32);
        file_count++;
      }
      else
        printf("ignoring $%02x\n", lpeek(0xffd6002L + half + i));
    }

    printf("Next: T%d, S%d\n", lpeek(0xffd6000L + half), lpeek(0xffd6001L + half));

    t = lpeek(0xffd6000L + half);
    s = lpeek(0xffd6001L + half);
    if (s & 1)
      half = 0x100;
    else
      half = 0;
    t--;
    h = 1;
    if (s > 19) {
      s -= 20;
      h = 0;
    }
    s = s >> 1;
    s++;

    //    while(!PEEK(0xD610)) continue; POKE(0xD610,0);
  }

  printf("Found %d files\n", (int)file_count);
}

void load_file(unsigned char t, unsigned char s)
{
  unsigned char h = 0;
  unsigned short half = 0, i;

  printf("%c%c%c%c%c%c%c%c", 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11);

  printf("First: T%d, S%d\n", t, s);

  while (t && (t != 0xff)) {

    // Convert to physical track and sector
    if (s & 1)
      half = 0x100;
    else
      half = 0;
    t--;
    h = 1;
    if (s > 19) {
      s -= 20;
      h = 0;
    }
    s = s >> 1;
    s++;

    printf("Phys: T%d, S%d, H%d: ", t, s, h);
    read_sector(t, s, 1);

    lcopy(0xffd6000L + half, 0x0450, 0x100);

    printf("Next: T%d, S%d\n", lpeek(0xffd6000L + half), lpeek(0xffd6001L + half));

    t = lpeek(0xffd6000L + half);
    s = lpeek(0xffd6001L + half);

    while (!PEEK(0xD610))
      continue;
    POKE(0xD610, 0);
  }

  printf("Found %d files\n", (int)file_count);
}

char filename[17];

void main(void)
{
  int x, y;

  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // Make sure we are on track 79 or less, so that auto-tune can find tracks for us
  while (!(PEEK(0xD082) & 1)) {
    POKE(0xD081, 0x10);
    usleep(6000);
  }

  // Floppy motor on
  POKE(0xD080, 0x68);

  // Enable auto-tracking
  POKE(0xD689, PEEK(0xD689) & 0xEF);

  // Map FDC sector buffer, not SD sector buffer
  POKE(0xD689, PEEK(0xD689) & 0x7f);

  scan_directory();

  printf("%c", 0x93);
  for (i = 0; i < 38; i++) {
    y = i % 18;
    x = 0;
    if (i > 17)
      x = 20;
    printf("%c%c%c%c%c%c", 0x13, 0x11, 0x11, 0x11, 0x11, 0x11);
    while (y--)
      printf("%c", 0x11);
    while (x--)
      printf("%c", 0x1d);
    lcopy(0x40000 + i * 32 + 3, filename, 16);
    filename[16] = 0;
    if (i > file_count)
      filename[0] = 0;
    printf("%c. %s", (i < 26) ? 0x41 + i : 0x30 + (i - 26), filename);
  }

  while (1) {
    while (!PEEK(0xD610))
      continue;
    i = PEEK(0xD610);
    POKE(0x0402, i);
    if (i >= 0x61 && i <= 0x76)
      i = i - 0x61;
    else if (i >= 0x41 && i <= 0x56)
      i = i - 0x41;
    else if (i >= '0' && i <= '9')
      i -= 0x30 + 26;
    else {
      i = -1;
    }
    POKE(0xD610, 0);
    POKE(0x400, i);
    POKE(0x401, file_count);
    if (i < file_count) {
      lcopy(0x40000 + i * 32 + 3, filename, 16);
      filename[16] = 0;
      printf("%cLoading %s...\n", 0x93, filename);
      load_file(lpeek(0x40000 + i * 32 + 1), lpeek(0x40000 + i * 32 + 2));
      break;
    }
  }
}
