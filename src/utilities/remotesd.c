/*
  Remote SD card access tool for mega65_ftp to more quickly
  send and receive files.

  It implements a simple protocol with pre-emptive sending 
  of read data in raw mode at 4mbit = 40KB/sec.  Can do this
  while writing jobs to the SD card etc to hide latency.

*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>
#include <debug.h>
#include <random.h>

// Write a char to the serial monitor interface
#define SERIAL_WRITE(the_char) { __asm__ ("LDA %v",the_char); __asm__ ("STA $D643"); __asm__ ("NOP"); }

uint8_t c,j;
uint16_t i;
void serial_write_string(uint8_t *m,uint16_t len)
{
  for(i=0;i<len;i++) {
    c=*m; m++;
    SERIAL_WRITE(c);
  }
}

uint16_t last_advertise_time,current_time;
uint8_t job_count=0;
uint16_t job_addr;

char msg[80+1];

uint32_t buffer_address,sector_number,transfer_size;

void main(void)
{
  asm ( "sei" );

  // Fast CPU, M65 IO
  POKE(0,65);
  POKE(0xD02F,0x47);
  POKE(0xD02F,0x53);

  // Cursor off
  POKE(204,0x80);
  
  printf("%cMEGA65 File Transfer helper.\n",0x93);

  last_advertise_time=0;

  // Clear communications area
  lfill(0xc000,0x00,0x1000);
  lfill(0xc001,0x42,0x1000);
  
  while(1) {
    current_time=*(uint16_t *)0xDC08;
    if (current_time!=last_advertise_time) {
      // Announce our protocol version
      serial_write_string("\nmega65ft1.0\n\r",14);
      last_advertise_time=current_time;
    }

    if (PEEK(0xC000)) {
      // Perform one or more jobs
      job_count=PEEK(0xC000);
      printf("Received list of %d jobs.\n",job_count);
      job_addr=0xc001;
      for(j=0;j<job_count;j++) {
	if (job_addr>0xcfff) break;
	switch(PEEK(job_addr)) {
	case 0x00: job_addr++; break;
	case 0xFF:
	  // Terminate
	  __asm__("jmp 58552");
	  break;
	case 0x01:
	  // Read sector
	  job_addr++;
	  buffer_address=*(uint32_t *)job_addr;
	  job_addr+=4;
	  sector_number=*(uint32_t *)job_addr;
	  job_addr+=4;

#if 1
	  printf("$%04x : Read sector $%08lx into mem $%07lx\n",*(uint16_t *)0xDC08,
		 sector_number,buffer_address);
#endif	  
	  // Do read
	  *(uint32_t *)0xD681 = sector_number;	  
	  POKE(0xD680,0x02);

	  // Wait for SD to go busy
	  while(!(PEEK(0xD680)&0x03)) continue;
	  
	  // Wait for SD to read and fiddle border colour to show we are alive
	  while(PEEK(0xD680)&0x03) POKE(0xD020,PEEK(0xD020)+1);

	  lcopy(0xffd6e00,buffer_address,0x200);

	  snprintf(msg,80,"ftjobdone:%04x:\n\r",job_addr-9);
	  serial_write_string(msg,strlen(msg));

	  printf("$%04x : Read sector done\n",*(uint16_t *)0xDC08);

	  
	  break;
	case 0x11:
	  // Send block of memory
	  job_addr++;
	  buffer_address=*(uint32_t *)job_addr;
	  job_addr+=4;
	  transfer_size=*(uint32_t *)job_addr;
	  job_addr+=4;

#if 1
	  printf("$%04x : Send mem $%07lx to $%07lx: %02x %02x ...\n",*(uint16_t *)0xDC08,
		 buffer_address,buffer_address+transfer_size-1,
		 lpeek(buffer_address),lpeek(buffer_address+1));
#endif
	  
	  snprintf(msg,80,"ftjobdata:%04x:%08lx:",job_addr-9,transfer_size);
	  serial_write_string(msg,strlen(msg));

	  j=2;
	  while(transfer_size--) {
	    c=lpeek(buffer_address++);
	    if (j) {
	      printf("%02x\n",c);
	      j--;
	    }
	    SERIAL_WRITE(c);
	  }

	  snprintf(msg,80,"ftjobdone:%04x:\n\r",job_addr-9);
	  serial_write_string(msg,strlen(msg));

	  printf("$%04x : Send mem done\n",*(uint16_t *)0xDC08);
	  
	  break;
	  
	default:
	  job_addr=0xd000;
	  break;
	}
      }

      // Indicate when we think we are all done      
      POKE(0xC000,0);      
      snprintf(msg,80,"ftbatchdone\n");
      serial_write_string(msg,strlen(msg));

      printf("$%04x : Sending batch done\n",*(uint16_t *)0xDC08);
      
    }
    
  }
  
}
