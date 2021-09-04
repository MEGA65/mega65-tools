#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>

void readtrackgaps(void);

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

void text80x40_clear_screen(void)
{
  lfill(0xC000L, 0x20, 80*40);
  lfill(0xff80000L, 0xf, 80*40);
}

void text80x40_mode(void)
{
  // 8-bit text mode, normal chars
  POKE(0xD054, 0x00);
  // H640, V400, fast CPU
  POKE(0xD031, 0xC8);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016, 0xC9);
  // Advance 80 bytes per row of text
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  // Draw 80 chars
  POKE(0xD05E, 80);
  // Draw 40 rows
  POKE(0xD07B, 39);
  // Put ~3.5KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  POKE(0xD020, 0);
  POKE(0xD021, 0);

  text80x40_clear_screen();
}

void graphics_clear_screen(void)
{
  lfill(0x40000L, 0, 32768);
  lfill(0x48000L, 0, 32768);
}

void graphics_clear_double_buffer(void)
{
  lfill(0x50000L, 0, 32768);
  lfill(0x58000L, 0, 32768);
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
  // Draw 25 rows
  POKE(0xD07B, 24);
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

void print_text80x40(unsigned char x, unsigned char y, unsigned char colour, char* msg)
{
  pixel_addr = 0xC000 + x + y * 80;
  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    POKE(pixel_addr + 0, char_code);
    lpoke(0xff80000 - 0xc000 + pixel_addr + 0, colour);
    msg++;
    pixel_addr ++;
  }
}


void activate_double_buffer(void)
{
  lcopy(0x50000, 0x40000, 0x8000);
  lcopy(0x58000, 0x48000, 0x8000);
}

unsigned char histo_bins[640];
char peak_msg[80 + 1];
unsigned char random_target = 40;
unsigned char last_random_target = 40;
unsigned int random_seek_count = 0;
unsigned char request_track = 40;
unsigned char request_sector = 0;
unsigned char request_side = 0;
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
      a--;
    }
  }
}

char read_message[41];
unsigned char n,sector_order[10];

void read_all_sectors(unsigned char HD)
{
  unsigned char t, s, ss, h;
  unsigned char xx, yy, y;
  unsigned int x, c;

  if (HD) POKE(0xD6A2,0x28); else POKE(0xD6A2,0x51);
  
  // Floppy motor on
  POKE(0xD080, 0x68);

  // Disable auto-tracking
  POKE(0xD689, PEEK(0xD689) | 0x10);

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

  graphics_clear_double_buffer();
  
  {
    print_text(0, 0, 1, "Reading all sectors...");
    print_text(0, 22, 15, "GREEN = good, RED = bad");
    print_text(0, 23, 15, "YELLOW = Track >80 bad");

    // Seek back to track 0
    while (!(PEEK(0xD082) & 1)) {
      POKE(0xD081, 0x10);
      usleep(6000);
    }

    // Work out sector order
    n=0;
    c=PEEK(0xD6A4);
    while(c==PEEK(0xD6A4)) continue;
    for(n=0;n<(10*(1+HD));n++) {
      c=PEEK(0xD6A4);
      while(n&&c==sector_order[n-1]) c=PEEK(0xD6A4);
      sector_order[n]=c;
    }
    for(n=0;n<10*(1+HD);n++) sector_order[n]&=0x7f;

    for (t = 0; t < 80; t++) {
      if (PEEK(0xD610))
        break;

      // Clear display for reading this track
      x = t * 7; y=16;
      for (xx = 0; xx < 6; xx++)
	for (yy = 0; yy < 160; yy++)
	  plot_pixel(x + xx, y + yy, 0);

      
      for (h = 0; h < 2; h++) {
        if (PEEK(0xD610))
          break;
	n=0;
	s=1;
        for (ss = 1; ss <= 10*(1+HD); ss++) {

          if (PEEK(0xD610))
            break;

          snprintf(read_message, 40, "Trying T:$%02x, S:$%02x, H:$%02x", t, s, h);
          print_text(0, 1, 7, read_message);

	  
          // Schedule a sector read
          POKE(0xD081, 0x00); // Cancel previous action

          // Select track, sector, side
          POKE(0xD084, t);
          POKE(0xD085, s);
          POKE(0xD086, 1-h); // Side flag is inverted

          // Select correct side of the disk
          if (h)
            POKE(0xD080, 0x68);
          else
            POKE(0xD080, 0x60);

          // Issue read command
	  POKE(0xD081, 0x01); // but first reset buffers

          POKE(0xD081, 0x40);

          x = t * 7;
          y = 16 + (s - 1) * 8 + (h * 80);
          if (HD) y = 16 + (s - 1) * 4 + (h * 80);

          for (xx = 0; xx < 6; xx++)
            for (yy = 0; yy < 7-(4*HD); yy++)
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
	      for (yy = 0; yy < 7-(4*HD); yy++)
                plot_pixel(x + xx, y + yy, c);
          }
          else {
            c = 5;
            if (((t / 10) + h) & 1)
              c = 13;
            for (xx = 0; xx < 6; xx++)
	      for (yy = 0; yy < 7-(4*HD); yy++)
                plot_pixel(x + xx, y + yy, c);
          }
          activate_double_buffer();
          //	  lcopy(0xffd6000L,0x4e200L,0x200);

	  // Read every 2nd sector for better interleaving
	  s+=2; if (s>(10*(1+HD))) s-=(10*(1+HD))-1;


    
	  
	  
        }
      }

      // Seek to next track
      POKE(0xD081,0x18);
      usleep(10000);
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
        snprintf(peak_msg, 40, "Peak @ %d, auto-tune=%d, %s     ", interval_length, PEEK(0xD689) & 0x10,
		 (PEEK(0xD6A2)==0x51)?"DD":"HD");
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
    snprintf(peak_msg, 40, "Last sector under head T:$%02X S:%02X H:%02x", PEEK(0xD6A3), PEEK(0xD6A4), PEEK(0xD6A5));
    if ((PEEK(0xD6A3) & 0x7f) != last_track_seen) {
      last_track_seen = PEEK(0xD6A3) & 0x7f;
      // Zero out list of sectors seen
      snprintf(read_sectors, 41, "Sect: ................................");
    }
    else {
      // Note this sector
      read_sectors[6 + (PEEK(0xD6A4) & 0x1f)] = '0'+(PEEK(0xD6A4)%10);
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
      case 0x4D: // (M)anual seeking
      case 0x6D:
        // Switch auto/manual tracking in FDC to manual
        POKE(0xD689, PEEK(0xD689) | 0x10);
        break;
      case 0x41: // (A)uto track seeking
      case 0x61:
        // Auto-tune on
        POKE(0xD689, PEEK(0xD689) & 0xEF);
        break;
      case 0x48: case 0x68: // (H)D disk
	POKE(0xD6A2,0x28);
	break;
      case 0x44: case 0x64: // (D)D disk
	POKE(0xD6A2,0x51);
	break;
      case 0x52: // (R)ead a sector
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
      case 0x53: // (S)eek to a random sector
      case 0x73:
        random_seek_count = 0;
        seek_random_track();
        break;
      }
      POKE(0xD610, 0);
    }
  }
}

unsigned char sector_num=0;

void read_track(unsigned char track_number,unsigned char side)
{
  // First seek to the correct track

  if (track_number!=255) {

    graphics_mode();
    graphics_clear_double_buffer();
    activate_double_buffer();
    
    // Connect to real floppy drive
    while(!(lpeek(0xffd36a1L) & 1)) {
      lpoke(0xffd36a1L,lpeek(0xffd36a1L)|0x01);
    }
    
    // Floppy motor on, and select side
    POKE(0xD080, 0x68);
    if (side) POKE(0xD080,0x60);
    
    // Map FDC sector buffer, not SD sector buffer
    POKE(0xD689, PEEK(0xD689) & 0x7f);
    
    // Disable matching on any sector, use real drive
    POKE(0xD6A1, 0x01);
    
    // Wait until busy flag clears
    while (PEEK(0xD082) & 0x80) {
      snprintf(peak_msg, 40, "Sector under head T:$%02X S:%02X H:%02x", PEEK(0xD6A3), PEEK(0xD6A4), PEEK(0xD6A5));
      print_text(0, 24, 7, peak_msg);
      continue;
    }

    print_text(0, 0, 7, "Reading track...");
    
    POKE(0xD689,PEEK(0xD689)|0x10); // Disable auto-seek, or we can't force seeking to track 0
      
    // Seek to track 0
    print_text(0, 2, 15, "Seeking to track 0");
    while(!(PEEK(0xD082)&0x01)) {
      POKE(0xD081,0x10);
      usleep(6000);
      
      snprintf(peak_msg, 40, "Sector under head T:$%02X S:%02X H:%02x", PEEK(0xD6A3), PEEK(0xD6A4), PEEK(0xD6A5));
      print_text(0, 24, 7, peak_msg);
      
    }
    
    // Seek to the requested track
    print_text(0, 3, 15, "Seeking to target track");
    for(i=0;i<track_number;i++) {
      POKE(0xD081,0x18);
      usleep(6000);	
    }        
    print_text(0, 4, 15, "Seek complete");
  }
  else 
    print_text(0, 0, 7, "Reading current track...");
  
  print_text(0, 4, 7, "Starting track read");
  
  // Call routine to read a complete track of data into $50000-$5FFFF
  readtrackgaps();
  
  print_text(0, 5, 7, "Track read complete");
     
}

unsigned char read_a_sector(unsigned char track_number,unsigned char side, unsigned char sector)
{
  
  // Now select the side, and try to read the sector
  POKE(0xD084, track_number);
  POKE(0xD085, sector);
  POKE(0xD086, side?1:0);

  // Issue read command
  POKE(0xD081, 0x01); // but first reset buffers
  POKE(0xD081, 0x40);

  // Wait for busy flag to clear
  i=0xe000;
  while (PEEK(0xD082) & 0x80) {
    // Read sector data non-buffered while we wait for comparison
    if (PEEK(0xD082)&0x40) { POKE(0xE000+i,PEEK(0xD087)); i++; i&=0x1fff; }
    POKE(0xc000,PEEK(0xc000)+1);
  }

  if (PEEK(0xD082) & 0x18) {
    // Read failed
    POKE(0xD020,PEEK(0xD020)+1);
    return 1;
  } else {
    // Read succeeded
    return 0;
  }
}

unsigned char write_a_sector(unsigned char track_number,unsigned char side, unsigned char sector)
{
  // Disable auto-seek, or we can't force seeking to track 0    
  POKE(0xD689,PEEK(0xD689)|0x10); 

  // Connect to real floppy drive
  while(!(lpeek(0xffd36a1L) & 1)) {
    lpoke(0xffd36a1L,lpeek(0xffd36a1L)|0x01);
  }
  
  // Floppy motor on, and select side
  POKE(0xD080, 0x68);
  if (side) POKE(0xD080,0x60);
  
  // Map FDC sector buffer, not SD sector buffer
  POKE(0xD689, PEEK(0xD689) & 0x7f);
  
  // Disable matching on any sector, use real drive
  POKE(0xD6A1, 0x01);

  // Wait until busy flag clears
  while (PEEK(0xD082) & 0x80) {
    continue;
  }

  // Seek to track 0
  while(!(PEEK(0xD082)&0x01)) {
    POKE(0xD081,0x10);
    while(PEEK(0xD082)&0x80) continue;
  }

  // Seek to the requested track
  for(i=0;i<track_number;i++) {
    POKE(0xD081,0x18);
    while(PEEK(0xD082)&0x80) continue;
    }        

  // Now select the side, and try to read the sector
  POKE(0xD084, track_number);
  POKE(0xD085, sector);
  POKE(0xD086, side?1:0);

  // Issue write command
  POKE(0xD081, 0x01); // but first reset buffers
  POKE(0xD081, 0x80);

  // Wait for busy flag to clear
  while (PEEK(0xD082) & 0x80) {
    POKE(0xc000,PEEK(0xc000)+1);

    i=0xc050+PEEK(0xD694);
    POKE(i,PEEK(i)+1);
  }

  if (PEEK(0xD082) & 0x10) {
    // Write failed
    POKE(0xD020,PEEK(0xD020)+1);
    return 1;
  } else {
    // Write succeeded
    return 0;
  }
}



// CRC16 algorithm from:
// https://github.com/psbhlw/floppy-disk-ripper/blob/master/fdrc/mfm.cpp
// GPL3+, Copyright (C) 2014, psb^hlw, ts-labs.
// crc16 table
unsigned short crc_ccitt[256];
unsigned short crc=0;

// crc16 init table
void crc16_init()
{
    for (i = 0; i < 256; i++)
    {
        uint16_t w = i << 8;
        for (a = 0; a < 8; a++)
            w = (w << 1) ^ ((w & 0x8000) ? 0x1021 : 0);
        crc_ccitt[i] = w;
    }
}

// calc crc16 for 1 byte
unsigned short crc16(unsigned short crc, unsigned short b)
{
    crc = (crc << 8) ^ crc_ccitt[((crc >> 8) & 0xff) ^ b];
    return crc;
}

#define MAX_SECTORS 64
unsigned char header_crc_bytes[MAX_SECTORS*2];
unsigned char data_crc_bytes[MAX_SECTORS][2];
unsigned char track_num=0;
unsigned char side=0;

unsigned char s;
unsigned char sector_data[512];
char msg[80];

void precalc_sector_header_crcs(void)
{
  // Pre-calculate header CRC bytes
  crc16_init();
  for(i=0;i<MAX_SECTORS;i++)
    {
      // Calculate initial CRC of sync bytes and $FE header marker
      crc=crc16(0xFFFF,0xa1);
      crc=crc16(crc,0xa1);
      crc=crc16(crc,0xa1);
      crc=crc16(crc,0xfe);
      crc=crc16(crc,track_num);
      crc=crc16(crc,side);
      crc=crc16(crc,i+1);
      crc=crc16(crc,2); // 512 byte sectors
      header_crc_bytes[i*2+0]=crc&0xff;
      header_crc_bytes[i*2+1]=crc>>8;
    }
}

void format_single_track_side(/* unsigned char track_num,unsigned char side, */unsigned char sector_count,unsigned char with_gaps)
{

  precalc_sector_header_crcs();
  
  // Now calculate CRC of data sectors
  // We fill the data sectors with info that makes it easy to see where mis-reads
  // have come from.
  for(s=0;s<sector_count;s++) {
    bzero(sector_data,512);
    
    crc16_init();
    crc=crc16(0xFFFF,0xa1);
    crc=crc16(crc,0xa1);
    crc=crc16(crc,0xa1);
    crc=crc16(crc,0xfb);
    for(i=0;i<512;i++) crc=crc16(crc,sector_data[i]);
    data_crc_bytes[s][0]=crc&0xff;
    data_crc_bytes[s][1]=crc>>8;
    
  }      
  
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

       See also the wttrk routine from dos.src of C65 ROM source code.
	   
  */

  POKE(0xD020,0x06);
  
  // Data byte = $00 (first of 12 post-index gap bytes)
  POKE(0xD087,0x4E);
  // Clock byte = $FF
  POKE(0xD088,0xFF);
  
  // Begin unbuffered write
  POKE(0xD081,0xA1);  

  // Write 12 gap bytes
  POKE(0xD020,15);
  for(i=0;i<12;i++) {
    while(!(PEEK(0xD082)&0x40)) {
      if (!(PEEK(0xD082)&0x80)) break;
      continue;
    }
    POKE(0xD087,0x00);
  }
  
  // We count x2 so that sector_num is also the offset in header_crc_bytes[] to get the CRC bytes
  for(sector_num=0;sector_num<(sector_count*2);sector_num+=2) {
    
    s=(sector_num>>1);

    POKE(0x0400+s,0x21);
    
    // Write 3 sync bytes
    for(i=0;i<3;i++) {
      while(!(PEEK(0xD082)&0x40)) {
	if (!(PEEK(0xD082)&0x80)) break;
	continue;
      }
      POKE(0xD087,0xA1);
      POKE(0xD088,0xFB);
    }
    
    // Header mark
    while(!(PEEK(0xD082)&0x40)) {
      if (!(PEEK(0xD082)&0x80)) break;
      continue;
    }
    POKE(0xD087,0xFE); 
    POKE(0xD088,0xFF);
    
    // Track number
    while(!(PEEK(0xD082)&0x40)) {
      if (!(PEEK(0xD082)&0x80)) break;
      continue;
    }
    POKE(0xD087,track_num); 
    
    // Side number
    while(!(PEEK(0xD082)&0x40)) {
      if (!(PEEK(0xD082)&0x80)) break;
    }
    POKE(0xD087,side); 
    
    // Sector number
    while(!(PEEK(0xD082)&0x40)) {
      if (!(PEEK(0xD082)&0x80)) break;
    }
    POKE(0xD087,1+s);
    
    // Sector length
    while(!(PEEK(0xD082)&0x40)) {
      if (!(PEEK(0xD082)&0x80)) break;
    }
    POKE(0xD087,0x02); 
    
    // Sector header CRC
    while(!(PEEK(0xD082)&0x40)) {
      if (!(PEEK(0xD082)&0x80)) break;
    }
    POKE(0xD087,header_crc_bytes[sector_num+1]); 
    while(!(PEEK(0xD082)&0x40)) {
      if (!(PEEK(0xD082)&0x80)) break;
    }
    POKE(0xD087,header_crc_bytes[sector_num+0]); 

    if (with_gaps) {
      // 23 gap bytes
      for (i=0;i<23;i++) {
	while(!(PEEK(0xD082)&0x40)) {
	  if (!(PEEK(0xD082)&0x80)) break;
	}
	POKE(0xD087,0x4E); 
      }
      
      // 12 gap bytes
      for (i=0;i<12;i++) {
	while(!(PEEK(0xD082)&0x40)) {
	  if (!(PEEK(0xD082)&0x80)) break;
	}
	POKE(0xD087,0x00); 
      }
    }
    
    // Write 3 sync bytes
    for(i=0;i<3;i++) {
      while(!(PEEK(0xD082)&0x40)) {
	if (!(PEEK(0xD082)&0x80)) break;
      }
      POKE(0xD087,0xA1);
      POKE(0xD088,0xFB);
    }
    
    // Data mark
    while(!(PEEK(0xD082)&0x40)) {
      if (!(PEEK(0xD082)&0x80)) break;
    }
    POKE(0xD087,0xFB); 
    POKE(0xD088,0xFF);    
    
    // Data bytes
    for (i=0;i<512;i++) {
      while(!(PEEK(0xD082)&0x40)) {
	if (!(PEEK(0xD082)&0x80)) break;
      }
      POKE(0xD087,sector_data[i]); 
    }
    
    // Write data CRC bytes
    while(!(PEEK(0xD082)&0x40)) {
      if (!(PEEK(0xD082)&0x80)) break;
    }
    POKE(0xD087,data_crc_bytes[s][1]); 
    while(!(PEEK(0xD082)&0x40)) {
      if (!(PEEK(0xD082)&0x80)) break;
    }
    POKE(0xD087,data_crc_bytes[s][0]); 
    
    if (with_gaps) {
      // 24 gap bytes
      for (i=0;i<24;i++) {
	while(!(PEEK(0xD082)&0x40)) {
	  if (!(PEEK(0xD082)&0x80)) break;
	}
	POKE(0xD087,0x4E);
      }
    }
    
  }
}


void format_disk(unsigned char HD)
{
  // First seek to the correct track

  if (HD) POKE(0xD6A2,0x28); else POKE(0xD6A2,0x51);
  
  // Connect to real floppy drive
  while(!(lpeek(0xffd36a1L) & 1)) {
    lpoke(0xffd36a1L,lpeek(0xffd36a1L)|0x01);
  }
  
  // Floppy 0 motor on
  POKE(0xD080, 0x68);

  // Disable auto-tracking
  POKE(0xD689, PEEK(0xD689) | 0x10);
  POKE(0xD696,0x00);  // also disable auto-seek on new address

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

  
  print_text(0, 0, 7, "Formatting disk...");

  POKE(0xD689,PEEK(0xD689)|0x10); // Disable auto-seek, or we can't force seeking to track 0

  // Seek to track 0
  print_text(0, 2, 15, "Seeking to track 0 .....");
  while(!(PEEK(0xD082)&0x01)) {
    POKE(0xD081,0x10);
    usleep(6000);

    snprintf(peak_msg, 40, "Sector under head T:$%02X S:%02X H:%02x", PEEK(0xD6A3), PEEK(0xD6A4), PEEK(0xD6A5));
    print_text(0, 24, 7, peak_msg);
    
  }

  lfill(0xFF80000L,0x01,4000);
  
  for(track_num=0;track_num<80;track_num++) {
    // Seek to the requested track
    snprintf(peak_msg, 40, "Formatting track %d",track_num);
    print_text(0, 3, 15, peak_msg);

    for(side=0;side<2;side++) {

      // Select head side
      if (side)
	POKE(0xD080, 0x60);
      else
	POKE(0xD080, 0x68);

      format_single_track_side(/* track_num,side, */ 10*(1+HD),1 /* = with inter-sector gaps */);

    }
    
  // Seek to next track
  POKE(0xD081,0x18);
  usleep(20000);

  }

}

void wipe_disk(void)
{
  // Connect to real floppy drive
  while(!(lpeek(0xffd36a1L) & 1)) {
    lpoke(0xffd36a1L,lpeek(0xffd36a1L)|0x01);
  }
  
  // Floppy 0 motor on
  POKE(0xD080, 0x68);

  // Disable auto-tracking
  POKE(0xD689, PEEK(0xD689) | 0x10);
  POKE(0xD696,0x00);  // also disable auto-seek on new address

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

  
  print_text(0, 0, 7, "Wiping disk (erasing magnetic reversals)...");

  POKE(0xD689,PEEK(0xD689)|0x10); // Disable auto-seek, or we can't force seeking to track 0

  // Seek to track 0
  print_text(0, 2, 15, "Seeking to track 0");
  while(!(PEEK(0xD082)&0x01)) {
    POKE(0xD081,0x10);
    usleep(6000);

    snprintf(peak_msg, 40, "Sector under head T:$%02X S:%02X H:%02x", PEEK(0xD6A3), PEEK(0xD6A4), PEEK(0xD6A5));
    print_text(0, 24, 7, peak_msg);
    
  }

  lfill(0xFF80000L,0x01,4000);
  
  a=0;
  for(track_num=0;track_num<85;track_num++) {
    // Seek to the requested track
    snprintf(peak_msg, 40, "Erasing track %d",track_num);
    print_text(0, 3, 15, peak_msg);

    for(side=0;side<2;side++) {

      // Select head side
      if (side)
	POKE(0xD080, 0x68);
      else
	POKE(0xD080, 0x60);
      
      POKE(0xD020,0x06);

      // Write no data with no clock
      POKE(0xD087,0x00);
      POKE(0xD088,0x00);

      // Begin unbuffered write
      POKE(0xD081,0xA1);  
      
      // Write 87+512 x 10 sectors = 5,990
      POKE(0xD020,15);
      
      for(i=0;i<5990;i++) {
	  while(!(PEEK(0xD082)&0x40)) {
	    if (!(PEEK(0xD082)&0x80)) break;
	    if (PEEK(0xD082)<0x40) {
	      a++;
	      snprintf(peak_msg, 40, "i=%d, track=%d, count=%d",i,track_num,a);
	      print_text(0,5,2,peak_msg);
	    }
	    continue;
	  }
	  POKE(0xD087,0x00);
	  POKE(0xD088,0x00);
      }
      
    }
    
    // Seek to next track
    POKE(0xD081,0x18);
    usleep(20000);
    
  }
  
}

unsigned short tries=0,auto_tracking=0;


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
    // Revert video mode
    POKE(0xD054, 0);
    POKE(0xD031, 0);
    POKE(0xD060, 0);
    POKE(0xD061, 0x04);
    POKE(0xD062, 0);
    POKE(0xD011, 0x1b);
    POKE(0xD018, 0x16);
    
    printf("%cMEGA65 Floppy Drive Capacity Testing.\n\n", 0x93);

    {
      // How many sectors per track without gaps (i.e., amiga style)
      unsigned char sectors_by_rate_no_gaps[40+1]
	={
	  0,0,0,0,0,0,0,0,0,0,
	  0,0,0,0,0,0,0,0,0,0,
	  0,0,0,0,0,0,0,0,0,0,
	  31,30,29,29,28,27,26,26,25,24,
	  24					   
      };

      unsigned char rate_for_sector_count_no_gaps[31+1]=
	{
	 40,40,40,40,40,40,40,40,40,40,
	 40,40,40,40,40,40,40,40,40,40,
	 40,40,40,40,40,38,37,35,34,33,31,
	 30
	};

      for(track_num=0;track_num<80;track_num++) {
	
	// 36 + no gaps works on track 79 without errors = 26 sectors per track
	unsigned char bit_interval=35; // Standard HD 3.5" floppy is 500Kbit/second = 1mhz MFM rate (2 clocks per MFM bit)
	unsigned char sector_count=50;
	unsigned char with_gaps=0;
	unsigned char sector_num,errors,best=0;
	side=0;

	printf(" T%d:",track_num);

	for(sector_count=24;sector_count<=31;sector_count++) {
	  	 
	  // Floppy 0 motor on
	  POKE(0xD080, 0x68);	  

	  // Seek to track 0
	  while (!(PEEK(0xD082) & 1)) {
	    POKE(0xD081, 0x10);
	    usleep(6000);
	  }
	  
	  // Seek to the desired track
	  for(i=0;i<track_num;i++) {
	    POKE(0xD081,0x18);
	    while(PEEK(0xD082)&0x80) continue;
	  }        
	  	
	  // Disable auto-tracking
	  POKE(0xD696,0x00);  // also disable auto-seek on new address
	  
	  // Map FDC sector buffer, not SD sector buffer
	  POKE(0xD689, PEEK(0xD689) & 0x7f);
	  
	  // Disable matching on any sector, use real drive
	  POKE(0xD6A1, 0x01);
	  
	  // Select the specified side
	  POKE(0xD086, 1-side); // Side flag is inverted
	  
	  //	  printf("Pre-erasing track.\n");
	  
	  // Wipe the track
	  // Write no data with no clock
	  POKE(0xD087,0x00);
	  POKE(0xD088,0x00);
	  
	  // Erase the track at standard HD data rate
	  POKE(0xD6A2,40);
	  
	  // Begin unbuffered write
	  POKE(0xD081,0xA1);  
	  
	  // Write absolutely nothing on the whole track
	  for(i=0;i<30000;i++) {
	    while(!(PEEK(0xD082)&0x40)) {
	      if (!(PEEK(0xD082)&0x80)) break;
	      if (PEEK(0xD082)<0x40) {
		a++;
	      }
	      continue;
	    }
	    POKE(0xD087,0x00);
	    POKE(0xD088,0x00);
	  }
	  
	  // Write the track at the desired rate
	  //	  printf("Writing track at rate %d\n",bit_interval);
	  bit_interval=rate_for_sector_count_no_gaps[sector_count];
	  POKE(0xD6A2,bit_interval);
	  format_single_track_side(sector_count,with_gaps);
	  
	  //	  printf("Reading back %d sectors:\n",sector_count);
	  errors=0; 
	  for(sector_num=1;sector_num<=sector_count;sector_num++) {
	    if (read_a_sector(track_num,side,sector_num)) { errors++; }
	    //	    else { printf("%d",sector_num%10); }
	  }
	  //	  printf(" %d/%d",sector_count-errors,sector_count);
	  if (!errors) best=sector_count; // printf(" %d",sector_count);
	}
	//	printf("\n");
	printf("%d",best);
      }
    }
    
    while(1) continue;
  }
}
