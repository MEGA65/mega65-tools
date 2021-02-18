#include <stdio.h>
#include <memory.h>
#include <targets.h>
#include <time.h>

struct m65_tm tm;

void m65_io_enable(void)
{
  // Gate C65 IO enable
  POKE(0xd02fU, 0x47);
  POKE(0xd02fU, 0x53);
  // Force to full speed
  POKE(0, 65);
}

void wait_10ms(void)
{
  // 16 x ~64usec raster lines = ~1ms
  int c = 160;
  unsigned char b;
  while (c--) {
    b = PEEK(0xD012U);
    while (b == PEEK(0xD012U))
      continue;
  }
}

unsigned char free_buffers = 0;
unsigned char last_free_buffers = 0;

unsigned char frame_buffer[1024];
char msg[160 + 1];

unsigned char rx_ref[79] = { 0x80, 0xAB, 0x54, 0x45, 0x53, 0x54, 0x20, 0x4D, 0x45, 0x53, 0x53, 0x41, 0x47, 0x45, 0x20, 0x31,
  0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x20, 0x54, 0x48, 0x45, 0x20, 0x51, 0x55, 0x49, 0x43, 0x4B, 0x20,
  0x42, 0x52, 0x4F, 0x57, 0x4E, 0x20, 0x46, 0x4F, 0x58, 0x20, 0x4A, 0x55, 0x4D, 0x50, 0x45, 0x44, 0x20, 0x4F, 0x56, 0x45,
  0x52, 0x20, 0x54, 0x48, 0x45, 0x20, 0x4C, 0x41, 0x5A, 0x59, 0x20, 0x4F, 0x4C, 0x44, 0x20, 0x44, 0x4F, 0x47, 0x39, 0x93,
  0x13, 0xB9, 0xEA };

unsigned char i;

void main(void)
{

  m65_io_enable();

  printf("%cWireKrill 0.0 Network Analyser.\n", 0x93);
  printf("(C) Paul Gardner-Stephen, 2020.\n");

  // Clear reset on ethernet controller
  POKE(0xD6E0, 0x01);
  POKE(0xD6E0, 0x03);

  // No promiscuous mode, ignore corrupt packets, accept broadcast and multicast
  // default RX and TX phase adjust.
  POKE(0xD6E5, 0x30);

  while (1) {
    // Check for new packets
    if (PEEK(0xD6E1) & 0x20) // Packet received
    {
      // Pop a frame from the buffer list
      POKE(0xD6E1, 0x01);
      POKE(0xD6E1, 0x03);
      lcopy(0xFFDE800L, (long)frame_buffer, 0x0800);
      snprintf(msg, 160, "%02x:%02x:%02x:%02x:%02x:%02x > %02x:%02x:%02x:%02x:%02x:%02x\n  len=%d, rxerr=%c",
          frame_buffer[8], frame_buffer[9], frame_buffer[10], frame_buffer[11], frame_buffer[12], frame_buffer[13],
          frame_buffer[2], frame_buffer[3], frame_buffer[4], frame_buffer[5], frame_buffer[6], frame_buffer[7],
          frame_buffer[0] + ((frame_buffer[1] & 0xf) << 8), (frame_buffer[1] & 0x80) ? 'Y' : 'N');
      printf("%s\n", msg);

      // Check for RX error checking packet
      for (i = 0; i < 79; i++)
        if (frame_buffer[0x0e + i] != rx_ref[i])
          break;
      if (i > 2 && i < 79) {
        printf("%s\n", msg);
        printf("Corrupt RX reference packet:\n");
        for (i = 0; i < 79; i++) {
          // Red for errors, white for correct
          if (frame_buffer[0x0e + i] != rx_ref[i])
            printf("%c", 28);
          else
            printf("%c", 5);
          printf("%02x", frame_buffer[0x0e + i]);
          if ((i & 0x0f) == 0x0f)
            printf("\n");
        }
        printf("\n");
      }
      else if (i == 79) {
        printf("%s\n", msg);
        if (frame_buffer[0] != 0x5a)
          printf("Length field of RX reference packet is wrong, but data is good.\n");
      }
    }
  }
}
