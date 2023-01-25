/*
  Issue #340 - Allow masking of each pixel row to allow for more flexible RRB Y scrolling
  Test code by George Kirkham (Geehaf) 21/6/2022
*/
#define ISSUE_NUM 340
#define ISSUE_NAME "allow masking of each pixel row to allow for more flexible rrb y scrolling"

#define COLOR 0xD800
#define SCREEN 0x0400

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <memory.h>
#include <tests.h>

unsigned char sub = 0;

unsigned short i, j;

int xp, yp, errcount, testresult;

unsigned char state[12];
unsigned char red, green, blue;
unsigned char frame_num;

void save_state(void)
{
  // save state
  state[0] = PEEK(0xD031);
  state[1] = PEEK(0xD054);
}

void restore_state(void)
{
  // restore state
  POKE(0xD031, state[0]);
  POKE(0xD054, state[1]);
}

void read_pixel(short x, short y, unsigned char *red, unsigned char *green, unsigned char *blue)
{
  // Select (128,128) as pixel to read back
  POKE(0xD07D, x);
  POKE(0xD07E, y);
  POKE(0xD07F, (x >> 8) + ((y >> 8) << 4));

  // Wait at least one whole frame
  frame_num = PEEK(0xD7FA);
  while (PEEK(0xD7FA) == frame_num)
    continue;
  frame_num = PEEK(0xD7FA);
  while (PEEK(0xD7FA) == frame_num)
    continue;

  POKE(0xD07C, 0x52);
  *red = PEEK(0xD07D);
  POKE(0xD07C, 0x92);
  *green = PEEK(0xD07D);
  POKE(0xD07C, 0xD2);
  *blue = PEEK(0xD07D);
}

unsigned char keybuffer(unsigned char wait)
{
  unsigned char key = 0;
  // clear keyboard buffer
  while (PEEK(0xD610))
    POKE(0xD610, 0);

  if (wait) {
    while ((key = PEEK(0xD610)) == 0)
      ;
    POKE(0xD610, 0);
  }

  return key;
}

void testpixels(int test_mode)
{

  unsigned char is_black=0;
  unsigned char bad=0;
  
  // Now we scan screen pixel area for any black coloured pixels
  // If we find the colour black then the test has failed as the pixels should be masked
  // Pixel resolution is double character resolution
  errcount = 0;
  for (yp = 0; yp < 16; yp++) {
    for (xp = 1; xp < 16; xp++) {
      // (86,104) = x,y for top left of screen
      read_pixel(xp + 86 + 2, yp + 104, &red, &green, &blue);
      // If any of the pixels we are checking are black (RGB=000000) then we have a fail, i.e
      // the character pixel row has not been masked out      
      is_black=0;
      if ((red == 0x00 && green == 0x00 && blue == 0x00)) {
	is_black=1;
      }
      bad=0;
      if (test_mode==0) {
	if (is_black&&(yp>7)) bad=1;
	if ((yp<8)&&(!is_black)) bad=1;
      }
      if (test_mode==1) {
	if (is_black&&yp<8) bad=1;
	if (yp>7&&(!is_black)) bad=1;
      }
      if (test_mode==2) {
	if (is_black&&(yp&2)) bad=1;
	if ((!is_black)&&(!(yp&2))) bad=1;
      }

      POKE(SCREEN + 80 + 4 + xp*2 + yp*40,0xa0);
      if (is_black) {
	if (bad) {
	  POKE(COLOR + 80 + 4 + xp*2 + yp*40 + 1,0x2);
	}
	else {
	  POKE(COLOR + 80 + 4 + xp*2 + yp*40 + 1,0x5);
	}
      } else {
	if (bad) {
	  POKE(COLOR + 80 + 4 + xp*2 + yp*40 + 1,0xa);
	} else {
	  POKE(COLOR + 80 + 4 + xp*2 + yp*40 + 1,0xd);
	}
      }

      if (bad) errcount++;
    }
  }
}

unsigned int fails[3]={0,0,0};

void main(void)
{

  asm("sei");

  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  // keybuffer(1);

  save_state();

  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // Disable hot registers
  // POKE(0xD05D, PEEK(0xD05D) & 0x7f);

  // No ugly screen during test run
  for(i=0;i<0x0400;i+=2) {
    POKE(SCREEN + i+0,0x20);
    POKE(SCREEN + i+1,0x00);
    POKE(COLOR + i+0,0x00);
    POKE(COLOR + i+1,0x00);
  }  
    
  // bit0: 0CHR16 enable 16-bit character numbers (two screen bytes per character)
  POKE(0xD054, PEEK(0xD054) | 1);

  // Only display 20 chars, since in 16-bit text mode that uses all 40 bytes per row  
  POKE(0xD05E,20);

  // bit5: ATTR enable extended attributes and 8 bit colour entries
  // bit7: H640 disable C64 640 horizontal pixels / 80 column mode
  POKE(0xD031, 0x20);

  POKE(COLOR + 0, 0x18); // Enable GOTOX (bit 4) and rowmask (bit 3)
  POKE(COLOR + 1, 0x0f); // First Rowmask: 00001111, i.e. bottom 4
                         // 4 character pixel lines should be masked (not displayed)
  POKE(COLOR + 2, 0x00);
  POKE(COLOR + 3, 0x00);

  POKE(SCREEN + 0, 0x00); // First WORD GOTOX position
  POKE(SCREEN + 1, 0x00);
  POKE(SCREEN + 2, 32 + 128); // Output the character to mask (Inverted space)
  // POKE(SCREEN+2,98); // ** ONLY USED FOR TESTING THE TEST! ** Output the character to mask (Half Inverted space to
  // simulate passing the test)
  POKE(SCREEN + 3, 0x00);

  testresult = 0;

  // run our 1st test with rowmask of $0f
  testpixels(0);
  fails[0]=errcount;
  if (errcount) {
    testresult++;
  }

  // POKE(SCREEN+2,98+128); // ** ONLY USED FOR TESTING THE TEST! ** Output the character to mask (Half Inverted space to
  // simulate passing the test)

  // run our 2nd test with rowmask of $0f
  POKE(COLOR + 1, 0xf0); // Second Rowmask: 11110000, i.e. top
                         // 4 character pixel lines should be masked (not displayed)
  testpixels(1); fails[1]=errcount;
  if (errcount != 0) {
    testresult++;
  }

  // Now put an FCM char there.
  // data at $40040 = char number $1001
  POKE(0xD054,PEEK(0xD054)|0x04); // allow high-char FCM
  lfill(0x40040,1,64);
  POKE(SCREEN + 2, 0x01); // Output the character to mask (FCM char at $40040)
  POKE(SCREEN + 3, 0x10);
  POKE(COLOR + 1, 0x55); // Third Rowmask: 01010101, i.e., alternate rows should
                         // be masked, i.e., not displayed.  
  // Make white black, and fill FCM char with it
  POKE(0xD101,0x00);POKE(0xD201,0x00);POKE(0xD301,0x00);
  testpixels(2); fails[2]=errcount;
  if (errcount != 0) {
    testresult++;
  }
  
  // Make white white again
  POKE(0xD101,0xff);POKE(0xD201,0xff);POKE(0xD301,0xff);
  
  // keybuffer(1);

  restore_state();

  printf("%c", 147); // clear screen;
  printf(".\nIssue #%d - %s\n\n", ISSUE_NUM, ISSUE_NAME);


  if (fails[0]) printf("FAIL - Draw upper 4 rows only (%d)\n",fails[0]);
  else printf("PASS - Draw upper 4 rows only\n");
  if (fails[1]) printf("FAIL - Draw lower 4 rows only (%d)\n",fails[1]);
  else printf("PASS - Draw lower 4 rows only\n");
  if (fails[2]) printf("FAIL - Draw FCM alternate rows only (%d)\n", fails[2]);
  else printf("PASS - Draw FCM alternate rows only\n");
  
  // if testresult > 1, i.e. both rowmask tests failed then it's a failure overall
  if (testresult) {
    printf("%02d rowmask tests failed :(\n", testresult);
    unit_test_report(ISSUE_NUM, sub++, TEST_FAIL);
  }
  else {
    printf("Rowmask tests passed - success :)");
    unit_test_report(ISSUE_NUM, sub++, TEST_PASS);
  }

  // Report completion of tests
  unit_test_report(ISSUE_NUM, sub++, TEST_DONEALL);
}
