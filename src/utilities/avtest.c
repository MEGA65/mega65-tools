#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>

unsigned char frames;
unsigned char note;
unsigned char sid_num;
unsigned int sid_addr;
unsigned int notes[5]={5001,5613,4455,2227,3338};

unsigned char i;

// This is a bit of a pain, as we wrote it while fixing the keyboard disco light mode.
// Prior to the fix, exactly only one channel could be set, while after, they can all
// be set individually. So we have to clear them all, and then set exactly the one we
// want, for it to work on both revisions.
void keyboard_set_rgb(unsigned char c)
{
  for(i=0;i<12;i++) {
    if (i!=c) {
      POKE(0xD61E,0x00);
      POKE(0xD61D,0x80+i);
    }
    usleep(1000);
  }
  POKE(0xD61D,0x80+c);
  POKE(0xD61E,0xFF);
}


void test_audio(void)
{
  /*
    Play notes and samples through 4 SIDs and left/right digi
  */

  // Reset all sids
  lfill(0xffd3400,0,0x100);
  
  // Full volume on all SIDs
  POKE(0xD418U,0x0f);
  POKE(0xD438U,0x0f);
  POKE(0xD458U,0x0f);
  POKE(0xD478U,0x0f);

  for(note=0;note<5;note++)
    {
      switch(note) {
      case 0: sid_num=0; keyboard_set_rgb(8); break;
      case 1: sid_num=2; keyboard_set_rgb(2); break;
      case 2: sid_num=1; keyboard_set_rgb(8); break;
      case 3: sid_num=3; keyboard_set_rgb(2); break;
      case 4: sid_num=0; keyboard_set_rgb(8); break;
      }
	
      sid_addr=0xd400+(0x20*sid_num);

      // Play note
      POKE(sid_addr+0,notes[note]&0xff);
      POKE(sid_addr+1,notes[note]>>8);
      POKE(sid_addr+4,0x10);
      POKE(sid_addr+5,0x0c);
      POKE(sid_addr+6,0x00);
      POKE(sid_addr+4,0x11);

      // Wait 1/2 second before next note
      // (==25 frames)
      /* 
	 So the trick here, is that we need to decide if we are doing 4-SID mode,
	 where all SIDs are 1/2 volume (gain can of course be increased to compensate),
	 or whether we allow the primary pair of SIDs to be louder.
	 We have to write to 4-SID registers at least every couple of frames to keep them active
      */
      for(frames=0;frames<35;frames++) {
	// Make sure all 4 SIDs remain active
	// by proding while waiting
	while(PEEK(0xD012U)!=0x80) {
	  POKE(0xD438U,0x0f);
	  POKE(0xD478U,0x0f);
	  continue;
	}
	
	while(PEEK(0xD012U)==0x80) continue;
      }
	 
    }

  // Silence SIDs gradually to avoid pops
  for(frames=15;frames<=0;frames--) {
    while(PEEK(0xD012U)!=0x80) {
      POKE(0xD418U,frames);
      POKE(0xD438U,frames);
      POKE(0xD458U,frames);
      POKE(0xD478U,frames);
      continue;
    }
    
    while(PEEK(0xD012U)==0x80) continue;
  }

  // Reset all sids
  lfill(0xffd3400,0,0x100);
} 

void set_keyboard_rgb(unsigned char rl, unsigned char gl, unsigned char bl,
		      unsigned char rr, unsigned char gr, unsigned char br)
{
  // XXX There is presently a bug, where only exactly one channel can be
  // set.
  
  POKE(0xD61D,0x80); POKE(0xD61E,rl);
  usleep(1000);
  POKE(0xD61D,0x81); POKE(0xD61E,gl);
  usleep(1000);
  POKE(0xD61D,0x82); POKE(0xD61E,bl);
  usleep(1000);

  POKE(0xD61D,0x83); POKE(0xD61E,rl);
  usleep(1000);
  POKE(0xD61D,0x84); POKE(0xD61E,gl);
  usleep(1000);
  POKE(0xD61D,0x85); POKE(0xD61E,bl);
  usleep(1000);

  POKE(0xD61D,0x86); POKE(0xD61E,rr);
  usleep(1000);
  POKE(0xD61D,0x87); POKE(0xD61E,gr);
  usleep(1000);
  POKE(0xD61D,0x88); POKE(0xD61E,br);
  usleep(1000);

  POKE(0xD61D,0x89); POKE(0xD61E,rr);
  usleep(1000);
  POKE(0xD61D,0x8a); POKE(0xD61E,gr);
  usleep(1000);
  POKE(0xD61D,0x8b); POKE(0xD61E,br);
  usleep(1000);

}

unsigned char palP=0;
unsigned char audioP=0;


void main(void)
{
  // Fast CPU, M65 IO
  POKE(0,65);
  POKE(0xD02F,0x47);
  POKE(0xD02F,0x53);

  //  set_keyboard_rgb(255,0,0,0,0,0);  

  if (PEEK(0xD06F)&0x80) palP=0; else palP=1;
  audioP=PEEK(0xD61A)&1;

  printf("%c\n\nP - PAL\nN - NTSC\nS - Stop HDMI Audio\nA - Enable HDMI audio\nM - Play tones\n",0x93);
  
  while(1) {
    // Keyboard LEDs indicate mode
    keyboard_set_rgb(palP*6+audioP);

    printf("%c%s, %s",0x13,palP?"PAL ":"NTSC",audioP?"HDMI Audio on   ":"HDMI Audio muted");
    
    if (PEEK(0xD610)) {
      switch(PEEK(0xD610)) {
      case 0x50: case 0x70:
	// PAL
	POKE(0xD06F,0);
	POKE(0xD011,0x1b);
	palP=1;
	break;
      case 0x4E: case 0x6E:
	// NTSC
	POKE(0xD06F,0x80);
	POKE(0xD011,0x1b);
	palP=0;
	break;
      case 0x41: case 0x61:
	// HDMI audio enable
	POKE(0xD61A,0x01);
	audioP=1;
	break;
      case 0x53: case 0x73:
	// (S)ilent (HDMI audio disable)
	POKE(0xD61A,0x00);
	audioP=0;
	break;
      case 0x04d: case 0x6d:
	test_audio();
	break;
      }
      POKE(0xD610,0);
    }
  }
  
}
