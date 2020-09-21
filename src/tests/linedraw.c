#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>
#include <random.h>

char msg[64+1];

unsigned short pwr_sum=0;
unsigned char pwr_samples=0;
unsigned char pwr_period=32;
unsigned char pwr_last[3];
unsigned char pwr_count=0;
unsigned char zero_count=0;
unsigned char scanning=1;

unsigned short i,j;
unsigned char a,b,c,d;

unsigned char sin_table[32]={
  //  128,177,218,246,255,246,218,177,
  //  128,79,38,10,0,10,38,79
  128,152,176,198,217,233,245,252,255,252,245,233,217,198,176,152,128,103,79,57,38,22,10,3,1,3,10,22,38,57,79,103
};

unsigned short abs2(signed short s)
{
  if (s>0) return s;
  return -s;
}

void graphics_clear_screen(void)
{
  lfill(0x40000L,0,32768L);
  lfill(0x48000L,0,32768L);
}

void graphics_clear_double_buffer(void)
{
  lfill(0x50000L,0,32768L);
  lfill(0x58000L,0,32768L);
}

void h640_text_mode(void)
{
  // lower case
  POKE(0xD018,0x16);

  // Normal text mode
  POKE(0xD054,0x00);
  // H640, fast CPU, extended attributes
  POKE(0xD031,0xE0);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016,0xC9);
  // 640x200 16bits per char, 16 pixels wide per char
  // = 640/16 x 16 bits = 80 bytes per row
  POKE(0xD058,80);
  POKE(0xD059,80/256);
  // Draw 80 chars per row
  POKE(0xD05E,80);
  // Put 2KB screen at $C000
  POKE(0xD060,0x00);
  POKE(0xD061,0xc0);
  POKE(0xD062,0x00);
  
  lfill(0xc000,0x20,2000);
  // Clear colour RAM
  lfill(0xff80000L,0x0E,2000);

}

void graphics_mode(void)
{
  // 16-bit text mode, full-colour text for high chars
  POKE(0xD054,0x05);
  // H320, fast CPU
  POKE(0xD031,0x40);
  // 320x200 per char, 16 pixels wide per char
  // = 320/8 x 16 bits = 80 bytes per row
  POKE(0xD058,80);
  POKE(0xD059,80/256);
  // Draw 40 chars per row
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
   
  // Clear colour RAM
  for(i=0;i<2000;i++) {
    lpoke(0xff80000L+0+i,0x00);
    lpoke(0xff80000L+1+i,0x00);
  }

  POKE(0xD020,0);
  POKE(0xD021,0);
}

void print_text(unsigned char x,unsigned char y,unsigned char colour,char *msg);

unsigned short pixel_addr;
unsigned char pixel_temp;
void plot_pixel(unsigned short x,unsigned char y,unsigned char colour)
{
  pixel_addr=(x&0x7)+64*25*(x>>3);
  pixel_addr+=y<<3;

  lpoke(0x50000L+pixel_addr,colour);

}

void plot_pixel_direct(unsigned short x,unsigned char y,unsigned char colour)
{
  pixel_addr=(x&0x7)+64*25*(x>>3);
  pixel_addr+=y<<3;

  lpoke(0x40000L+pixel_addr,colour);
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

void print_text80(unsigned char x,unsigned char y,unsigned char colour,char *msg)
{
  pixel_addr=0xC000+x+y*80;
  while(*msg) {
    char_code=*msg;
    if (*msg>=0xc0&&*msg<=0xe0) char_code=*msg-0x80;
    else if (*msg>=0x40&&*msg<=0x60) char_code=*msg-0x40;
    else if (*msg>=0x60&&*msg<=0x7A) char_code=*msg-0x20;
    POKE(pixel_addr+0,char_code);
    lpoke(0xff80000L-0xc000+pixel_addr,colour);
    msg++;
    pixel_addr+=1;
  }
}

void activate_double_buffer(void)
{
  lcopy(0x50000,0x40000,0x8000);
  lcopy(0x58000,0x48000,0x8000);
}

unsigned char fd;
int count;
unsigned char buffer[512];

unsigned long load_addr;

unsigned char line_dmalist[256];

void main(void)
{
  unsigned char ch,ofs;
  unsigned char slope_ofs,line_mode_ofs,cmd_ofs,count_ofs;
  unsigned char src_ofs,dst_ofs;
  unsigned char playing=0;

  asm ( "sei" );
  
  // Fast CPU, M65 IO
  POKE(0,65);
  POKE(0xD02F,0x47);
  POKE(0xD02F,0x53);

  while(PEEK(0xD610)) POKE(0xD610,0);
  
  POKE(0xD020,0);
  POKE(0xD021,0);

  // Stop all DMA audio first
  POKE(0xD720,0);
  POKE(0xD730,0);
  POKE(0xD740,0);
  POKE(0xD750,0);

  graphics_mode();
  graphics_clear_screen();

  graphics_clear_double_buffer();
  activate_double_buffer();

  print_text(0,0,1,"Line Draw Test");

  // Set up common structure of the DMA list
  ofs=0;
  // Screen layout is in vertical stripes, so we need only to setup the
  // X offset step.  64x25 = 
  line_dmalist[ofs++]=0x87;
  line_dmalist[ofs++]=(1600-8)&0xff;
  line_dmalist[ofs++]=0x88;
  line_dmalist[ofs++]=(1600-8)>>8;
  line_dmalist[ofs++]=0x8b;
  slope_ofs=ofs++; // remember where we have to put the slope in
  line_dmalist[ofs++]=0x8c;
  ofs++;
  line_dmalist[ofs++]=0x8f;
  line_mode_ofs=ofs++;
  line_dmalist[ofs++]=0x0a; // F018A list format
  line_dmalist[ofs++]=0x00; // end of options
  cmd_ofs=ofs++;  // command byte
  count_ofs=ofs; ofs+=2;
  src_ofs=ofs; ofs+=3;
  dst_ofs=ofs; ofs+=3;
  line_dmalist[ofs++]=0x00; // modulo
  line_dmalist[ofs++]=0x00;
  
  while(1) {
    long addr;
    int temp,slope,dx,dy;
    unsigned char colour=rand32(256);
    int x1=rand32(320);
    int y1=rand32(200);
    int x2=rand32(320);
    int y2=rand32(200);

    // Ignore if we choose to draw a point
    if (x2==x1&&y2==y1) continue;
        
    dx=x2-x1;
    dy=y2-y1;
    if (dx<0) dx=-dx;
    if (dy<0) dy=-dy;

    snprintf(msg,41,"(%d,%d) - (%d,%d)    ",x1,y1,x2,y2);
    print_text(0,1,1,msg);

    
    // Draw line from x1,y1 to x2,y2
    if (dx<dy) {
      // Y is major axis

      // Use hardware divider to get the slope
      POKE(0xD770,dx&0xff);
      POKE(0xD771,dx>>8);
      POKE(0xD772,0);
      POKE(0xD773,0);
      POKE(0xD774,dy&0xff);
      POKE(0xD775,dy>>8);
      POKE(0xD776,0);
      POKE(0xD777,0);

      // Wait 16 cycles
      POKE(0xD020,0);
      POKE(0xD020,0);
      POKE(0xD020,0);
      POKE(0xD020,0);

      // Slope is the most significant bytes of the fractional part
      // of the division result
      slope=PEEK(0xD76A)+(PEEK(0xD76B)<<8);

      slope=0x7fff;

      // Put slope into DMA options
      line_dmalist[slope_ofs]=slope&0xff;
      line_dmalist[slope_ofs+2]=slope&0xff;

      // Load DMA dest address with the address of the first pixel
      addr=0x40000+(y1<<3)+(x1&7)+(x1>>3)*64*25L;
      line_dmalist[dst_ofs+0]=addr&0xff;
      line_dmalist[dst_ofs+1]=addr>>8;
      line_dmalist[dst_ofs+2]=(addr>>16)&0xf;

      // Source is the colour
      line_dmalist[src_ofs]=colour&0xf;

      // Count is number of pixels, i.e., dy.
      line_dmalist[count_ofs]=dy&0xff;
      line_dmalist[count_ofs+1]=dy>>8;

      // Command is FILL
      line_dmalist[cmd_ofs]=0x03;

      // Line mode active, major axis is Y
      line_dmalist[line_mode_ofs]=0x80+0x40;

      POKE(0xD020,1);
      
      POKE(0xD701,((unsigned int)(&line_dmalist))>>8);
      POKE(0xD705,((unsigned int)(&line_dmalist))>>0);

      POKE(0xD020,0);
      
    } else {
      // X is major axis

      
      // Use hardware divider to get the slope
      POKE(0xD770,dy&0xff);
      POKE(0xD771,dy>>8);
      POKE(0xD772,0);
      POKE(0xD773,0);
      POKE(0xD774,dx&0xff);
      POKE(0xD775,dx>>8);
      POKE(0xD776,0);
      POKE(0xD777,0);

      // Wait 16 cycles
      POKE(0xD020,0);
      POKE(0xD020,0);
      POKE(0xD020,0);
      POKE(0xD020,0);

      // Slope is the most significant bytes of the fractional part
      // of the division result
      slope=PEEK(0xD76A)+(PEEK(0xD76B)<<8);

      // Put slope into DMA options
      line_dmalist[slope_ofs]=slope&0xff;
      line_dmalist[slope_ofs+2]=slope&0xff;

      // Load DMA dest address with the address of the first pixel
      addr=0x40000+(y1<<3)+(x1&7)+(x1>>3)*64*25;
      line_dmalist[dst_ofs+0]=addr&0xff;
      line_dmalist[dst_ofs+1]=addr>>8;
      line_dmalist[dst_ofs+2]=(addr>>16)&0xf;

      // Source is the colour
      line_dmalist[src_ofs]=colour&0xf;

      // Count is number of pixels, i.e., dy.
      line_dmalist[count_ofs]=dx&0xff;
      line_dmalist[count_ofs+1]=dx>>8;

      // Command is FILL
      line_dmalist[cmd_ofs]=0x03;

      // Line mode active, major axis is X
      line_dmalist[line_mode_ofs]=0x80+0x00;

      POKE(0xD020,1);
      
      POKE(0xD701,((unsigned int)(&line_dmalist))>>8);
      POKE(0xD705,((unsigned int)(&line_dmalist))>>0);

      POKE(0xD020,0);
      
    }

    //    while(!PEEK(0xD610)) continue;
    //    POKE(0xD610,0);
    
  }
  
  
}

