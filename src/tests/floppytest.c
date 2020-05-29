#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>

unsigned short i;
unsigned char a,b,c,d;
unsigned short interval_length;

void get_interval(void)
{
  // Make sure we start measuring a fresh interval
  a=PEEK(0xD6AA);
  while(a==PEEK(0xD6AA)) continue;
	
  do {
    a=PEEK(0xD6A9); b=PEEK(0xD6AA);
    c=PEEK(0xD6A9); d=PEEK(0xD6AA);
  } while (a!=c||b!=d);
  interval_length=a+((b&0xf)<<8);
}

void graphics_clear_screen(void)
{
  lfill(0x40000,0,32768);
  lfill(0x48000,0,32768);
}

void graphics_clear_double_buffer(void)
{
  lfill(0x50000,0,32768);
  lfill(0x58000,0,32768);
}

void graphics_mode(void)
{
  // 16-bit text mode, full-colour text for high chars
  POKE(0xD054,0x05);
  // H640, fast CPU
  POKE(0xD031,0xC0);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016,0xC9);
  // 640x200 16bits per char, 16 pixels wide per char
  // = 640/16 x 16 bits = 80 bytes per row
  POKE(0xD058,80);
  POKE(0xD059,80/256);
  // Draw 40 (double-wide) chars per row
  POKE(0xD05E,40);
  // Put 2KB screen at $C000
  POKE(0xD060,0x00);
  POKE(0xD061,0xc0);
  POKE(0xD062,0x00);

  // Layout screen so that graphics data comes from $40000 -- $4FFFF

  i=0x40000/0x40;
  for(a=0;a<40;a++)
    for(b=0;b<25;b++) {
      POKE(0xC000+b*80+a*2+0,i&0xff);
      POKE(0xC000+b*80+a*2+1,i>>8);

      i++;
    }

  // Clear colour RAM, while setting all chars to 4-bits per pixel
  for(i=0;i<2000;i+=2) {
    lpoke(0xff80000L+0+i,0x08);
    lpoke(0xff80000L+1+i,0x00);
  }
  POKE(0xD020,0);
  POKE(0xD021,0);

  graphics_clear_screen();
}

unsigned short pixel_addr;
unsigned char pixel_temp;
void plot_pixel(unsigned short x,unsigned char y,unsigned char colour)
{
  pixel_addr=((x&0xf)>>1)+64*25*(x>>4);
  pixel_addr+=y<<3;
  pixel_temp=lpeek(0x50000L+pixel_addr);
  if (x&1) {
    pixel_temp&=0x0f;
    pixel_temp|=colour<<4;
  } else {
    pixel_temp&=0xf0;
    pixel_temp|=colour&0xf;
  }
  lpoke(0x50000L+pixel_addr,pixel_temp);
}

unsigned char char_code;
void print_text(unsigned char x,unsigned char y,unsigned char colour,char *msg)
{
  pixel_addr=0xC000+x*2+y*80;
  while(*msg) {
    char_code=*msg;
    if (*msg>=0xc0&&*msg<=0xe0) char_code=*msg-0x80;
    if (*msg>=0x40&&*msg<=0x60) char_code=*msg-0x40;
    POKE(pixel_addr+0,char_code);
    POKE(pixel_addr+1,0);
    lpoke(0xff80000-0xc000+pixel_addr+0,0x00);
    lpoke(0xff80000-0xc000+pixel_addr+1,colour);
    msg++;
    pixel_addr+=2;
  }
}

void activate_double_buffer(void)
{
  lcopy(0x50000,0x40000,0x8000);
  lcopy(0x58000,0x48000,0x8000);
}

unsigned char histo_bins[640];
char peak_msg[40+1];
unsigned char random_target=40;

void gap_histogram(void)
{

  // Floppy motor on
  POKE(0xD080,0x60);  
  
  graphics_mode();

  print_text(0,0,1,"Magnetic Domain Interval Histogram");
  
  while(1) {
    // Clear histogram bins
    for(i=0;i<640;i++) histo_bins[i]=0;

    // Schedule a sector read
    POKE(0xD084,40);
    POKE(0xD085,1);
    POKE(0xD086,0);
    POKE(0xD081,0x40);
    
    // Get new histogram data
    while(1) {
      get_interval();
      if (interval_length>=640) continue;
      // Stop as soon as a single histogram bin fills
      if (histo_bins[interval_length]==255) {
	snprintf(peak_msg,40,"Peak @ %d     ",interval_length);
	print_text(0,2,7,peak_msg);
	break;
      }
      histo_bins[interval_length]++;
    }

    // Re-draw histogram.
    // We use 640x200 16-colour char mode
    graphics_clear_double_buffer();
    for(i=0;i<640;i++) {
      b=5;
      if (histo_bins[i]>128) b=7;
      if (histo_bins[i]>192) b=10;
      for(a=199-(histo_bins[i]>>1);a<200;a++)
	plot_pixel(i,a,b);
    }

    snprintf(peak_msg,40,"Floppy Status = $%02X,$%02X",
	     PEEK(0xD082),PEEK(0xD083)	     );
    print_text(0,3,7,peak_msg);
    snprintf(peak_msg,40,"Last sector  T:%02X, S:%02X, H:%02x",
	     PEEK(0xD6A3),PEEK(0xD6A4),PEEK(0xD6A5)
	     );
    print_text(0,4,7,peak_msg);
    snprintf(peak_msg,40,"Target track is T:$%02X",random_target);
    print_text(0,5,7,peak_msg);
    
    
    activate_double_buffer();

    if (PEEK(0xD610)) {
      switch(PEEK(0xD610)) {
      case 0x11: POKE(0xD081,0x10); break;
      case 0x91: POKE(0xD081,0x18); break;
      case 0x52: case 0x72:
	// Seek to random track.
	random_target=PEEK(0xD012)%80;
	a=(PEEK(0xD6A3)&0x7f)-random_target;
	if (a&0x80) {
	  while(a) {
	    POKE(0xD081,0x18);
	    usleep(6000);
	    a++;
	  }
	} else {
	  while(a) {
	    POKE(0xD081,0x10);
	    usleep(6000);
	    a--;
	  }
	}
	break;
      }
      POKE(0xD610,0);
    }
  }
}

void main(void)
{
  // Fast CPU, M65 IO
  POKE(0,65);
  POKE(0xD02F,0x47);
  POKE(0xD02F,0x53);

  gap_histogram();
  
}
