#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>

char msg[64+1];

unsigned short i;
unsigned char a,b,c,d;
unsigned char vol=0x01;

unsigned char sin_table[32]={
  //  128,177,218,246,255,246,218,177,
  //  128,79,38,10,0,10,38,79
  128,152,176,198,217,233,245,252,255,252,245,233,217,198,176,152,128,103,79,57,38,22,10,3,1,3,10,22,38,57,79,103
};

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
  for(i=0;i<2000;i++) {
    lpoke(0xff80000L+0+i,0x08);
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

void plot_pixel_direct(unsigned short x,unsigned char y,unsigned char colour)
{
  pixel_addr=((x&0xf)>>1)+64*25*(x>>4);
  pixel_addr+=y<<3;

  pixel_temp=lpeek(0x40000L+pixel_addr);
  if (x&1) {
    pixel_temp&=0x0f;
    pixel_temp|=colour<<4;
  } else {
    pixel_temp&=0xf0;
    pixel_temp|=colour&0xf;
  }
  lpoke(0x40000L+pixel_addr,pixel_temp);

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

unsigned long frequency=200;
unsigned long new_freq=200;
unsigned long time_base=0x4000;
unsigned char mic_num=3;

void audioxbar_setcoefficient(uint8_t n,uint8_t value)
{
  // Select the coefficient
  POKE(0xD6F4,n);

  // Now wait at least 16 cycles for it to settle
  POKE(0xD020U,PEEK(0xD020U));
  POKE(0xD020U,PEEK(0xD020U));

  POKE(0xD6F5U,value); 
}

void play_sine(unsigned char ch,unsigned char vol)
{
  unsigned ch_ofs=ch<<4;

  /* Work out time_base from frequency
     time_base=0xffffff = 40.5MHz / sample length = 40.5M/32 = 1.265625 MHz
     0xfffff is the highest practical frequency we can do via DMA like this,
     which is about 79,101 Hz.
     This means time_base needs to be 13.2562x the frequency.
     As the max frequency is <2^17, we can do the multiplication as 
     TIMEBASE * 2^12 = FREQ * (13.2562 * 2^12)
                     = FREQ * 54297
  */
  time_base=frequency*54297L;
  time_base=time_base>>12;
  
  if (ch>3) return;
  
  // Play sine wave for frequency matching
  // We use the hardware ROM sine table for this.

#if 0
  POKE(0xD721+ch_ofs,0x00);
  POKE(0xD722+ch_ofs,0x00);
  POKE(0xD723+ch_ofs,0);
  POKE(0xD72A+ch_ofs,0x00);
  POKE(0xD72B+ch_ofs,0x00);
  POKE(0xD72C+ch_ofs,0);
  // 32 bytes long
  POKE(0xD727+ch_ofs,0x20);
  POKE(0xD728+ch_ofs,0x00);
  // Enable playback+looping of channel 0, 8-bit samples, signed, play ROM sine
  POKE(0xD720+ch_ofs,0xF0);
#endif
  
  POKE(0xD720+ch_ofs,0x00);
  POKE(0xD721+ch_ofs,((unsigned short)&sin_table)&0xff);
  POKE(0xD722+ch_ofs,((unsigned short)&sin_table)>>8);
  POKE(0xD723+ch_ofs,0);
  POKE(0xD72A+ch_ofs,((unsigned short)&sin_table)&0xff);
  POKE(0xD72B+ch_ofs,((unsigned short)&sin_table)>>8);
  POKE(0xD72C+ch_ofs,0);
  POKE(0xD727+ch_ofs,((unsigned short)&sin_table+32)&0xff);
  POKE(0xD728+ch_ofs,((unsigned short)&sin_table+32)>>8);
  // Enable playback+looping of channel 0, 8-bit samples, signed, use soft table
  POKE(0xD720+ch_ofs,0xE0);

  
  // Set volume
  POKE(0xD729+ch_ofs,vol);

  // Enable audio dma
  // And bypass audio mixer for direct 100% volume output
  POKE(0xD711,0x90);

  // Maximise volume level on amplifier

  // Set left channel to +0dB ($00 would be +24dB, but then we get lots of background noise)
  for(i=0;i<10000;i++) continue;
  lpoke(0xffd7035,0x40);
  for(i=0;i<10000;i++) continue;
  // Right channel is not used, so mute to avoid over-current
  lpoke(0xffd7036,0xFF); 
  for(i=0;i<10000;i++) continue;
  
  
  // time base = $001000
  POKE(0xD724+ch_ofs,time_base&0xff);
  POKE(0xD725+ch_ofs,time_base>>8);
  POKE(0xD726+ch_ofs,time_base>>16);
  
  
}

unsigned char last_samples[256];
unsigned char samples[256];
unsigned char last_mic_samples[256];
unsigned char mic_samples[256];

void main(void)
{
  unsigned char ch;
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

  // Turn on power to microphones
  
  
  play_sine(0,vol);
  
  //  printf("%c",0x93);

  graphics_mode();
  graphics_clear_screen();

  graphics_clear_double_buffer();
  activate_double_buffer();

  print_text(0,0,1,"Ultrasound Test");

  while(1) {

    snprintf(msg,64,"Freq = %5ld Hz, vol=$%02x",
	     frequency,vol);
    print_text(0,1,7,msg);
    snprintf(msg,64,"New Freq: %ld          ",new_freq);
    print_text(0,2,12,msg);

    // Read back digital audio channel
    POKE(0xD6F4,0x08);  

    // a=0;
    a=&sin_table;
    
    if (frequency) {
      // Synchronise to start of wave
     
      while(PEEK(0xD72A)==a) continue;
      while(PEEK(0xD72A)!=a) continue;
    }
    
    // Read a bunch of samples
    do {
      samples[a]=PEEK(0xD6FD);      
      a++;
    } while(a);


    // Read back MEMS microphone 2
    POKE(0xD6F4,0x0A+mic_num);  

    // a=0;
    a=&sin_table;
    
    if (frequency) {
      // Synchronise to start of wave
      while(PEEK(0xD72A)==a) continue;
      while(PEEK(0xD72A)!=a) continue;
    }
    
    // Read a bunch of samples
    a=0;
    do {
      // XXX Read LSB of microphone, while testing
      mic_samples[a]=PEEK(0xD6FC);      
      a++;
    } while(a);    
    
    // Update oscilloscope
    for(i=0;i<256;i++) {
      b=last_samples[i]^0x80;
      b=b>>1;
      plot_pixel_direct(i,(200-129)+b,0);

      b=last_mic_samples[i]^0x80;
      b=b>>1;
      plot_pixel_direct(i,(200-129)+b,0);

      b=samples[i]^0x80;
      b=b>>1;
      plot_pixel_direct(i,(200-129)+b,1);

      b=mic_samples[i]^0x80;
      b=b>>1;
      plot_pixel_direct(i,(200-129)+b,7);
      
      last_samples[i]=samples[i];
      last_mic_samples[i]=mic_samples[i];
    }
    // XXX We can't use DMA during DMA audio, or it causes pauses
    // (or at least not big DMAs.)
    //    lcopy(samples,last_samples,256);
    //    activate_double_buffer();
    
    if (PEEK(0xD610)) {
      if (PEEK(0xD610)>='0'&&PEEK(0xD610)<='9') {
	new_freq=new_freq*10+(PEEK(0xD610)-'0');
      }
      switch (PEEK(0xD610)) {
      case 0x14: new_freq/=10; break;
      case 0x0d: frequency=new_freq;
	// RETURN = set frequency
	new_freq=0;
	break;
      case 0x4e: case 0x6e:
	// N = next mic
	mic_num++;
	mic_num&=3;
	break;
      case 0x56: case 0x76:
	// V = set volume
	if (new_freq<256) vol=new_freq;
	new_freq=0;
	break;
      case '+': vol++; break;
      case '-': vol--; break;
      case 0x4d: case 0x6d: vol=0; break;
      case 0x11: frequency-=50; break;
      case 0x91: frequency-=50; break;
      case 0x1d: frequency+=50; break;
      case 0x9d: frequency+=50; break;	
      }
      frequency&=0xffff;
      if (!frequency) frequency=20;

      play_sine(0,vol);
       
      POKE(0xD610,0);      
    }

    //    POKE(0xD020,(PEEK(0xD020)+1)&0xf);
  }
  
  
}

