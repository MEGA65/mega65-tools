#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>

unsigned short i;
unsigned char a,b,c,d;

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
unsigned char last_random_target=40;
unsigned int random_seek_count=0;
unsigned char request_track=40;
unsigned char read_sectors[41]={0};
unsigned char last_track_seen=255;
unsigned int histo_samples=0;


void main(void)
{
  // Fast CPU, M65 IO
  POKE(0,65);
  POKE(0xD02F,0x47);
  POKE(0xD02F,0x53);

  // time base = $000001
  POKE(0xD724,0x01);
  POKE(0xD725,0x00);
  POKE(0xD726,0x00);
  // Top address
  POKE(0xD727,0xFE);
  POKE(0xD728,0xFF);
  // Enable audio dma, 16 bit samples
  POKE(0xD711,0x80);
  // Enable playback+looping of channel 0
  POKE(0xD720,0xC3);
  
  
  //  graphics_mode();
  //  print_text(0,0,1,"");

  printf("%c",0x93);

  while(1) {
    printf("%c",0x13);    
    
    if (PEEK(0xD610)) {
      switch(PEEK(0xD610)) {
      case 0x4d: case 0x6d:
	// M - Toggle master enable
       	POKE(0xD711,PEEK(0xD711)^0x80);
	break;
      case 0x57: case 0x77:
	// W - Toggle write enable
       	POKE(0xD711,PEEK(0xD711)^0x20);
	break;
      case 0x30:
	// 0 - Toggle channel 0 enable
       	POKE(0xD720,PEEK(0xD720)^0x80);
	break;
      }
      POKE(0xD610,0);
    }
    
    printf("Audio DMA tick counter = $%02x%02x%02x%02x\n",
	   PEEK(0xD71F),PEEK(0xD71E),PEEK(0xD71D),PEEK(0xD71C));

    printf("Master enable = %d,\n   blocked=%d, block_timeout=%d    \n"
	   "   write_enable=%d\n",
	   PEEK(0xD711)&0x80?1:0,
	   PEEK(0xD711)&0x40?1:0,
	   PEEK(0xD711)&0x0f,
	   PEEK(0xD711)&0x20?1:0
	   
	   );
    for(i=0;i<4;i++) {
      // Display Audio DMA channel
      printf("%d: en=%d, loop=%d, pending=%d, B24=%d, SS=%d\n"
	     "   v=$%02x, base=$%02x%02x%02x, top=$%04x\n"
	     "   curr=$%02x%02x%02x, tb=$%02x%02x%02x, ct=$%02x%02x%02x\n",
	     i,
	     (PEEK(0xD720+i*16+0)&0x80)?1:0,
	     (PEEK(0xD720+i*16+0)&0x40)?1:0,
	     (PEEK(0xD720+i*16+0)&0x20)?1:0,
	     (PEEK(0xD720+i*16+0)&0x10)?1:0,
	     (PEEK(0xD720+i*16+0)&0x3),
	     PEEK(0xD729+i*16),
	     PEEK(0xD723+i*16),PEEK(0xD722+i*16),PEEK(0xD721+i*16),
	     PEEK(0xD727+i*16)+(PEEK(0xD728+i*16)<<8+i*16),
	     PEEK(0xD72C+i*16),PEEK(0xD72B+i*16),PEEK(0xD72A+i*16),
	     PEEK(0xD726+i*16),PEEK(0xD725+i*16),PEEK(0xD724+i*16),
	     PEEK(0xD72F+i*16),PEEK(0xD72E+i*16),PEEK(0xD72D+i*16));
    }

    printf("Audio left/right: $%02x%02x, $%02X%02X\n",
	   PEEK(0xD6F8),PEEK(0xD6F9),PEEK(0xD6FA),PEEK(0xD6FB));
    
  }
  
  
}

