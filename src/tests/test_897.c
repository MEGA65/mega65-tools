/*
Issue #897 - 
*/
#define ISSUE_NUM 897
#define ISSUE_NAME "hyppo system resource access"

#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <tests.h>
#include <hal.h>

extern int test_resource_read(void);

char msg[80];
int i = 0;
unsigned char carry = 0;

unsigned long resource_sector = 0;
unsigned long return_value = 0;

char message[81];

unsigned char sector_buffer[512];
unsigned char magic_string[]={
  0x4d,0x45,0x47,0x41,'6','5',                   // MEGA65
  0x53,0x48,0x41,0x52,0x45,0x44,                 // SHARED
  0x52,0x45,0x53,0x4f,0x55,0x52,0x43,0x45,0x53,  // RESOURCES
  0x00};


void main(void)
{
  printf("%c%c", 147, 5); // clear screen; color white
  printf("issue #%d - %s\n", ISSUE_NUM, ISSUE_NAME);

  // Fast CPU, M65 IO
  mega65_io_enable();

  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  // Try reading sector 0 of resource area
  resource_sector = 0;
  *(unsigned long *)0x7f0 = resource_sector;
  
  if (test_resource_read() == 0) {
    unit_test_ok("");
  }
  else {
    unit_test_fail("hyppo call helper failed");
  }

  return_value = *(unsigned long *)0x7f4;

  carry = PEEK(0x7f8)&1;

  if (!carry) {
    switch(PEEK(0x7f4)) {
    case 0xff: unit_test_fail("hyppo returned 'trap not implemented'"); break;
    case 0x11: // dos_errorcode_illegal_value
      unit_test_fail("reading resource sector 0 failed -- no resource area in system partition?");
      break;
    default: {
      snprintf(message,80,"hyppo resource read failed with unknown error $%02x",PEEK(0x7f4));
      unit_test_fail(message);
    }
    }
    unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
    return;
  }

  unit_test_ok("hyppo resource read sector 0 test passed");

  resource_sector = 0xffffffffUL;
  for(i=31;i>=0;i--) {
    *(unsigned long *)0x7f0 = resource_sector;

    if (test_resource_read() != 0) unit_test_fail("hyppo trap failed");
    carry = PEEK(0x7f8)&1;

    if (!carry) {
      resource_sector -= (1UL <<i);
    }

  }

  if (resource_sector==0xffffffffUL || (!resource_sector)) {
    unit_test_fail("failed to determine size of shared resource area");
  } else {
    snprintf(message,80,"determined shared resource area is $%08lx sectors",
	     resource_sector + 1);
    unit_test_ok(message);
  }
  
  printf("Shared resource area = $%08lx sectors.\n",resource_sector+1);


  // Reset SD card and make sure it's not busy after, so that we can write to the sector buffer
  // so that we can be sure that the sector buffer contents change after a read request.
  POKE(0xD680,0x00);
  POKE(0xD680,0x01);
  // Wait for SD card busy to clear
  for(i=0;i<32767;i++) {
    if ((PEEK(0xD680)&3)==0x00) break;
    usleep(100);
  }
  if (i==32767) unit_test_fail("sd card stayed busy on reset");
  else unit_test_ok("sd card busy cleared on reset");

  
  // Now check that requests cause $D681-4 get overwritten
  // And the SD sector buffer changes contents
  POKE(0xD681,0x00);
  POKE(0xD682,0x00);
  POKE(0xD683,0x00);
  POKE(0xD684,0x00);
  lpoke(0xffd6000UL,0x42);
  lpoke(0xffd6001UL,0x23);

  // Read first sector of the shared resource area
  resource_sector = 0;
  *(unsigned long *)0x7f0 = resource_sector;
  
  if (test_resource_read() != 0) unit_test_fail("hyppo trap failed");
  carry=0;
  if (PEEK(0xD681)!=0x00) carry=1;
  if (PEEK(0xD682)!=0x00) carry=1;
  if (PEEK(0xD683)!=0x00) carry=1;
  if (PEEK(0xD684)!=0x00) carry=1;
  if (!carry) unit_test_fail("sd card registers not written to");
  else unit_test_ok("sd card registers written to");

  // Wait for SD card busy to clear
  for(i=0;i<32767;i++) {
    if ((PEEK(0xD680)&3)==0x00) break;
    usleep(100);
  }
  if (i==32767) unit_test_fail("sd card stayed busy");
  else unit_test_ok("sd card busy cleared");
  
  carry=0;
  if (lpeek(0xffd6000UL)!=0x42) carry=1;
  if (lpeek(0xffd6001UL)!=0x23) carry=1;
  if (!carry) unit_test_fail("sd card buffer contents did not change");
  else unit_test_ok("sd card buffer contents change");

  // Now check if the read sector contains the magic string for the shared resource section.
  {
    sector_buffer[0]=0;
    lcopy(0xffd6e00L,&sector_buffer,512);
    for(i=0;magic_string[i];i++) {
      if (sector_buffer[i]!=magic_string[i]) break;      
    }
    if (magic_string[i]) {
      unit_test_fail("read magic string from shared resource area");
      printf("i=%d, 0x%x vs 0x%x\n",i,sector_buffer[i],magic_string[i]);
    } else {
      unit_test_ok("read magic string from shared resource area");
    }
  }


  
  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
