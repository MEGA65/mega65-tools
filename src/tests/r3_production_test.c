#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>
#include <time.h>

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
  // Disable hot regs
  POKE(0xD05D,PEEK(0xD05D)&0x7f);
  
  // Layout screen so that graphics data comes from $40000 -- $4FFFF

  i=0x40000/0x40;
  for(a=0;a<40;a++)
    for(b=0;b<25;b++) {
      POKE(0xC000+b*80+a*2+0,i&0xff);
      POKE(0xC000+b*80+a*2+1,i>>8);

      i++;
    }

  // Clear colour RAM, while setting all chars to 4-bits per pixel
  // Actually set colour to 15, so that 4-bit graphics mode picks up colour 15
  // when using 0xf as colour (as VIC-IV uses char foreground colour in that case)
  for(i=0;i<2000;i+=2) {
    lpoke(0xff80000L+0+i,0x08);
    lpoke(0xff80000L+1+i,0x0f);
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

unsigned char floppy_interval_first=0;
unsigned char floppy_active=0;
unsigned char eth_pass=0;
unsigned char iec_pass=0,v,y;
unsigned int x;
unsigned char colours[10]={0,2,5,6,0,11,12,15,1,0};
struct m65_tm tm;
char msg[80];

unsigned char sin_table[32]={
  //  128,177,218,246,255,246,218,177,
  //  128,79,38,10,0,10,38,79
  128,152,176,198,217,233,245,252,255,252,245,233,217,198,176,152,128,103,79,57,38,22,10,3,1,3,10,22,38,57,79,103
};



void play_sine(unsigned char ch, unsigned long time_base)
{
  unsigned ch_ofs=ch<<4;

  if (ch>3) return;
  
  // Play sine wave for frequency matching
  POKE(0xD721+ch_ofs,((unsigned short)&sin_table)&0xff);
  POKE(0xD722+ch_ofs,((unsigned short)&sin_table)>>8);
  POKE(0xD723+ch_ofs,0);
  POKE(0xD72A+ch_ofs,((unsigned short)&sin_table)&0xff);
  POKE(0xD72B+ch_ofs,((unsigned short)&sin_table)>>8);
  POKE(0xD72C+ch_ofs,0);
  // 16 bytes long
  POKE(0xD727+ch_ofs,((unsigned short)&sin_table+32)&0xff);
  POKE(0xD728+ch_ofs,((unsigned short)&sin_table+32)>>8);
  // 1/4 Full volume
  POKE(0xD729+ch_ofs,0x3F);
  // Enable playback+looping of channel 0, 8-bit samples, signed
  POKE(0xD720+ch_ofs,0xE2);
  // Enable audio dma
  POKE(0xD711+ch_ofs,0x80);

  // time base = $001000
  POKE(0xD724+ch_ofs,time_base&0xff);
  POKE(0xD725+ch_ofs,time_base>>8);
  POKE(0xD726+ch_ofs,time_base>>16);
  
  
}

void audioxbar_setcoefficient(uint8_t n,uint8_t value)
{
  // Select the coefficient
  POKE(0xD6F4,n);

  // Now wait at least 16 cycles for it to settle
  POKE(0xD020U,PEEK(0xD020U));
  POKE(0xD020U,PEEK(0xD020U));

  POKE(0xD6F5U,value); 
}


void main(void)
{
  // Fast CPU, M65 IO
  POKE(0,65);
  POKE(0xD02F,0x47);
  POKE(0xD02F,0x53);

  // Floppy motor on
  POKE(0xD080,0x60);  

  // Reset ethernet controller
  POKE(0xD6E0,0);
  POKE(0xD6E0,3);

  // Stop all DMA audio first
  POKE(0xD720,0);
  POKE(0xD730,0);
  POKE(0xD740,0);
  POKE(0xD750,0);

  // Audio cross-bar full volume
  for(i=0;i<256;i++) audioxbar_setcoefficient(i,0xff);

  graphics_mode();
  graphics_clear_double_buffer();

  print_text(0,0,1,"MEGA65 R3 PCB Production Test programme");

  floppy_interval_first=PEEK(0xD6A9);

  // Draw colour bars
  for(x=0;x<640;x++) {
    for(y=150;y<200;y++) {
      plot_pixel(x,y,colours[x>>6]);
    }
  }
  activate_double_buffer();
  
  while(1) {    

    // Play two different tones out of the left and right speakers alternately
    if (PEEK(0xD7FA)&0x40) {
      play_sine(0,2000);
      play_sine(3,1);     
    } else {
      play_sine(0,1);
      play_sine(3,3000);     
    }
    
    // Internal floppy connector
    if (PEEK(0xD6A9)!=floppy_interval_first) floppy_active=1;
    if (!floppy_active) 
      print_text(0,2,2,"FAIL Floppy (is a disk inserted?)");
    else
      print_text(0,2,5,"PASS Floppy                          ");

    // Try toggling the IEC lines to make sure we can pull CLK and DATA low.
    // (ATN can't be read.)
    POKE(0xDD00,0xC3); // let the lines float high
    v=PEEK(0xDD00);
    usleep(1000);
    if ((v&0xc0)==0xc0) {
      POKE(0xDD00,0x13); // pull $40 low via $10
      usleep(1000);
      if ((PEEK(0xDD00)&0xc0)==0x80) {
	POKE(0xDD00,0x23); // pull $80 low via $20
	usleep(1000);
	if ((PEEK(0xDD00)&0xc0)==0x40) {
	  POKE(0xDD00,0x33); // pull $80 and $40 low via $30
	  usleep(1000);
	  if ((PEEK(0xDD00)&0xc0)==0x00) {
	    iec_pass=1;
	  } else {
	    print_text(0,3,2,"FAIL IEC CLK+DATA (CLK+DATA)");
	  }
	} else {
	  print_text(0,3,2,"FAIL IEC CLK+DATA (CLK)");
	}
      } else {
	print_text(0,3,2,"FAIL IEC CLK+DATA (DATA)");
      }
    } else {
      snprintf(msg,80,"FAIL IEC CLK+DATA (float $%02x)",v);
      print_text(0,3,2,msg);
    }
    if (iec_pass)
      print_text(0,3,5,"PASS IEC CLK+DATA                     ");
   
    // Real-time clock
    getrtc(&tm);
    if (tm.tm_sec||tm.tm_hour||tm.tm_min) {
      snprintf(msg,80,"PASS RTC Ticks (%02d:%02d.%02d)   ",
	       tm.tm_hour,tm.tm_min,tm.tm_sec);
      print_text(0,4,5,msg);
    } else {
      print_text(0,4,2,"FAIL RTC Not running");
      // But try to set it running
      tm.tm_year=2020-1900;
      tm.tm_mon=10;
      tm.tm_mday=1;
      setrtc(&tm);
    }

    // Ethernet controller
    if (eth_pass) 
      print_text(0,5,5,"PASS Ethernet frame RX");
    else
      print_text(0,5,2,"FAIL Ethernet frame RX");

    if (PEEK(0xD6E1)&0x20) eth_pass=1;
    
  }
  
  //  gap_histogram();
  // read_all_sectors();
}

