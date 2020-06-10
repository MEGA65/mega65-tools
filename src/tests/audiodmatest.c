#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>

#define MOD_TEST
#undef SINE_TEST
#undef DIRECT_TEST


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
  // Clear colour RAM, while setting all chars to 4-bits per pixel
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
  for(i=0;i<2000;i) {
    lpoke(0xff80000L+0+i,0x0E);
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
unsigned short instrument_loopstart[MAX_INSTRUMENTS];
unsigned short instrument_looplen[MAX_INSTRUMENTS];
unsigned char instrument_finetune[MAX_INSTRUMENTS];
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

// POPCORN.MOD
//char filename[16]={0x50,0x4f,0x50,0x43,0x4f,0x52,0x4e,0x2e,0x4d,0x4f,0x44,0x00,
//		   0x00,0x00,0x00,0x00  };
// AXELF.MOD
//char filename[16]={0x41,0x58,0x45,0x4c,0x46,0x2e,0x4d,0x4f,0x44,0x00,0x00,0x00,
//		   0x00,0x00,0x00,0x00 };
// SWEET2.MOD
char filename[16]={0x53,0x57,0x45,0x45,0x54,0x32,0x2e,0x4d,0x4f,0x44,0x00,0x00,
		   0x00,0x00,0x00,0x00 };


unsigned char current_pattern_in_song=0;
unsigned char current_pattern=0;
unsigned char current_pattern_position=0;

unsigned char screen_first_row=0;

unsigned ch_en[4]={1,1,1,1};

unsigned char pattern_buffer[16];
char note_fmt[9+1];

char *note_name(unsigned short period)
{
  switch(period) {
  case 0: return "---";
  case 856: return "C-1";
  case 808: return "C#1";
  case 762: return "D-1";
  case 720: return "D#1";
  case 678: return "E-1";
  case 640: return "F-1";
  case 604: return "F#1";
  case 570: return "G-1";
  case 538: return "G#1";
  case 508: return "A-1";
  case 480: return "A#1";
  case 453: return "B-1";

  case 428: return "C-2";
  case 404: return "C#2";
  case 381: return "D-2";
  case 360: return "D#2";
  case 339: return "E-2";
  case 320: return "F-2";
  case 302: return "F#2";
  case 285: return "G-2";
  case 269: return "G#2";
  case 254: return "A-2";
  case 240: return "A#2";
  case 226: return "B-2";

  case 214: return "C-3";
  case 202: return "C#3";
  case 190: return "D-3";
  case 180: return "D#3";
  case 170: return "E-3";
  case 160: return "F-3";
  case 151: return "F#3";
  case 143: return "G-3";
  case 135: return "G#3";
  case 127: return "A-3";
  case 120: return "A#3";
  case 113: return "B-3";

  default: return "???";
  }
}

void format_note(unsigned char *n)
{
  snprintf(note_fmt,9,"%s%X%02X%02X",
	   note_name(((n[0]&0xf)<<8)|n[1]),
	   n[0]>>4,n[2],n[3]);
}

void draw_pattern_row(unsigned char screen_row,
		      unsigned char pattern_row,
		      unsigned char colour)
{
  unsigned char c;
  // Get pattern row
  lcopy(0x40000+1084+(current_pattern<<10)+(pattern_row<<4),pattern_buffer,16);
  // Draw row number
  snprintf(note_fmt,9,"%02d",pattern_row);
  print_text80(0,screen_row,0x01,note_fmt);
  // Draw the four notes
  c=ch_en[0]?colour:2;
  format_note(&pattern_buffer[0]);
  print_text80(4,screen_row,c,note_fmt);
  c=ch_en[1]?colour:2;
  format_note(&pattern_buffer[4]);
  print_text80(13,screen_row,c,note_fmt);
  c=ch_en[2]?colour:2;
  format_note(&pattern_buffer[8]);
  print_text80(22,screen_row,c,note_fmt);
  c=ch_en[3]?colour:2;
  format_note(&pattern_buffer[12]);
  print_text80(31,screen_row,c,note_fmt);
}

void show_current_position_in_song(void)
{
  if (current_pattern_position<screen_first_row)
    screen_first_row=current_pattern_position;
  while (current_pattern_position>(screen_first_row+16)) {
    screen_first_row+=16;
  }
  if (screen_first_row>(63-(25-5)))
    screen_first_row=(63-(25-5));
  
  for(i=5;i<25;i++)
    {
      draw_pattern_row(i,screen_first_row+i-5,
		       ((screen_first_row+i-5)==current_pattern_position)?0x27:0x0c);
    }
    
}
  


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
  POKE(0xD729+ch_ofs,instrument_vol[instrument]>>1);
  
  // XXX - We should set base addr and top addr to the looping range, if the
  // sample has one.
  if (instrument_loopstart[instrument]) {
    // start of loop
    POKE(0xD721+ch_ofs,(((unsigned long)instrument_addr[instrument]+2*instrument_loopstart[instrument])>>0)&0xff);
    POKE(0xD722+ch_ofs,(((unsigned long)instrument_addr[instrument]+2*instrument_loopstart[instrument])>>8)&0xff);
    POKE(0xD723+ch_ofs,(((unsigned long)instrument_addr[instrument]+2*instrument_loopstart[instrument])>>16)&0xff);

    // Top addr
    POKE(0xD727+ch_ofs,(((unsigned short)instrument_addr[instrument]
			 +2*(instrument_loopstart[instrument]+instrument_looplen[instrument]-1))>>0)&0xff);
    POKE(0xD728+ch_ofs,(((unsigned short)instrument_addr[instrument]
			 +2*(instrument_loopstart[instrument]+instrument_looplen[instrument]-1))>>8)&0xff);
    
  }
  
  POKE(0xC050+channel,instrument);
  POKE(0xC0A0+channel,instrument_vol[instrument]);
  
  // Calculate time base.
  // XXX Here we use a slightly randomly chosen fudge-factor
  // It should be:
  // SPEED = SAMPLE RATE * 0.414252
  // But I don't (yet) know how to get the fundamental frequency of a sample
  // from a MOD file

  // The natural timebase for MOD files is ~3.5MHz
  // This means we need a time-base of ~11.42 for PAL and ~11.31 for
  // NTSC
  // Here the MEGA65's 25x18 hardware multiplier comes in handy.
  // 11.42x ~= 748316 / 65536   = $B6B1C / $10000
  // 11.31x ~= 741494 / 65536   = $B5075 / $10000

  // XXX Some samples manifestly require a slower play back rate,
  // but this does not seem to be encoded anywhere!? The "BOMI"
  // sample in POPCORN.MOD is an example of this
  
  // time_base=freq * 11.41;
  POKE(0xC0F0+channel,instrument_finetune[instrument]);
  if (instrument_finetune[instrument]) {
    // We really should have a proper fix for the fine tune byte.
    // For now we are just fudging things
    POKE(0xD770,0x1C);
    POKE(0xD771,0x6B);
    POKE(0xD772,0x0B);
    POKE(0xD773,0x00);
  } else {
    POKE(0xD770,0x1C);
    POKE(0xD771,0x6B);
    POKE(0xD772,0x0B);
    POKE(0xD773,0x00);
  }
  POKE(0xD774,freq<<(2));
  POKE(0xD775,freq>>(6));
  POKE(0xD776,freq>>(14));
  
  // Picking result from 2 bytes up does the divide by 65536
  POKE(0xD724+ch_ofs,PEEK(0xD77A));
  POKE(0xD725+ch_ofs,PEEK(0xD77B));
  POKE(0xD726+ch_ofs,PEEK(0xD77C));
  
  if (instrument_loopstart[instrument]) {
  // Enable playback+ nolooping of channel 0, 8-bit, no unsigned samples
  POKE(0xD720+ch_ofs,0xC0);
  } else {
    // Enable playback+ nolooping of channel 0, 8-bit, no unsigned samples
    POKE(0xD720+ch_ofs,0x80);
  }

  // Enable audio dma, enable bypass of audio mixer, signed samples
  POKE(0xD711,0x90);
  
}

void play_note(unsigned char channel,unsigned char *note)
{
  unsigned char instrument;
  unsigned short frequency;
  unsigned short effect;

  instrument=note[0]&0xf0;
  instrument|=note[2]>>4;
  instrument--;
  frequency=((note[0]&0xf)<<8)+note[1];
  effect=((note[2]&0xf)<<8)+note[3];

  frequency=frequency>>2;
  
  if (frequency) 
    play_sample(channel,instrument,frequency,effect);  
  
}

void play_mod_pattern_line(void)
{
  // Get pattern row
  lcopy(0x40000+1084+(current_pattern<<10)+(current_pattern_position<<4),pattern_buffer,16);
  if (ch_en[0]) play_note(0,&pattern_buffer[0]); else POKE(0xC050+0,0x18);
  if (ch_en[1]) play_note(1,&pattern_buffer[4]); else POKE(0xC050+1,0x18);
  if (ch_en[2]) play_note(2,&pattern_buffer[8]); else POKE(0xC050+2,0x18);
  if (ch_en[3]) play_note(3,&pattern_buffer[12]); else POKE(0xC050+3,0x18);
}

void play_sine(unsigned char ch, unsigned long time_base)
{
  unsigned ch_ofs=ch<<4;
  
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
  POKE(0xD720+ch_ofs,0xE0);
  // Enable audio dma
  POKE(0xD711+ch_ofs,0x80);

  // time base = $001000
  POKE(0xD724+ch_ofs,time_base&0xff);
  POKE(0xD725+ch_ofs,time_base>>8);
  POKE(0xD726+ch_ofs,time_base>>16);
  
  
}

void wait_frames(unsigned char n)
{
  while(n) {
    while (PEEK(0xD012)!=0x80) continue;
    while (PEEK(0xD012)==0x80) continue;
    n--;
  }
}

char msg[64+1];

void main(void)
{
  unsigned char ch;
  unsigned char playing=0;
  unsigned char tempo=4;

  // Fast CPU, M65 IO
  POKE(0,65);
  POKE(0xD02F,0x47);
  POKE(0xD02F,0x53);

  POKE(0xD020,0);
  POKE(0xD021,0);

  // Stop all DMA audio first
  POKE(0xD720,0);
  POKE(0xD730,0);
  POKE(0xD740,0);
  POKE(0xD750,0);
  
#ifdef DIRECT_TEST
  while(1) {
    for(top_addr=0;top_addr<32;top_addr++) {
      POKE(0xD6F9,sin_table[top_addr]);
      POKE(0xD6FB,sin_table[top_addr]);
      for(i=0;i<100;i++) continue;
      POKE(0xD020,(PEEK(0xD020)+1)&0x0f);
    }
  }  
#endif
  
#ifdef MOD_TEST

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

  h640_text_mode();
  lfill(0xc000,0x20,2000);
  
  lcopy(0x40000,mod_name,20);
  mod_name[20]=0;
  
  // Show MOD file name
  print_text80(0,0,1,mod_name);

  // Show  instruments from MOD file
  for(i=0;i<31;i++)
    {
      lcopy(0x40014+i*30,mod_name,22);
      mod_name[22]=0;
      if (mod_name[0]) {
	if (i+5<25)
	  print_text80(57,i+5,0x0c,mod_name);
	//	printf("Instr#%d is %s\n",i,mod_name);
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
      instrument_finetune[i]=mod_name[2];

      // Instrument volume
      instrument_vol[i]=mod_name[3];

      // Repeat start point and end point
      instrument_loopstart[i]=mod_name[5]+(mod_name[4]<<8);      
      instrument_looplen[i]=mod_name[7]+(mod_name[6]<<8);
      POKE(0xC048+(i+5)*80,mod_name[5]);
      POKE(0xC049+(i+5)*80,mod_name[4]);
      POKE(0xC04A+(i+5)*80,mod_name[7]);
      POKE(0xC04B+(i+5)*80,mod_name[6]);
    }

  song_length=lpeek(0x40000+950);
  song_loop_point=lpeek(0x40000+951);
  //  printf("Song length = %d, loop point = %d\n",
  //	 song_length,song_loop_point);
  lcopy(0x40000+952,song_pattern_list,128);
  for(i=0;i<song_length;i++) {
    //    printf(" $%02x",song_pattern_list[i]);
    if (song_pattern_list[i]>max_pattern) max_pattern=song_pattern_list[i];
  }
  //  printf("\n%d unique patterns.\n",max_pattern);
  sample_data_start=0x40000L+1084+(max_pattern+1)*1024;
  
  //  printf("sample data starts at $%lx\n",sample_data_start);
  for(i=0;i<MAX_INSTRUMENTS;i++) {
    instrument_addr[i]=sample_data_start;    
    sample_data_start+=instrument_lengths[i];
    //    printf("Instr #%d @ $%05lx\n",i,instrument_addr[i]);
  }

  current_pattern_in_song=0;
  current_pattern=song_pattern_list[0];
  current_pattern_position=0;
  
  show_current_position_in_song();

  while(1) {
    if (PEEK(0xD610)) {
      switch(PEEK(0xD610)) {
      case 0x4d: case 0x6d:
	// M - Toggle master enable
       	POKE(0xD711,PEEK(0xD711)^0x80);
	break;
      case 0x31: ch_en[0]^=1; show_current_position_in_song(); break;
      case 0x32: ch_en[1]^=1; show_current_position_in_song(); break;
      case 0x33: ch_en[2]^=1; show_current_position_in_song(); break;
      case 0x34: ch_en[3]^=1; show_current_position_in_song(); break;
      case 0x30:
	// 0 - Reset song to start
	current_pattern_in_song=0;
	current_pattern=song_pattern_list[0];
	current_pattern_position=0;
	
	show_current_position_in_song();
	
	break;
      case 0x50: case 0x70:
	// P - Play current note
	playing^=1;
	break;
      case 0x9d:
	current_pattern_in_song--;
	if (current_pattern_in_song>=song_length) current_pattern_in_song=0;
	current_pattern=song_pattern_list[current_pattern_in_song];
	show_current_position_in_song();
	break;
      case 0x1d:
	current_pattern_in_song++;
	if (current_pattern_in_song==song_length) current_pattern_in_song=0;
	current_pattern=song_pattern_list[current_pattern_in_song];
	show_current_position_in_song();
	break;
      case 0x11:
	current_pattern_position++;
	if (current_pattern_position>0x3f ) {
	  current_pattern_position = 0x00;
	  current_pattern_in_song++;
	  if (current_pattern_in_song==song_length) current_pattern_in_song=0;
	  current_pattern=song_pattern_list[current_pattern_in_song];
	  current_pattern_position=0;
	}	
	show_current_position_in_song();
	break;
      case 0x91:
	current_pattern_position--;
	if (current_pattern_position>0x3f ) {
	  current_pattern_position = 0x3f;
	  current_pattern_in_song--;
	  if (current_pattern_in_song<0) current_pattern_in_song=0;
	  current_pattern=song_pattern_list[current_pattern_in_song];
	  current_pattern_position=0;
	}	
	show_current_position_in_song();
	break;
      case 0x20:
	playing=2;
	break;
      case '+':
	tempo--;
	if (tempo==0xff) tempo=0;
	break;
      case '-':
	tempo++;
	if (tempo==0) tempo=0xff;
	break;
      }      
      if (PEEK(0xD610)>=0x61&&PEEK(0xD610)<0x6d) {
	play_sample(0,PEEK(0xD610)&0xf,200,0);
	POKE(0xD020,PEEK(0xD610)&0xf);
      }
      if (PEEK(0xD610)>=0x41&&PEEK(0xD610)<0x4d) {
	play_sample(0,PEEK(0xD610)&0xf,100,0);
	POKE(0xD020,PEEK(0xD610)&0xf);
      }
      if (PEEK(0xD610)==0x40) {
	play_sample(0,PEEK(0xD610)&0xf,200,0);
	POKE(0xD020,PEEK(0xD610)&0xf);
      }	
      POKE(0xD610,0);
    }

    for(i=0;i<4;i++) {
      // Display Audio DMA channel
      snprintf(msg,64,"%x: e=%x l=%x p=%x st=%x v=$%02x cur=$%02x%02x%02x tb=$%02x%02x%02x ct=$%02x%02x%02x",
	     i,
	     (PEEK(0xD720+i*16+0)&0x80)?1:0,
	     (PEEK(0xD720+i*16+0)&0x40)?1:0,
	     (PEEK(0xD720+i*16+0)&0x10)?1:0,
	     (PEEK(0xD720+i*16+0)&0x08)?1:0,
	     PEEK(0xD729+i*16),
	     PEEK(0xD72C+i*16),PEEK(0xD72B+i*16),PEEK(0xD72A+i*16),
	     PEEK(0xD726+i*16),PEEK(0xD725+i*16),PEEK(0xD724+i*16),
	     PEEK(0xD72F+i*16),PEEK(0xD72E+i*16),PEEK(0xD72D+i*16));
      print_text80(16,i,15,msg);
    }

    
    if (playing) {
      play_mod_pattern_line();
      wait_frames(tempo);
      current_pattern_position++;
      if (current_pattern_position>0x3f ) {
	current_pattern_position = 0x00;
	current_pattern_in_song++;
	if (current_pattern_in_song==song_length) current_pattern_in_song=0;
	current_pattern=song_pattern_list[current_pattern_in_song];
	current_pattern_position=0;
      }
      show_current_position_in_song();
    }
    playing&=0x1;
  }
  
#endif
  
#ifdef SINE_TEST

  play_sine(0,time_base);
  
  printf("%c",0x93);
  while(1) {

    printf("%c",0x13);
    
    for(i=0;i<4;i++)
      {
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
    if (PEEK(0xD610)) {
      switch (PEEK(0xD610)) {
      case 0x30: ch=0; break; 
      case 0x31: ch=1; break;
      case 0x32: ch=2; break;
      case 0x33: ch=3; break;
      case 0x11: time_base--; break;
      case 0x91: time_base++; break;
      case 0x1d: time_base-=0x100; break;
      case 0x9d: time_base+=0x100; break;	
      }
      time_base&=0x0fffff;

      POKE(0xD720,0);
      POKE(0xD730,0);
      POKE(0xD740,0);
      POKE(0xD750,0);
      play_sine(ch,time_base);
       
      POKE(0xD610,0);      
    }

    POKE(0x400+999,PEEK(0x400+999)+1);
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
  POKE(0xD711,0x80);
  // Enable playback+looping of channel 0, 16-bit samples
  //  POKE(0xD720,0xC3);
  
  
  //  graphics_mode();
  //  print_text(0,0,1,"");

  printf("%c",0x93);

  while(1) {
    printf("%c",0x13);            
    
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

