#include <stdio.h>
#include <memory.h>
#include <targets.h>
#include <time.h>

struct m65_tm tm;

void m65_io_enable(void)
{
  // Gate C65 IO enable
  POKE(0xd02fU,0x47);
  POKE(0xd02fU,0x53);
  // Force to full speed
  POKE(0,65);
}

void wait_10ms(void)
{
  // 16 x ~64usec raster lines = ~1ms
  int c=160;
  unsigned char b;
  while(c--) {
    b=PEEK(0xD012U);    
    while (b==PEEK(0xD012U))
      continue;
  }
}

unsigned char free_buffers=0;
unsigned char last_free_buffers=0;

void main(void)
{
  
  m65_io_enable();

  printf("WireKrill 0.0 Network Analyser.\n");
  printf("(C) Paul Gardner-Stephen, 2020.\n");

  // Clear reset on ethernet controller
  POKE(0xD6E0,0x03);
  
  while (1) {
    // Work around bug where ethernet RX block doesn't get automatically
    // cleared.
    free_buffers=(PEEK(0xD6E1)>>1)&3;
    POKE(0x0426,free_buffers);
    if (free_buffers!=last_free_buffers) {
      last_free_buffers=free_buffers;
      printf("%d free RX buffers.\n",free_buffers);
    }
    if (free_buffers<3) {
      // Pop a frame from the buffer list
      POKE(0xD6E1,0x03);
      lcopy(0xFFDE800L,0x0428,0x100);
    }
    POKE(0x0427,PEEK(0x0427)+1);
  }
  
}
  

