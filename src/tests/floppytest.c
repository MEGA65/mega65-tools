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

void get_interval(void)
{
  // Make sure we start measuring a fresh interval
  a = PEEK(0xD6AA);
  while (a == PEEK(0xD6AA))
    continue;

  do {
    a = PEEK(0xD6A9);
    b = PEEK(0xD6AA);
    c = PEEK(0xD6A9);
    d = PEEK(0xD6AA);
  } while (a != c || b != d);
  interval_length = a + ((b & 0xf) << 8);
}

void graphics_clear_screen(void)
{
  lfill(0x40000, 0, 32768);
  lfill(0x48000, 0, 32768);
}

void graphics_clear_double_buffer(void)
{
  lfill(0x50000, 0, 32768);
  lfill(0x58000, 0, 32768);
}

void graphics_mode(void)
{
  // 16-bit text mode, full-colour text for high chars
  POKE(0xD054, 0x05);
  // H640, fast CPU
  POKE(0xD031, 0xC0);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016, 0xC9);
  // 640x200 16bits per char, 16 pixels wide per char
  // = 640/16 x 16 bits = 80 bytes per row
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  // Draw 40 (double-wide) chars per row
  POKE(0xD05E, 40);
  // Put 2KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  // Layout screen so that graphics data comes from $40000 -- $4FFFF

  i = 0x40000 / 0x40;
  for (a = 0; a < 40; a++)
    for (b = 0; b < 25; b++) {
      POKE(0xC000 + b * 80 + a * 2 + 0, i & 0xff);
      POKE(0xC000 + b * 80 + a * 2 + 1, i >> 8);

      i++;
    }

  // Clear colour RAM, while setting all chars to 4-bits per pixel
  for (i = 0; i < 2000; i += 2) {
    lpoke(0xff80000L + 0 + i, 0x08);
    lpoke(0xff80000L + 1 + i, 0x00);
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
void print_text(unsigned char x, unsigned char y, unsigned char colour, char* msg)
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

unsigned char histo_bins[640];
char peak_msg[40 + 1];
unsigned char random_target = 40;
unsigned char last_random_target = 40;
unsigned int random_seek_count = 0;
unsigned char request_track = 40;
unsigned char read_sectors[41] = { 0 };
unsigned char last_track_seen = 255;
unsigned int histo_samples = 0;

void seek_random_track(void)
{
  // Seek to random track.
  last_random_target = random_target;
  random_target = PEEK(0xD012) % 80;
  POKE(0xD084, request_track);
  a = (PEEK(0xD6A3) & 0x7f) - random_target;
  if (a & 0x80) {
    while (a) {
      POKE(0xD081, 0x18);
      while (PEEK(0xD082) & 0x80)
        continue;
      a++;
    }
  }
  else {
    while (a) {
      POKE(0xD081, 0x10);
      while ((PEEK(0xD082) & 0x80))
        continue;
      //      usleep(6000);
      a--;
    }
  }
}

void gap_histogram(void)
{

  // Floppy motor on
  POKE(0xD080, 0x60);

  graphics_mode();

  print_text(0, 0, 1, "Magnetic Domain Interval Histogram");

  random_target = (PEEK(0xD6A3) & 0x7f);

  while (1) {
    // Clear histogram bins
    for (i = 0; i < 640; i++)
      histo_bins[i] = 0;
    histo_samples = 0;

    // Get new histogram data
    while (1) {
      get_interval();
      if (interval_length >= 640)
        continue;
      // Stop as soon as a single histogram bin fills
      if (histo_bins[interval_length] == 255) {
        snprintf(peak_msg, 40, "Peak @ %d, auto-tune=%d     ", interval_length, PEEK(0xD689) & 0x10);
        print_text(0, 2, 7, peak_msg);
        break;
      }
      histo_bins[interval_length]++;
      histo_samples++;
      if (histo_samples == 4096)
        break;
    }

    // Re-draw histogram.
    // We use 640x200 16-colour char mode
    graphics_clear_double_buffer();
    for (i = 0; i < 640; i++) {
      b = 5;
      if (histo_bins[i] > 128)
        b = 7;
      if (histo_bins[i] > 192)
        b = 10;
      for (a = 199 - (histo_bins[i] >> 1); a < 200; a++)
        plot_pixel(i, a, b);
    }

    snprintf(peak_msg, 40, "FDC Status = $%02X,$%02X, requested T:$%02x", PEEK(0xD082), PEEK(0xD083), request_track);
    print_text(0, 3, 7, peak_msg);
    snprintf(peak_msg, 40, "Last sector           T:$%02X S:%02X H:%02x", PEEK(0xD6A3), PEEK(0xD6A4), PEEK(0xD6A5));
    if ((PEEK(0xD6A3) & 0x7f) != last_track_seen) {
      last_track_seen = PEEK(0xD6A3) & 0x7f;
      // Zero out list of sectors seen
      snprintf(read_sectors, 41, "Sectors read:      ....................");
    }
    else {
      // Note this sector
      read_sectors[18 + (PEEK(0xD6A5) & 0x1) * 10 + (PEEK(0xD6A4) & 0x1f)] = 0x52;
      POKE(0xD080, PEEK(0xD080) & 0xf7 + (PEEK(0xD012) & 8));
    }
    read_sectors[40] = 0;
    print_text(0, 6, 5, read_sectors);

    print_text(0, 4, 7, peak_msg);
    snprintf(peak_msg, 40, "Target track %-5d is T:$%02X, prev $%02X", random_seek_count, random_target, last_random_target);
    print_text(0, 5, 7, peak_msg);

    if ((PEEK(0xD6A3) & 0x7f) == random_target) {
      random_seek_count++;
      seek_random_track();
    }

    activate_double_buffer();

    if (PEEK(0xD610)) {
      switch (PEEK(0xD610)) {
      case 0x11:
      case '-':
        POKE(0xD081, 0x10);
        break;
      case 0x91:
      case '+':
        POKE(0xD081, 0x18);
        break;
      case '0':
        request_track = 0;
        break;
      case '4':
        request_track = 40;
        break;
      case '8':
        request_track = 80;
        break;
      case '1':
        request_track = 81;
        break;
      case 0x9d:
        request_track--;
        break;
      case 0x1d:
        request_track++;
        break;
      case 0x20:
        last_random_target = random_target;
        random_target = 255;
        break;
      case 0x4D:
      case 0x6D:
        // Switch auto/manual tracking in FDC to manual
        POKE(0xD689, PEEK(0xD689) | 0x10);
        break;
      case 0x41:
      case 0x61:
        // Auto-tune on
        POKE(0xD689, PEEK(0xD689) & 0xEF);
        break;
      case 0x52:
      case 0x72:
        // Schedule a sector read
        POKE(0xD081, 0x00); // Cancel previous action

        // Select track, sector, side
        POKE(0xD084, request_track);
        POKE(0xD085, 1);
        POKE(0xD086, 0);

        // Issue read command
        POKE(0xD081, 0x40);

        break;
      case 0x53:
      case 0x73:
        random_seek_count = 0;
        seek_random_track();
        break;
      }
      POKE(0xD610, 0);
    }
  }
}

char read_message[41];

void read_all_sectors()
{
  unsigned char t, s, ss, h;
  unsigned char xx, yy, y;
  unsigned int x, c;

  // Floppy motor on
  POKE(0xD080, 0x68);

  // Enable auto-tracking
  POKE(0xD689, PEEK(0xD689) & 0xEF);

  // Map FDC sector buffer, not SD sector buffer
  POKE(0xD689, PEEK(0xD689) & 0x7f);

  // Disable matching on any sector, use real drive
  POKE(0xD6A1, 0x01);

  graphics_mode();
  graphics_clear_double_buffer();
  activate_double_buffer();

  // Wait until busy flag clears
  while (PEEK(0xD082) & 0x80) {
    snprintf(peak_msg, 40, "Sector under head T:$%02X S:%02X H:%02x", PEEK(0xD6A3), PEEK(0xD6A4), PEEK(0xD6A5));
    print_text(0, 24, 7, peak_msg);
    continue;
  }
  for (i = 0; i < 512; i++)
    POKE(0x8000 + i, PEEK(0xD087));
  //  lcopy(0xffd6000L,0x4e200L,0x200);

  while (1) {
    graphics_clear_double_buffer();
    print_text(0, 0, 1, "Reading all sectors...");
    print_text(0, 22, 15, "GREEN = good, RED = bad");
    print_text(0, 23, 15, "YELLOW = Track >80 bad");

    // Seek back to track 0
    while (!(PEEK(0xD082) & 1)) {
      POKE(0xD081, 0x10);
      usleep(6000);
    }

    if (PEEK(0xD610)) {
      POKE(0xD610, 0);
      break;
    }

    for (t = 0; t < 85; t++) {
      if (PEEK(0xD610))
        break;
      for (h = 0; h < 2; h++) {
        if (PEEK(0xD610))
          break;
        for (ss = 1; ss <= 10; ss++) {

          // Interleave reads, as by the time we have updated the display,
          // the drive is most likely already into the following sector.
          unsigned char sector_order[10] = { 1, 3, 5, 7, 9, 2, 4, 6, 8, 10 };
          s = sector_order[ss - 1];

          if (PEEK(0xD610))
            break;

          snprintf(read_message, 40, "Trying T:$%02x, S:$%02x, H:$%02x", t, s, h);
          print_text(0, 1, 7, read_message);

          // Schedule a sector read
          POKE(0xD081, 0x00); // Cancel previous action

          // Select track, sector, side
          POKE(0xD084, t);
          POKE(0xD085, s);
          POKE(0xD086, 0); // Always zero: only SIDE1 flag should be set for side 1

          // Select correct side of the disk
          if (h)
            POKE(0xD080, 0x68);
          else
            POKE(0xD080, 0x60);

          // Issue read command
          POKE(0xD081, 0x40);

          x = t * 7;
          y = 16 + (s - 1) * 8 + (h * 80);

          for (xx = 0; xx < 6; xx++)
            for (yy = 0; yy < 7; yy++)
              plot_pixel(x + xx, y + yy, 14);

          activate_double_buffer();

          // Give time for busy flag to assert
          usleep(1000);

          // Wait until busy flag clears
          while (PEEK(0xD082) & 0x80) {
            snprintf(peak_msg, 40, "Sector under head T:$%02X S:%02X H:%02x", PEEK(0xD6A3) & 0x7f, PEEK(0xD6A4) & 0x7f,
                PEEK(0xD6A5) & 0x7f);
            print_text(0, 24, 7, peak_msg);
            //	    lcopy(0xffd6000L,0x4e200L,0x200);
            continue;
          }
          if (PEEK(0xD082) & 0x10) {
            c = 2;
            if (t > 79)
              c = 7; // extra tracks aren't expected to be read
            for (xx = 0; xx < 6; xx++)
              for (yy = 0; yy < 7; yy++)
                plot_pixel(x + xx, y + yy, c);
          }
          else {
            c = 5;
            if (((t / 10) + h) & 1)
              c = 13;
            for (xx = 0; xx < 6; xx++)
              for (yy = 0; yy < 7; yy++)
                plot_pixel(x + xx, y + yy, c);
          }
          activate_double_buffer();
          //	  lcopy(0xffd6000L,0x4e200L,0x200);
        }
      }
    }
  }
}

void format_track(unsigned char track_number)
{
  // First seek to the correct track

  // Connect to real floppy drive
  while(!(lpeek(0xffd36a1L) & 1)) {
    lpoke(0xffd36a1L,lpeek(0xffd36a1L)|0x01);
  }
  
  // Floppy motor on
  POKE(0xD080, 0x68);

  // Enable auto-tracking
  POKE(0xD689, PEEK(0xD689) & 0xEF);

  // Map FDC sector buffer, not SD sector buffer
  POKE(0xD689, PEEK(0xD689) & 0x7f);

  // Disable matching on any sector, use real drive
  POKE(0xD6A1, 0x01);

  graphics_mode();
  graphics_clear_double_buffer();
  activate_double_buffer();

  // Wait until busy flag clears
  while (PEEK(0xD082) & 0x80) {
    snprintf(peak_msg, 40, "Sector under head T:$%02X S:%02X H:%02x", PEEK(0xD6A3), PEEK(0xD6A4), PEEK(0xD6A5));
    print_text(0, 24, 7, peak_msg);
    continue;
  }

  
  print_text(0, 0, 7, "Formatting track...");

  // Don't proceed without user's concent to ruin the disk in the drive
  print_text(0, 1, 2, "Insert sacraficial disk, then press *");
  while(PEEK(0xD610)!='*') {
    if (PEEK(0xD610)) {
      POKE(0xD610,0);
      return;
    }
  }

  POKE(0xD689,PEEK(0xD689)|0x10); // Disable auto-seek, or we can't force seeking to track 0
  
  // Seek to track 0
  print_text(0, 2, 15, "Seeking to track 0");
  while(!(PEEK(0xD082)&0x01)) {
    POKE(0xD081,0x10);
    usleep(20000);

    snprintf(peak_msg, 40, "Sector under head T:$%02X S:%02X H:%02x", PEEK(0xD6A3), PEEK(0xD6A4), PEEK(0xD6A5));
    print_text(0, 24, 7, peak_msg);
    
  }

  // Seek to the requested track
  print_text(0, 3, 15, "Seeking to target track");
  for(i=0;i<track_number;i++) {
    POKE(0xD081,0x18);
    usleep(20000);
    
  }
  
  // Wait for index sensor
  print_text(0, 4, 7, "Waiting for index sensor to pass by");
  while(PEEK(0xD083)&0x04) continue;
  while(!(PEEK(0xD083)&0x04)) continue;

  // OK: Now we are ready to do things
  
  /*
    From the C65 Specifications Manual:

    Write Track Unbuffered

           write FF hex to clock register
           issue "write track unbuffered" command
           write FF hex to data register
           wait for first DRQ flag
           write A1 hex to data register
           write FB hex to clock register
           wait for next DRQ flag
           write A1 hex to data register
           wait for next DRQ flag
           write A1 hex to data register
           wait for next DRQ flag
           write FF hex to clock register
     loop: write data byte to the data register
           check BUSY flag for completion
           wait for next DRQ flag
           go to loop

Formatting a track

     In order to be able to read or write sectored data on a diskette,
the diskette MUST be properly formatted. If, for any reason, marks are
missing  or  have  improper  clocks,  track,  sector,  side, or length
information are incorrect,  or the CRC bytes are in error, any attempt
to  perform  a  sectored read or write operation will terminate with a
RNF error.

     Formatting  a  track  is  simply  writing a track with a strictly
specified  series  of  bytes.  A  given  track must be divided into an
integer number of sectors,  which are 128,  256,  512,  or  1024 bytes
long.  Each  sector  must  consist  of  the following information. All
clocks, are FF hex, where not specified.  Data and clock values are in
hexadecimal  notation.  Fill  any left-over bytes in the track with 4E
data.

  quan      data/clock      description
  ----      ----------      -----------
    12      00              gap 3*
    3       A1/FB           Marks
            FE              Header mark
            (track)         Track number
            (side)          Side number
            (sector)        Sector number
            (length)        Sector Length (0=128,1=256,2=512,3=1024)

    2       (crc)           CRC bytes
    23      4E              gap 2
    12      00              gap 2
    3       A1/FB           Marks
            FB              Data mark
 128,
    256,
    512, or
    1024    00              Data bytes (consistent with length)
    2       (crc)           CRC bytes
    24      4E              gap 3*

    * you may reduce the size of gap 3 to increase diskette capacity,
      however the sizes shown are suggested.


Generating the CRC

     The  CRC  is a sixteen bit value that must be generated serially,
one  bit  at  a  time.  Think of it as a 16 bit shift register that is
broken in two places. To CRC a byte of data, you must do the following
eight  times,  (once  for each bit) beginning with the MSB or bit 7 of
the input byte.

     1. Take the exclusive OR of the MSB of the input byte and CRC
        bit 15. Call this INBIT.
     2. Shift the entire 16 bit CRC left (toward MSB) 1 bit position,
        shifting a 0 into CRC bit 0.
     3. If INBIT is a 1, toggle CRC bits 0, 5, and 12.

     To  Generate a CRC value for a header,  or for a data field,  you
must  first  initialize the CRC to all 1's (FFFF hex).  Be sure to CRC
all bytes of the header or data field, beginning with the first of the
three  A1  marks,  and ending with the before the two CRC bytes.  Then
output  the  most  significant CRC byte (bits 8-15) and then the least
significant CRC byte  (bits 7-0).  You may also CRC the two CRC bytes.
If you do, the final CRC value should be 0.

     Shown below is an example of code required to CRC bytes of data.
;
; CRC a byte. Assuming byte to CRC in accumulator and cumulative
;             CRC value in CRC (lsb) and CRC+1 (msb).

        CRCBYTE LDX  #8          ; CRC eight bits
                STA  TEMP
        CRCLOOP ASL  TEMP        ; shift bit into carry
                JSR  CRCBIT      ; CRC it
                DEX
                BNE  CRCLOOP
                RTS

;
; CRC a bit. Assuming bit to CRC in carry, and cumulative CRC
;            value in CRC (lsb) and CRC+1 (msb).

       CRCBIT   ROR
                EOR CRC+1       ; MSB contains INBIT
                PHP
                ASL CRC
                ROL CRC+1       ; shift CRC word
                PLP
                BPL RTS
                LDA CRC         ; toggle bits 0, 5, and 12 if INBIT is 1.
                EOR #$21
                STA CRC
                LDA CRC+1
                EOR #$10
                STA CRC+1
       RTS      RTS

       

       From a practical perspective, we need to wait for the index pulse to
       pass, as the write gate gets cleared by edges on it.

       Then we can schedule an unbuffered format operation, and start feeding
       the appropriate data to the clock and data byte registers, interlocked
       by the DRQ signal.
	   
  */
   

}

void main(void)
{
  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // Black background, white text
  POKE(0x286,1);
  POKE(0xD020,0);
  POKE(0xD021,0);
  
  while (1) {
    POKE(0xD054, 0);
    POKE(0xD031, 0);
    POKE(0xD060, 0);
    POKE(0xD061, 0x04);
    POKE(0xD062, 0);
    POKE(0xD011, 0x1b);

    // XXX For development, always start this immediately
    format_track(39);
    
    printf("%cMEGA65 Floppy Test Utility.\n\n", 0x93);

    printf("1. MFM Histogram and seeking tests.\n");
    printf("2. Test all sectors on disk.\n");
    printf("3. Test formatting new directory track.\n");

    while (!PEEK(0xD610))
      continue;
    switch (PEEK(0xD610)) {
    case '1':
      POKE(0xD610, 0);
      gap_histogram();
      break;
    case '2':
      POKE(0xD610, 0);
      read_all_sectors();
      break;
    case '3':
      POKE(0xD610,0);
      format_track(39);
      break;
    }
  }
}
