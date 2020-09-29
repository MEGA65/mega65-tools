#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>
#include <time.h>
#include <targets.h>

unsigned short i;
unsigned char a,b,c,d;
unsigned short interval_length;

#define DIVISOR_115200 (40500000L/115200L)

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
  lfill(0x40000L,0,32768);
  lfill(0x48000L,0,32768);
}

void graphics_clear_double_buffer(void)
{
  lfill(0x50000L,0,32768);
  lfill(0x58000L,0,32768);
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

unsigned char fast_flags=0x70; // 0xb0; 
unsigned char slow_flags=0x00;
unsigned char cache_bit=0x80; // =0x80;
unsigned long addr,upper_addr,time,speed;

unsigned char joya_up=0,joyb_up=0,joya_down=0,joyb_down=0;

void bust_cache(void) {
  lpoke(0xbfffff2,fast_flags&(0xff-cache_bit));
  lpoke(0xbfffff2,fast_flags|cache_bit);
}

unsigned char setup_hyperram(void)
{
  /*
    Test complete HyperRAM, including working out the size.
  */

  unsigned char retries=255;
  
  lpoke(0x8000000,0xbd);
  while(lpeek(0x8000000)!=0xBD) {
    lpoke(0x8000000,0xbd);
    retries--; if (!retries) return 1;
  }
  for(addr=0x8001000;(addr!=0x8800000);addr+=0x1000)
    {

      // XXX There is still some cache consistency bugs,
      // so we bust the cache before checking various things
      bust_cache();
      
      if (lpeek(0x8000000)!=0xbd) {
	// Memory location didn't hold value
	return 1;
      }
      
      //      if (!(addr&0xfffff)) printf(".");
      //      POKE(0xD020,PEEK(0xD020)+1);

      bust_cache();
      
      lpoke(addr,0x55);

      bust_cache();
      
      i=lpeek(addr);
      if (i!=0x55) {
	if ((addr!=0x8800000)&&(addr!=0x9000000)) {
	  // printf("\n$%08lx corrupted != $55\n (saw $%02x, re-read yields $%02x)",addr,i,lpeek(addr));
	  return 1;
	}
	break;
      }

      bust_cache();

      lpoke(addr,0xAA);

      bust_cache();

      i=lpeek(addr);
      if (i!=0xaa) {
	if ((addr!=0x8800000)&&(addr!=0x9000000)) {
	  // printf("\n$%08lx corrupted != $AA\n  (saw $%02x, re-read yields $%02x)",addr,i,lpeek(addr));
	  return 1;
	}
	break;
      }

      bust_cache();

      i=lpeek(0x8000000);
      if (i!=0xbd) {
	// printf("\n$8000000 corrupted != $BD\n  (saw $%02x, re-read yields $%02x)",i,lpeek(0x8000000));
	return 1;
	break;
      }
    }


  upper_addr=addr;

  if ((addr!=0x8800000)&&(addr!=0x9000000)) {
    return 1;
  }

  lpoke(0xbfffff2,fast_flags|cache_bit);

  return 0;
}


unsigned char num_uarts=0;

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

  print_text(0,0,1,"MEGA65/MEGAphone Buffered UART Controller Test");
  snprintf(msg,80,"Hardware model = %d",detect_target());
  print_text(0,1,1,msg);

  // Work out how many UARTs we have
  for(num_uarts=0;num_uarts<16;num_uarts++) {
    // Select UART
    POKE(0xD0E0,num_uarts);
    // Try to write to UART baud rate divisor
    POKE(0xD0E4,0x00);
    // If it doesn't set to 0, then no UART here
    if (PEEK(0xD0E4)) break;
    POKE(0xC000+num_uarts*2,PEEK(0xD0E4));
  }
  
  snprintf(msg,80,"Controller has %d UARTs",num_uarts);
  print_text(0,2,7,msg);

  // Perform loopback tests
  for(i=0;i<num_uarts;i++)
    {
      // Select UART i, and enable loopback mode.
      // Loopback mode connects UART 0 to 7 and vice versa, 1 to 6, ... 3 to 4.
      POKE(0xD0E0,0x10+i);
      // Clear RX buffer of UART
      for(x=0;x<256;x++) POKE(0xD0E2,0);
      POKE(0xD0E0,0x17-i);
      // Clear RX buffer of loop-backed UART
      for(x=0;x<256;x++) POKE(0xD0E2,0);
      // And set baud rate to 115200
      POKE(0xD0E4,(DIVISOR_115200>>0));
      POKE(0xD0E5,(DIVISOR_115200>>8));
      POKE(0xD0E6,(DIVISOR_115200>>16));
      
      POKE(0xD0E0,0x10+i);
      // Flush any TX queue quickly by setting divisor to $000000
      POKE(0xD0E4,0);
      POKE(0xD0E5,0);
      POKE(0xD0E6,0);
      // Clear interrupt modes etc
      POKE(0xD0E1,0x00);
      // 255 chars x 1/115200sec = 2.2ms
      // so waiting 10ms should be more than enough
      usleep(10000);
      if (PEEK(0xD0E1)!=0x60) {
	snprintf(msg,80,"UART#%d status incorrect. Should be $60. Saw $%02x",i,PEEK(0xD0E1));
	print_text(0,4+i,2,msg);
	break;
      }

      // Send a char from UART, make sure TXEMPTY bit clears, then reasserts.

      // Select UART again
      POKE(0xD0E0,0x10+i);
      // And set really slow baud rate, so we can watch TXEMPTY toggle
      POKE(0xD0E4,0xff);
      POKE(0xD0E5,0xff);
      POKE(0xD0E6,0x00);
      // Write char (which gets sent IMMEDIATELY because queue is empty)
      POKE(0xD0E3,0x42);
      // Then send a 2nd that WILL get queued
      POKE(0xD0E3,0x42);
      // We have to check fast, as else char will have been sent.
      // Only 10/115200 seconds = ~0.1 milliseconds to check
      x=PEEK(0xD0E1); 
      if (x!=0x40) {
	snprintf(msg,80,"UART#%d TXEMPTY did not clear ($%02x)",num_uarts,x);
	print_text(0,4+i,2,msg);
	break;
      }
      
      
      // If all tested well.
      snprintf(msg,80,"UART#%d OK.",i);
      print_text(0,4+i,5,msg);
      
    }

  while(1)
    { 
     continue;
    }
  
  
  //  gap_histogram();
  // read_all_sectors();
}

