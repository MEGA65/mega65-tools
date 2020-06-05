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

char read_message[41];

unsigned char read_sector(unsigned t, unsigned char s, unsigned char h)
{

  POKE(0xD081,0x00); // Cancel previous action
  
  // Wait until busy flag clears
  while(PEEK(0xD082)&0x80) {
    continue;
  }

  // Schedule a sector read
  
  // Select track, sector, side
  POKE(0xD084,t);
  POKE(0xD085,s);
  POKE(0xD086,0);
  
  // Select correct side of the disk
  if (h) POKE(0xD080,0x68);
  else POKE(0xD080,0x60);
  
  // Issue read command
  POKE(0xD081,0x40);

  // Wait for busy to assert
  while(!PEEK(0xD082)&0x80) continue;
  
  // Wait until busy flag clears
  while(PEEK(0xD082)&0x80) {
    continue;
  }

  return PEEK(0xD082);  
}

unsigned char disk_header[25+1];
unsigned char file_count=0;

void scan_directory(void)
{
  unsigned char t=39,h=1,s=1;
  unsigned short half=0;

  // Get directory header
  read_sector(t,s,h);

  usleep(50000);
  
  // Get track and sector of next dir block
  t=lpeek(0xffd6000L);
  s=lpeek(0xffd6001L);
  if (s&1) half=0x100; else half=0;
  t--;
  h=1;
  if (s>19) { s-=20; h=0;}
  s=s>>1; s++;

  // Get disk name
  lcopy(0xffd6004L,disk_header,25);

  printf("Disk: '%s'\n",disk_header);
  printf("First: T%d, S%d\n",lpeek(0xffd6000L),lpeek(0xffd6001L));

  //  while(1) continue;
  
  while(t&&(t!=0xff)) {
    read_sector(t,s,h);
    usleep(50000);

    lcopy(0xffd6000L+half,0x0400,0x100);
    
    printf("Next: T%d, S%d\n",lpeek(0xffd6000L+half),lpeek(0xffd6001L+half));

    t=lpeek(0xffd6000L+half);
    s=lpeek(0xffd6001L+half);
    if (s&1) half=0x100; else half=0;
    t--;
    h=1;
    if (s>19) { s-=20; h=0;}
    s=s>>1; s++;

    while(!PEEK(0xD610)) continue; POKE(0xD610,0);
    
  }
  

}

void main(void)
{
  // Fast CPU, M65 IO
  POKE(0,65);
  POKE(0xD02F,0x47);
  POKE(0xD02F,0x53);

  // Make sure we are on track 79 or less, so that auto-tune can find tracks for us
  while(!(PEEK(0xD082)&1))
    {
      POKE(0xD081,0x10);
      usleep(6000);
    }

  // Floppy motor on
  POKE(0xD080,0x68);  

  // Enable auto-tracking
  POKE(0xD689,PEEK(0xD689)&0xEF);

  // Map FDC sector buffer, not SD sector buffer
  POKE(0xD689,PEEK(0xD689)&0x7f);
  
  scan_directory();  
  
}

