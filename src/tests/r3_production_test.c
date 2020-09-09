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

unsigned char histo_bins[640];
char peak_msg[40+1];
unsigned char random_target=40;
unsigned char last_random_target=40;
unsigned int random_seek_count=0;
unsigned char request_track=40;
unsigned char read_sectors[41]={0};
unsigned char last_track_seen=255;
unsigned int histo_samples=0;

void seek_random_track(void)
{
  // Seek to random track.
  last_random_target=random_target;
  random_target=PEEK(0xD012)%80;
  POKE(0xD084,request_track);
  a=(PEEK(0xD6A3)&0x7f)-random_target;
  if (a&0x80) {
    while(a) {
      POKE(0xD081,0x18);
      while (PEEK(0xD082)&0x80) continue;
      a++;
    }
  } else {
    while(a) {
      POKE(0xD081,0x10);
      while ((PEEK(0xD082)&0x80)) continue;
      //      usleep(6000);
      a--;
    }
  }
}


void gap_histogram(void)
{

  // Floppy motor on
  POKE(0xD080,0x60);  
  
  graphics_mode();

  print_text(0,0,1,"Magnetic Domain Interval Histogram");

  random_target=(PEEK(0xD6A3)&0x7f);
  
  while(1) {
    // Clear histogram bins
    for(i=0;i<640;i++) histo_bins[i]=0;
    histo_samples=0;

    // Get new histogram data
    while(1) {
      get_interval();
      if (interval_length>=640) continue;
      // Stop as soon as a single histogram bin fills
      if (histo_bins[interval_length]==255) {
	snprintf(peak_msg,40,"Peak @ %d, auto_tune=%d     ",
		 interval_length,PEEK(0xD689)&0x10);
	print_text(0,2,7,peak_msg);
	break;
      }
      histo_bins[interval_length]++;
      histo_samples++;
      if (histo_samples==4096) break;
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

    snprintf(peak_msg,40,"FDC Status = $%02X,$%02X, requested T:$%02x",
	     PEEK(0xD082),PEEK(0xD083),request_track);
    print_text(0,3,7,peak_msg);
    snprintf(peak_msg,40,"Last sector           T:$%02X S:%02X H:%02x",
	     PEEK(0xD6A3),PEEK(0xD6A4),PEEK(0xD6A5)
	     );
    if ((PEEK(0xD6A3)&0x7f)!=last_track_seen) {
      last_track_seen=PEEK(0xD6A3)&0x7f;
      // Zero out list of sectors seen
      snprintf(read_sectors,41,"Sectors read:      ....................");
    } else {
      // Note this sector
      read_sectors[18+(PEEK(0xD6A5)&0x1)*10+(PEEK(0xD6A4)&0x1f)]=0x52;
      POKE(0xD080,PEEK(0xD080)&0xf7+(PEEK(0xD012)&8));      
    }
    read_sectors[40]=0;
    print_text(0,6,5,read_sectors);
    
    print_text(0,4,7,peak_msg);
    snprintf(peak_msg,40,"Target track %-5d is T:$%02X, prev $%02X",
	     random_seek_count,
	     random_target,last_random_target);
    print_text(0,5,7,peak_msg);

    if ((PEEK(0xD6A3)&0x7f)==random_target) {
      random_seek_count++;
      seek_random_track();
    }
    
    
    activate_double_buffer();

    if (PEEK(0xD610)) {
      switch(PEEK(0xD610)) {
      case 0x11: case '-': POKE(0xD081,0x10); break;
      case 0x91: case '+': POKE(0xD081,0x18); break;
      case '0':
	request_track=0;
	break;
      case '4':
	request_track=40;
	break;
      case '8':
	request_track=80;
	break;
      case '1':
	request_track=81;
	break;
      case 0x9d:
	request_track--;
	break;
      case 0x1d:
	request_track++;
	break;
      case 0x20:
	last_random_target=random_target;
	random_target=255;
	break;
      case 0x41: case 0x61:
	// Switch auto/manual tracking in FDC
	POKE(0xD689,PEEK(0xD689)|0x10);
	break;
      case 0x4d: case 0x6d:
	POKE(0xD689,PEEK(0xD689)&0xEF);
	break;
      case 0x52: case 0x72:
	// Schedule a sector read
	POKE(0xD081,0x00); // Cancel previous action

	// Select track, sector, side
	POKE(0xD084,request_track);
	POKE(0xD085,1);
	POKE(0xD086,0);
	
	// Issue read command
	POKE(0xD081,0x40);
	
	break;
      case 0x53: case 0x73:
	random_seek_count=0;
	seek_random_track();
	break;
      }
      POKE(0xD610,0);
    }
  }
}

char read_message[41];

void read_all_sectors()
{
  unsigned char t,s,h;
  unsigned char xx,yy,y;
  unsigned int x;
  
  // Floppy motor on
  POKE(0xD080,0x60);  

  // Enable auto-tracking
  POKE(0xD689,PEEK(0xD689)&0xEF);
	
  // Disable matching on any sector, use real drive
  POKE(0xD6A1,0x01);
  
  graphics_mode();

  while(1) {
    graphics_clear_double_buffer();  
    print_text(0,0,1,"Reading all sectors...");

    for(t=0;t<85;t++) {
      for(h=0;h<2;h++) {
	for(s=1;s<=10;s++) {

	  snprintf(read_message,40,"Trying T:$%02x, S:$%02x, H:$%02x",t,s,h);
	  print_text(0,1,7,read_message);
	  
	  // Schedule a sector read
	  POKE(0xD081,0x00); // Cancel previous action
	  
	  // Select track, sector, side
	  POKE(0xD084,t);
	  POKE(0xD085,s);
	  POKE(0xD086,h);

	  // Issue read command
	  POKE(0xD081,0x40);
	  
	  x=t*7; y=16+(s-1)*8+(h*80);

	  for (xx=0;xx<6;xx++)
	    for (yy=0;yy<7;yy++)
	      plot_pixel(x+xx,y+yy,14);

	  activate_double_buffer();
	  
	  // Give time for busy flag to assert
	  usleep(1000);
	  
	  // Wait until busy flag clears
	  while(PEEK(0xD082)&0x80) {
	    snprintf(peak_msg,40,"Sector under head T:$%02X S:%02X H:%02x",
		     PEEK(0xD6A3),PEEK(0xD6A4),PEEK(0xD6A5)
	     );
	    print_text(0,24,7,peak_msg);		     
	    continue;
	  }
	  if (PEEK(0xD082)&0x10) {
	    for (xx=0;xx<6;xx++)
	      for (yy=0;yy<7;yy++)
		plot_pixel(x+xx,y+yy,2);
	  } else {
	    for (xx=0;xx<6;xx++)
	      for (yy=0;yy<7;yy++)
		plot_pixel(x+xx,y+yy,5);
	  }
	  activate_double_buffer();
	  
	}
      }
    }
  }
  
}

unsigned char floppy_interval_first=0;
unsigned char floppy_active=0;
unsigned char iec_pass=0,v,y;
unsigned int x;
unsigned char colours[10]={0,2,5,6,0,11,12,15,1,0};

void main(void)
{
  // Fast CPU, M65 IO
  POKE(0,65);
  POKE(0xD02F,0x47);
  POKE(0xD02F,0x53);

  // Floppy motor on
  POKE(0xD080,0x60);  
  
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
    // Internal floppy connector
    if (PEEK(0xD6A9)!=floppy_interval_first) floppy_active=1;
    if (!floppy_active) 
      print_text(0,2,2,"Floppy: FAIL (is a disk inserted?)");
    else
      print_text(0,2,5,"Floppy: PASS                      ");

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
	    print_text(0,3,2,"IEC CLK+DATA: FAIL (CLK+DATA)");
	  }
	} else {
	  print_text(0,3,2,"IEC CLK+DATA: FAIL (CLK)");
	}
      } else {
	print_text(0,3,2,"IEC CLK+DATA: FAIL (DATA)");
      }
    } else {
      char msg[80];
      snprintf(msg,80,"IEC CLK+DATA: FAIL (float $%02x)",v);
      print_text(0,3,2,msg);
    }
    if (iec_pass)
      print_text(0,3,5,"IEC CLK+DATA: PASS                     ");
   

  }
  
  //  gap_histogram();
  // read_all_sectors();
}

