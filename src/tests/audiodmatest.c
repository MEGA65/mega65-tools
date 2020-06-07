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
  lfill(0x40000L,0,32768L);
  lfill(0x48000L,0,32768L);
}

void graphics_clear_double_buffer(void)
{
  lfill(0x50000L,0,32768L);
  lfill(0x58000L,0,32768L);
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


unsigned char fd;
int count;
unsigned char buffer[512];

unsigned long load_addr;

unsigned char mod_name[23];

#define MAX_INSTRUMENTS 32
unsigned short instrument_lengths[MAX_INSTRUMENTS];
unsigned short instrument_[MAX_INSTRUMENTS];
unsigned long instrument_addr[MAX_INSTRUMENTS];
unsigned char instrument_vol[MAX_INSTRUMENTS];

unsigned char song_length;
unsigned char song_loop_point;
unsigned char song_pattern_list[128];
unsigned char max_pattern=0;
unsigned long sample_data_start=0x40000;

unsigned long time_base=0;

unsigned char sin_table[32]={
  //  128,177,218,246,255,246,218,177,
  //  128,79,38,10,0,10,38,79
  128,152,176,198,217,233,245,252,255,252,245,233,217,198,176,152,128,103,79,57,38,22,10,3,1,3,10,22,38,57,79,103
};

// CC65 PETSCII conversion is a pain, so we provide the exact bytes of the file name
char filename[16]={0x50,0x4f,0x50,0x43,0x4f,0x52,0x4e,0x2e,0x4d,0x4f,0x44,0x00,
		   0x00,0x00,0x00,0x00
};


unsigned short top_addr;

void play_sample(unsigned char channel,
		 unsigned char instrument,
		 unsigned short freq,
		 unsigned short effect)
{
  unsigned ch_ofs=channel<<4;
  
  // Stop playback while loading new sample data
  POKE(0xD720+ch_ofs,0x00);
  // Load sample address into base and current addr
  POKE(0xD721+ch_ofs,(((unsigned short)instrument_addr[instrument])>>0)&0xff);
  POKE(0xD722+ch_ofs,(((unsigned short)instrument_addr[instrument])>>8)&0xff);
  POKE(0xD723+ch_ofs,(((unsigned long)instrument_addr[instrument])>>16)&0xff);
  POKE(0xD72A+ch_ofs,(((unsigned short)instrument_addr[instrument])>>0)&0xff);
  POKE(0xD72B+ch_ofs,(((unsigned short)instrument_addr[instrument])>>8)&0xff);
  POKE(0xD72C+ch_ofs,(((unsigned long)instrument_addr[instrument])>>16)&0xff);
  // Sample top address
  top_addr=instrument_addr[instrument]+instrument_lengths[instrument];
  POKE(0xD727+ch_ofs,(top_addr>>0)&0xff);
  POKE(0xD728+ch_ofs,(top_addr>>8)&0xff);
  // Volume
  POKE(0xD729,instrument_vol[instrument]);
  // Load sample address into base and current addr

  // Calculate time base.
  // XXX Here we use a slightly randomly chosen fudge-factor
  // It should be:
  // SPEED = SAMPLE RATE * 0.414252
  // But I don't (yet) know how to get the fundamental frequency of a sample
  // from a MOD file
  time_base=freq*10;
  time_base&=0xfffff;
  POKE(0xD724+ch_ofs,(time_base>>0)&0xff);
  POKE(0xD725+ch_ofs,(time_base>>8)&0xff);
  POKE(0xD726+ch_ofs,(time_base>>16)&0xff);
  
  // Enable playback+looping of channel 0, 8-bit samples
  POKE(0xD720+ch_ofs,0xC0);
  // Enable audio dma
  POKE(0xD711,0x80);
  
}

void main(void)
{
  // Fast CPU, M65 IO
  POKE(0,65);
  POKE(0xD02F,0x47);
  POKE(0xD02F,0x53);

  // Load a MOD file for testing
  closeall();
  fd=open(filename); 
  if (fd==0xff) {
    printf("Could not read MOD file\n");
    return;
  }
  load_addr=0x40000;
  while((count=read512(buffer))>0) {
    if (count>512) break;
    lcopy((unsigned long)buffer,(unsigned long)load_addr,512);
    POKE(0xD020,(PEEK(0xD020)+1)&0xf);
    load_addr+=512;
    
    if (count<512) break;
  }

  printf("%c",0x93);

  lcopy(0x40000,mod_name,20);
  mod_name[20]=0;
  
  // Show MOD file name
  printf("%s\n",mod_name);
  
  // Show  instruments from MOD file
  for(i=0;i<31;i++)
    {
      lcopy(0x40014+i*30,mod_name,22);
      mod_name[22]=0;
      if (mod_name[0]) {
	printf("Instr#%d is %s\n",i,mod_name);
      }
      // Get instrument data for plucking
      lcopy(0x40014+i*30+22,mod_name,22);
      instrument_lengths[i]=mod_name[1]+(mod_name[0]<<8);
      if ((instrument_lengths[i]&0x8000)) {
	printf("ERROR: MOD file has samples >64KB\n");
	return;	
      }
      // Redenominate instrument length into bytes
      instrument_lengths[i]<<=1;
      instrument_vol[i]=mod_name[3];
    }

  song_length=lpeek(0x40000+950);
  song_loop_point=lpeek(0x40000+951);
  printf("Song length = %d, loop point = %d\n",
	 song_length,song_loop_point);
  lcopy(0x40000+952,song_pattern_list,128);
  for(i=0;i<song_length;i++) {
    printf(" $%02x",song_pattern_list[i]);
    if (song_pattern_list[i]>max_pattern) max_pattern=song_pattern_list[i];
  }
  printf("\n%d unique patterns.\n",max_pattern);
  sample_data_start=0x40000L+1084+(max_pattern+1)*1024;
  printf("sample data starts at $%lx\n",sample_data_start);
  for(i=0;i<MAX_INSTRUMENTS;i++) {
    instrument_addr[i]=sample_data_start;
    sample_data_start+=instrument_lengths[i];
    //    printf("Instr #%d @ $%05lx\n",i,instrument_addr[i]);
  }

#ifdef SINE_TEST
  // Play sine wave for frequency matching
  POKE(0xD721,((unsigned short)&sin_table)&0xff);
  POKE(0xD722,((unsigned short)&sin_table)>>8);
  POKE(0xD723,0);
  // 16 bytes long
  POKE(0xD727,((unsigned short)&sin_table+32)&0xff);
  POKE(0xD728,((unsigned short)&sin_table+32)>>8);
  // Full volume
  POKE(0xD729,0xFF);
  // Enable playback+looping of channel 0, 8-bit samples
  POKE(0xD720,0xC0);
  // Enable audio dma
  POKE(0xD711,0x80);

  printf("%c",0x93);
  while(1) {

    printf("%c",0x13);
    
    // time base = $001000
    POKE(0xD724,time_base&0xff);
    POKE(0xD725,time_base>>8);
    POKE(0xD726,time_base>>16);

    i=0;
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
    if (PEEK(0xD610)) {
      switch (PEEK(0xD610)) {
      case 0x11: time_base--; break;
      case 0x91: time_base++; break;
      case 0x1d: time_base-=0x100; break;
      case 0x9d: time_base+=0x100; break;	
      }
      POKE(0xD610,0);      
    }
  }
#endif

  // base addr = $040000
  POKE(0xD721,0x00);
  POKE(0xD722,0x00);
  POKE(0xD723,0x04);
  // time base = $001000
  POKE(0xD724,0x01);
  POKE(0xD725,0x10);
  POKE(0xD726,0x00);
  // Top address
  POKE(0xD727,0xFE);
  POKE(0xD728,0xFF);
  // Full volume
  POKE(0xD729,0xFF);
  // Enable audio dma, 16 bit samples
  POKE(0xD711,0x90);
  // Enable playback+looping of channel 0, 16-bit samples
  //  POKE(0xD720,0xC3);
  
  
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
      if (PEEK(0xD610)>=0x61&&PEEK(0xD610)<0x6d) {
	play_sample(0,PEEK(0xD610)&0xf,200,0);
	POKE(0xD020,PEEK(0xD610)&0xf);
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

