/*
  Simple SID tester.

  It plays a variety of single tones using different waveforms for 
  recording.

  On MEGA65, it then uses the new SID-record DMA mode that I created
  for testing this to save direct digital captures of the tones.

*/


#include <stdio.h>
#include <conio.h>
#include <hal.h>
#include <memory.h>

unsigned char is_mega65=0;
unsigned short div;
unsigned char wf,i,c;
unsigned char sid_mode=0;
unsigned char with_gate=1;
unsigned char oct=1;
unsigned char note=0;
unsigned long divl;

unsigned char dma_list[16]=
  {
   0x0a, // F018A DMA format
   0x10, // SID recording mode
   0x90,0x02, // 2x64KB length
   0x00, // End of options
   0x03, // FILL + end of chain
   0xff, 0xff, // Len=$FFFF
   0x00,0x00,0x00, // SRC (ignored for SID record file)
   0x00,0x00,0x04, // DST = bank 4
   0x00,0x00 // Modulo (ignored)
   
  };

unsigned char wav_header[0x50]=
  {
   0x52,0x49,0x46,0x46,0xf8,0xff,0x00,0x00,0x57,0x41,0x56,0x45,0x66,0x6d,0x74,0x20,  //   RIFFH(E@WAVEfmt 
   0x10,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x25,0xac,0x00,0x00,0x44,0xac,0x00,0x00, // 44069Hz = $AC25 
   0x01,0x00,0x08,0x00,0x66,0x61,0x63,0x74,0x04,0x00,0x00,0x00,0x00,0x4a,0x01,0x00, //    D@ @factD@@@@JA@
   0x50,0x45,0x41,0x4b,0x10,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0xda,0xa1,0xff,0x5c, //    PEAKP@@@A@@@Za~
   0xce,0x4d,0x00,0x3f,0xfa,0xae,0x00,0x00,0x64,0x61,0x74,0x61,0xb0,0xff,0x00,0x00   
  };

unsigned long note_freqs[12]={
			       35641L,37760L,40005L,42384L,44904L,47574L,50403L,53401L,56576L,59940L,63504L,67280L
};

unsigned char *note_names[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

unsigned short j;

void main(void)
{
  /* Detect MEGA65 vs stock C64
     There are lots of ways we could do it. Nicest would probably be to ask hypervisor
     for version. But as we have DMA operation functions, we just see if one of those works :)
  */
  if (PEEK(0xFFFD)==0xE4) {
    POKE(0x0420,1);
    POKE(0xD02f,0x47);
    POKE(0xD02f,0x53);
    lfill(0x0420,0,1);
    if (PEEK(0x0420)==0) { is_mega65=1; POKE(0,65); }
  }
  
  printf("%c%cSimple SID Tester.\n",0x05,0x93);
  if (is_mega65) printf("Running on a MEGA65.\n"); else printf("Running on a C64\n");

  /*
    0x10 = triangle
    0x20 = saw-tooth.
    0x40 = test?
    0x80 = noise
   */
  wf=0x10;

  while(1) {
    printf("\n\n\n\n");
    printf("SID mode is %d\n",sid_mode?8580:6581);
    printf("Waveform is $%02X\n",wf);
    printf("Note is %s%d\n",note_names[note],oct);
    divl=note_freqs[note]>>(7-oct);
    div=divl;
    if (divl>65535L) { printf("WARNING: Note is too high. Clipping to max.\n"); div=65535L; }
    printf("Divisor is %ld\n",divl);
    if (with_gate) printf("Gate enabled\n");
    printf("\n");
    printf("M = Toggle SID mode\n");
    printf("W = Next waveform\n");
    printf("O = Next octave\n");
    printf("N = Next note\n");
    printf("G = Toggle gate\n");
    printf("T = Test (play tone; record if MEGA65)\n");

    // Read from key buffer
    while(!PEEK(198)) continue;
    c=PEEK(631); POKE(198,0);
    
    switch(c) {
    case 0x54: case 0x74:
      for(i=0;i<32;i++) POKE(0xD400+i,0);

      if (is_mega65) POKE(0xD63C,sid_mode?0xff:0);
      else
	{
	printf("Get Ready");
	printf("...3"); for(j=0;j<10000;j++);
	printf("...2"); for(j=0;j<10000;j++);
	printf("...1"); for(j=0;j<10000;j++);
	printf("...go!\n");
      }
      POKE(0xD418,0x0f);
      
      // ADSR
      // Attack duration = 38mS (0x4)
      // Decay duration = 72mS (0x3)
      // Sustain level = half (0x8)
      // Release duration = 300ms (0x8)
      POKE(0xd405,0x43);
      POKE(0xd406,0x88);
      
      // Note frequency
      POKE(0xD400,div);
      POKE(0xD401,div>>8);

      // Pulse width = 100% for testing ADSR envelope
      POKE(0xD410,0xff);
      POKE(0xD411,0xff);
      
      // Start note playing  
      POKE(0xD404,wf+1);
      if (with_gate) POKE(0xD404,wf+0);
      
      if (is_mega65) {
	// DMA audio recording
	POKE(0xD020,0x02);
	POKE(0xD702,0x00);
	POKE(0xD701,((unsigned short)&dma_list)>>8);
	POKE(0xD705,(unsigned char)&dma_list);
	POKE(0xD020,0x0e);

	// Apply WAV file header
	lcopy((unsigned long)wav_header,0x40000,0x50);
	
	// Record info about the test
	lpoke(0x40050,sid_mode);
	lpoke(0x40051,wf);
	lpoke(0x40052,with_gate);
	lpoke(0x40053,oct);
	lpoke(0x40054,note);

	// Then shut it up after on MEGA65
	POKE(0xD418,0);
      }
      
      break;
    case 0x47: case 0x67:
      with_gate^=1;
      break;
    case 0x4d: case 0x6d:
      sid_mode^=1;
      break;
    case 0x4e: case 0x6e:
      note++; if (note>11) note=0;
      break;
    case 0x4f: case 0x6f:
      oct++; if (oct>7) oct=0;
      break;
    case 0x57: case 077:
      wf=wf+0x10;
      break;
    }
  }
}
