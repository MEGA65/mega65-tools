#include <stdio.h>
#ifndef NATIVE_TEST
#include <memory.h>
#include <targets.h>
#include <time.h>

struct m65_tm tm;
#else
#include <strings.h>
#include <string.h>
#include <pcap/pcap.h>
#include <stdlib.h>

pcap_t *p = NULL;

#define POKE(X, Y)
// Make PEEK(0xD6E1) always indicate that a packet is always ready to be received
#define PEEK(X) 0x20

void lcopy(long src, long dst, int count)
{
  if (src == 0xFFDE800L) {
    // Read next packet from pcap file
    struct pcap_pkthdr h;

    char *packet = pcap_next(p, &h);
    if (!packet)
      exit(0);
    ((char *)dst)[0] = h.len;
    ((char *)dst)[1] = h.len >> 8;
    bcopy(packet, (void *)dst + 2, count - 1);
  }
  else {
    bcopy((void *)src, (void *)dst, count);
  }
}

void lpoke(long addr, int val)
{
  // Ignore POKEs to upper memory
}

void lfill(long addr, int val, int count)
{
  // Ignore POKEs to upper memory
}

int lpeek(long addr)
{
  return 0;
}
#endif

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

void wait_1ms(void)
{
  // 16 x ~64usec raster lines = ~1ms
  int c = 16;
  unsigned char b;
  while (c--) {
    b = PEEK(0xD012U);
    while (b == PEEK(0xD012U))
      continue;
  }
}

unsigned short mdio_read_register(unsigned char addr, unsigned char reg)
{
  unsigned short result = 0, r1 = 1, r2 = 2, r3 = 3;

  // Require several consecutive identical reads to debounce
  while (result != r1 || r1 != r2 || r2 != r3) {
    r3 = r2;
    r2 = r1;
    r1 = result;

    // Use new MIIM interface
    POKE(0xD6e6L, (reg & 0x1f) + ((addr & 7) << 5));
    // Reading takes a little while...
    // ~100 bits at ~500KHz = ~200usec
    wait_1ms();
    result = PEEK(0xD6E7L);
    result |= PEEK(0xD6E8L) << 8;
  }

  return result;
}

unsigned short mdio_write_register(unsigned char addr, unsigned char reg, unsigned short val)
{
    POKE(0xD6e6L, (reg & 0x1f) + ((addr & 7) << 5));
    POKE(0xD6E8L, val>>8);
    POKE(0xD6E7L, val>>0);
    // Allow write to finish before scheduling the read
    wait_1ms();
    return mdio_read_register(addr,reg);
}

#define TEST(reg, v, bit, desc)                                                                                             \
  {                                                                                                                         \
    if (v & (1 << bit)) {                                                                                                   \
      snprintf(msg, 160, "   MDIO $%x.%x : %s", reg, bit, desc);                                                            \
      println_text80(8, msg);                                                                                               \
    }                                                                                                                       \
  }

unsigned char free_buffers = 0;
unsigned char last_free_buffers = 0;

unsigned char frame_buffer[1024];
char msg[160 + 1];

unsigned short i;

unsigned char loopback_mode=0;
unsigned char good_loop_frames=0;
unsigned int total_loop_frames=0;
unsigned int bad_loop_frames=0;
unsigned int missed_loop_frames=0;
unsigned char skip_frame=0;
unsigned char saw_loop_frame=0;
unsigned char send_loop_frame=0;
unsigned char error_map[1024];
unsigned char icmp_only=0;

unsigned char loop_data[256];

void graphics_clear_screen(void)
{
  lfill(0x40000L, 0, 32768L);
  lfill(0x48000L, 0, 32768L);
}

void graphics_clear_double_buffer(void)
{
  lfill(0x50000L, 0, 32768L);
  lfill(0x58000L, 0, 32768L);
}

void h640_text_mode(void)
{
  // lower case
  POKE(0xD018, 0x16);

  // Normal text mode
  POKE(0xD054, 0x00);
  // H640, V400, fast CPU, extended attributes
  POKE(0xD031, 0xE8);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016, 0xC9);
  // 80 chars per line logical screen layout
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  // Draw 80 chars per row
  POKE(0xD05E, 80);
  // Put 4KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  // 50 lines of text
  POKE(0xD07B, 50);

  lfill(0xc000, 0x20, 4000);
  // Clear colour RAM, while setting all chars to 4-bits per pixel
  lfill(0xff80000L, 0x0E, 4000);
}

unsigned short pixel_addr;
unsigned char pixel_temp;
void plot_pixel(unsigned short x, unsigned char y, unsigned char colour)
{
  pixel_addr = ((x & 0xf) >> 1) + 64 * 25 * (x >> 4);
  pixel_addr += y << 3;
  pixel_temp = lpeek(0x50000L + pixel_addr);
  if (x & 1) {
    pixel_temp &= 0x0f;
    pixel_temp |= colour << 4;
  }
  else {
    pixel_temp &= 0xf0;
    pixel_temp |= colour & 0xf;
  }
  lpoke(0x50000L + pixel_addr, pixel_temp);
}

unsigned char char_code;
void print_text(unsigned char x, unsigned char y, unsigned char colour, char *msg)
{
  pixel_addr = 0xC000 + x * 2 + y * 80;
  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    POKE(pixel_addr + 0, char_code);
    POKE(pixel_addr + 1, 0);
    lpoke(0xff80000 - 0xc000 + pixel_addr + 0, 0x00);
    lpoke(0xff80000 - 0xc000 + pixel_addr + 1, colour);
    msg++;
    pixel_addr += 2;
  }
}

void print_text80(unsigned char x, unsigned char y, unsigned char colour, char *msg)
{
  pixel_addr = 0xC000 + x + y * 80;
  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    else if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    else if (*msg >= 0x60 && *msg <= 0x7A)
      char_code = *msg - 0x20;
    POKE(pixel_addr + 0, char_code);
    lpoke(0xff80000L - 0xc000 + pixel_addr, colour);
    msg++;
    pixel_addr += 1;
  }
}

// this should print the decimal version of an arbitrary number of bytes
// probably already something in c stdlib or this repo but i couldnt find anything
int to_decimal(unsigned char *src, int bytes)
{
  int out = 0;
  int i;
  int x;
  for (i = 0; i < bytes; i++) {
    x = src[bytes - i - 1];
    out |= (x << (8 * i));
  }
  return out;
}

unsigned char text_row = 0;

#ifdef NATIVE_TEST
void clear_text80(void)
{
}

void println_text80(unsigned char colour, char *msg)
{
  fprintf(stdout, "%s\n", msg);
}

unsigned char safe_char(unsigned char in)
{
  if (in >= ' ' && in < 0x7d)
    return in;
  return 0x2e;
}

#else
void clear_text80(void)
{
  lfill(0xc000, 4000, 0x20);
  lfill(0xff80000L, 4000, 1);
  text_row = 0;
}

void println_text80(unsigned char colour, char *msg)
{
  if (text_row == 49) {
    lcopy(0xc000 + 80, 0xc000, 4000 - 80);
    lcopy(0xff80000 + 80, 0xff80000, 4000 - 80);
    lfill(0xc000 + 4000 - 80, 0x20, 80);
    lfill(0xff80000 + 4000 - 80, 0x01, 80);
  }
  print_text80(0, text_row, colour, msg);
  if (text_row < 49)
    text_row++;
}

unsigned char safe_char(unsigned char in)
{
  if (!in)
    return 0x2e;

  return in;
}

#endif

unsigned short mdio1 = 0, last_mdio1 = 0, frame_count = 0;
unsigned char phy, last_frame_num = 0, show_hex = 0, last_d6ef = 0;

void errata_ksz8081rnd(void)
{
  /* 
     None of the errata work-arounds are required at this time.
   */
  
}

void clear_rx_queue(void)
{
  // Clear queued packets
  for(i=0;i<255;i++) {
    if (PEEK(0xD6E1) & 0x20) // Packet received
      {
	// Pop a frame from the buffer list
	POKE(0xD6E1, 0x01);
	POKE(0xD6E1, 0x03);
	POKE(0xD6E1, 0x01);
      }
  }
}


void eth_reset(void)
{
  // Datasheet requires at least 500usec held low, then at least 100usec after release,
  // before anything else is done to it.
  POKE(0xd6e0, 0);
  for(i=0;i<20;i++) wait_10ms();
  POKE(0xd6e0, 3);
  for(i=0;i<20;i++) wait_10ms();
  POKE(0xd6e1, 3);
  POKE(0xd6e1, 0);

  clear_rx_queue();
  
}

void mdio_dump(void)
{
  unsigned char i;
  for(i=0;i<32;i+=8) {
    snprintf(msg, 160, "  MDIO: %02x: %04x %04x %04x %04x %04x %04x %04x %04x",
	     i,
	     mdio_read_register(phy,i+0),
	     mdio_read_register(phy,i+1),
	     mdio_read_register(phy,i+2),
	     mdio_read_register(phy,i+3),
	     mdio_read_register(phy,i+4),
	     mdio_read_register(phy,i+5),
	     mdio_read_register(phy,i+6),
	     mdio_read_register(phy,i+7)
	     );
    println_text80(7, msg);
  }

}

#ifdef NATIVE_TEST
int main(int argc, char **argv)
{
  if (argc != 2) {
    fprintf(stderr, "usage: wirekrill <file.pcap>\n");
    exit(-1);
  }

  char errbuf[8192];
  p = pcap_open_offline(argv[1], errbuf);
  if (!p) {
    fprintf(stderr, "ERROR: Failed to open PCAP capture file '%s'\nPCAP said: %s\n", argv[1], errbuf);
    exit(-1);
  }
  
#else
void main(void)
{
#endif
  unsigned short len;
  // since the start of the header varies depending on whether the IPv4 options field is present
  unsigned short start_of_protocol_header;
  // looping
  int i;

  POKE(0x0286, 0x01);

  m65_io_enable();

  h640_text_mode();

  clear_text80();

  println_text80(1, "WireKrill 0.0.1 Network Analyser.");
  println_text80(1, "(C) Paul Gardner-Stephen, 2020-2022.");

  
  // No promiscuous mode, ignore corrupt packets, accept broadcast and multicast
  // default RX and TX phase adjust.
  POKE(0xD6E5, 0x30);

  // Reset ethernet
  eth_reset();

  POKE(0xD6E5,0x32); // enable buff ID peeking via CRC-disable bit
  snprintf(msg, 160, "Initial buffer status: [CPU=$%02x, ETH=$%02x]", PEEK(0xD6EC),PEEK(0xD6EE));
  POKE(0xD6E5,0x30); // enable buff ID peeking via CRC-disable bit
  println_text80(1, msg);
  
  // Accept broadcast and multicast frames, and enable promiscuous mode
  POKE(0xD6E5, 0x30);

  // Find PHY for MDIO access, and select register 1 that has the signals we really care about
  for (phy = 0; phy != 0x20; phy++)
    if (mdio_read_register(phy, 1) != 0xffff)
      break;
  if (phy == 0x20) {
    println_text80(7, "WARNING: Could not find PHY address.");
  }
  snprintf(msg, 160, "Network PHY is at MDIO address $%x", phy);
  println_text80(1, msg);

#if 0
  i=mdio_read_register(phy,0x1f);
  if (i&(1<<10)) {
    snprintf(msg, 160, "WARNING: Cable-unplug power-saving enabled ($%04x).", i);
    println_text80(2, msg);
    mdio_write_register(phy,0x1f,i&0xfbff);
  }
  i=mdio_read_register(phy,0x1f);
  if (i&(1<<10)) {
    snprintf(msg, 160, "WARNING: Cable-unplug power-saving still enabled.", phy);
    println_text80(2, msg);
  }
  if ((i&(1<<7))) {
    snprintf(msg, 160, "WARNING: PHY using 25MHz clock: Setting to 50MHz", phy);
    println_text80(2, msg);
    mdio_write_register(phy,0x1f,i&0xff7f);
  }
  mdio_write_register(phy,0x1f,0x8100);
  i=mdio_read_register(phy,0x1f);
  if ((i&(1<<7))) {
    snprintf(msg, 160, "WARNING: PHY still using 25MHz clock after setting to 50MHz", phy);
    println_text80(2, msg);
  }
  i=mdio_read_register(phy,0x18);
  if (i&(1<<11)) {
    snprintf(msg, 160, "WARNING: Cable-unplug auto-neg power-saving enabled.", i);
    println_text80(2, msg);
    mdio_write_register(phy,0x18,i&0xf7ff);
  }
  i=mdio_read_register(phy,0x18);
  if (i&(1<<11)) {
    snprintf(msg, 160, "WARNING: Cable-unplug auto-neg power-saving still enabled.", phy);
    println_text80(2, msg);
  }
#endif

  mdio_dump();
  
  clear_rx_queue();
    
  // Apply KSZ8081RND errata
  errata_ksz8081rnd();

#if 0
  while(PEEK(0xD610)) POKE(0xD610,0);
  while(!PEEK(0xD610)) continue;
#endif

  // Get initial value of MDIO register 1 for monitoring link status changes
  last_mdio1 = mdio_read_register(phy,0x01);

  while (1) {

    switch(PEEK(0xD610)) {
    case 0x52: case 0x72:
      eth_reset();
      POKE(0xD610,0);
      println_text80(7,"Resetting Ethernet PHY");
      break;      
    case 0x31:
    case 0x32:
    case 0x33:           
      // Adjust REFCLK pad delay. According to the errata for the ethernet PHY.
      // This register is not documented in the main datasheet, however.
      i=mdio_read_register(phy,0x19);
      i&=0x0fff;
      // Select -0.6ns (|=0x0000), 0ns (|=0x7000) or +0.6ns (|=0xf000) for REFCLK
      // pad delay.
      switch(PEEK(0xD610)) {
      case 0x31: i=0x0000; break;
      case 0x32: i=0x7777; break;
      case 0x33: i=0xffff; break;
      }
      mdio_write_register(phy,0x19,i);
      mdio_write_register(phy,0x1a,i);
      snprintf(msg,80,"Setting register $19 to $%04x\n",i);
      println_text80(7,msg);
      
      POKE(0xD610,0);
      break;
    case 0x34:
      // default drive current
      mdio_write_register(phy,0x11,0x0000);
      POKE(0xD610,0);
      break;      
    case 0x35:
      // 8mA drive current
      mdio_write_register(phy,0x11,0x8000);
      POKE(0xD610,0);
      break;      
    case 0x36:
      // 8mA drive current
      mdio_write_register(phy,0x11,0xA000);
      POKE(0xD610,0);
      break;      
    case 0x37:
      // 14mA drive current
      mdio_write_register(phy,0x11,0xC000);
      POKE(0xD610,0);
      break;      
    case 0x38:
      // 10mA drive current
      mdio_write_register(phy,0x11,0xE000);
      POKE(0xD610,0);
      break;      

    case 0x4d: case 0x6d:
      // Show MDIO registers
      mdio_dump();
      POKE(0xD610,0);      
      break;
    case 0x4c: case 0x6c:
      // Send a single loop-back frame for testing
      send_loop_frame=1;
      POKE(0xD610,0);      
      break;
    case 0x49: case 0x69:
      icmp_only^=1;
      if (icmp_only) println_text80(7,"Will report only ICMP frames.");
      else println_text80(7,"ICMP filter disactivated.");
      POKE(0xD610,0);      
      break;
    case 0x44: case 0x64:
      // Digital loopback test      

      clear_rx_queue();
      
      total_loop_frames=0;
      bad_loop_frames=0;
      missed_loop_frames=0;      
      for(i=0;i<1024;i++) error_map[i]=0;
      
      // Enable digital loopback mode
      mdio_write_register(phy,0x00,0x6100);

      loopback_mode=1;

      println_text80(7, "Activated digital loopback test mode.");

      POKE(0xD610,0);
      break;
    case 0x03:

      // Cancel digital loopback mode, if it was selected
      mdio_write_register(phy,0x00,0x0100);

      loopback_mode=0;

      println_text80(7, "Ending loopback test mode.");
      
      POKE(0xD610,0);
      break;
    case 0x00:
      break;
    default:
      // Eat unsupported characters
      POKE(0xD610,0);
    }
    
#if 0
    if (PEEK(0xD6EF) != last_d6ef) {
      last_d6ef = PEEK(0xD6EF);
      snprintf(msg, 160, "#$%02x : $D6EF change: CPU buffer=%d, ETH buffer=%d, toggles=%d, rotates=%d", PEEK(0xD7FA),
          (PEEK(0xD6EF) >> 0) & 3, (PEEK(0xD6EF) >> 2) & 3, (PEEK(0xD6EF) >> 6) & 3, (PEEK(0xD6EF) >> 4) & 0x3);
      println_text80(1, msg);
    }
#endif
    
    // Check for new packets
    if (PEEK(0xD6E1) & 0x20) // Packet received
    {
      // Pop a frame from the buffer list
      POKE(0xD6E1, 0x01);
      POKE(0xD6E1, 0x03);
      POKE(0xD6E1, 0x01);
      lcopy(0xFFDE800L, (long)frame_buffer, sizeof(frame_buffer));
      lfill((long)msg, 0, 160);
      len = frame_buffer[0] + ((frame_buffer[1] & 0xf) << 8);

      skip_frame=0;
      
      // Check if its a loop-back debug frame: If so, report only success or failure
      if ((frame_buffer[8]==0x01)&&(frame_buffer[9]==0x02)&&(frame_buffer[10]==0x03)&&
	  (frame_buffer[11]==0x40)&&(frame_buffer[12]==0x50)&&(frame_buffer[13]==0x60)) {

	unsigned int loop_frame_num=frame_buffer[16+0]+(frame_buffer[16+1]<<8);
	
	// It's a loopback frame
	saw_loop_frame=1;

	if (total_loop_frames!=loop_frame_num) {
	  snprintf(msg,80,"ERROR: Expected loopback frame #%d, but saw #%d",total_loop_frames,loop_frame_num);
	  println_text80(2, msg);
	  for(i=0;i<100;i++) wait_10ms();
	}
	
	for(i=0;i<256;i++) {
	  if (frame_buffer[16+i]!=loop_data[i]) break;	  
	}
	total_loop_frames++;
	if (i==256) {
	  //	  println_text80(1,"Good loop back frame");
	  // skip_frame=1;
	  show_hex = 1;      
	  good_loop_frames++;
	  if (good_loop_frames==50) {
	    println_text80(1, "50 consecutive loop-back frames received without error.");	    
	    good_loop_frames=0;
	  }
	}
	else {

	  bad_loop_frames++;
	  snprintf(msg,80,"Error in loop-back frame detected after %d good frames (position %d).",good_loop_frames,i);	    
	  println_text80(2, msg);
	  good_loop_frames=0;
	  // Show the bad frame
	  //	  skip_frame=0;

	  for(i=0;i<1024;i++) {
	    unsigned char mask=0xc0 >> ((i&3) << 1);
	    if ((frame_buffer[16+(i>>2)]&mask)!=(loop_data[i>>2]&mask)) {
	      POKE(0xc002,0x21);
	      if (error_map[i]<9) {
		error_map[i]++;
	      } else {
		// Clear screen before displaying loop-back error report
		lfill(0xc000,0x20,4000);

		println_text80(7,"Loop-back test complete. Error map at top of screen.");
		snprintf(msg,80,"  %d frames sent, of which %d had errors, and %d were not received",
			 total_loop_frames,bad_loop_frames,missed_loop_frames);
		println_text80(1,msg);

		// Show info about errata workaround mode
		i=mdio_read_register(phy,0x19);
		snprintf(msg,80,"  REFCLK pad skew = 0x%04x",i);
		println_text80(1,msg);
		i=mdio_read_register(phy,0x11);
		snprintf(msg,80,"  Drive current select = 0x%04x",i);
		println_text80(1,msg);
		
		// Display loopback error map density after a while
		for(i=0;i<1024;i++) {
		  // Convert map to numeric values, with . meaning no errors
		  if (error_map[i]==0) error_map[i]='.';
		  else error_map[i]+=0x30;
		}
		lcopy(error_map,0xC000,0x0400);
		for(i=0;i<1024;i++)
		  if (error_map[i]!='.') {
		    switch(i&7) {
		    case 0: lpoke(0xff80000L+i,0x05); break;
		    case 1: lpoke(0xff80000L+i,0x04); break;
		    case 2: lpoke(0xff80000L+i,0x03); break;
		    case 3: lpoke(0xff80000L+i,0x07); break;
		    case 4: lpoke(0xff80000L+i,0x25); break;
		    case 5: lpoke(0xff80000L+i,0x24); break;
		    case 6: lpoke(0xff80000L+i,0x23); break;
		    case 7: lpoke(0xff80000L+i,0x27); break;
		    }
		  } else {
		    // light blue for all positions without errors
		    if (i&4) lpoke(0xff80000L+i,0x2e);
		    else lpoke(0xff80000L+i,0x0e);
		  }
		// Wait for key press and reset accumulation
		while(!PEEK(0xD610)) continue;
		POKE(0xD610,0);
		lfill(error_map,0,1024);
		// And cancel loop-back mode
		loopback_mode=0;
		// Cancel digital loopback mode, if it was selected
		mdio_write_register(phy,0x00,0x0100);
		total_loop_frames=0;
		bad_loop_frames=0;
		missed_loop_frames=0;
	      }		
	    } else POKE(0xc002,0x2e);
	  }	  
	}

      }

      if (!skip_frame) {

	if (!icmp_only) {

	  POKE(0xD6E5,0x32); // enable buff ID peeking via CRC-disable bit
	  snprintf(msg, 160, "#$%02x : Ethernet Frame #%d [CPU=$%02x, ETH=$%02x]", PEEK(0xD7FA), ++frame_count,
		   PEEK(0xD6EC),PEEK(0xD6EE));
	  POKE(0xD6E5,0x30); // enable buff ID peeking via CRC-disable bit
	  println_text80(1, msg);
	  
	  snprintf(msg, 160, "  %02x:%02x:%02x:%02x:%02x:%02x > %02x:%02x:%02x:%02x:%02x:%02x : len=%d($%x), rxerr=%c",
		   frame_buffer[8], frame_buffer[9], frame_buffer[10], frame_buffer[11], frame_buffer[12], frame_buffer[13],
		   frame_buffer[2], frame_buffer[3], frame_buffer[4], frame_buffer[5], frame_buffer[6], frame_buffer[7], len, len,
		   (frame_buffer[1] & 0x80) ? 'Y' : 'N');
	  println_text80(13, msg);
	  show_hex = 1;
	} else show_hex=0;
	
	if (frame_buffer[16] == 0x45) {
	  // IPv4
	  start_of_protocol_header = 36;
	  snprintf(msg, 160, "  IPv4: %d.%d.%d.%d -> %d.%d.%d.%d", frame_buffer[2 + 14 + 12 + 0], frame_buffer[28 + 1],
		   frame_buffer[28 + 2], frame_buffer[28 + 3], frame_buffer[32 + 0], frame_buffer[32 + 1], frame_buffer[32 + 2],
		   frame_buffer[32 + 3]);
	  println_text80(13, msg);
	  if (frame_buffer[16 + 9] == 0x01) {
	    // ICMP
	    if (frame_buffer[16 + 20] == 0x08) {
	      // PING
	      snprintf(msg, 160, "  ICMP: Echo request, id=%d, seq=%d",
		       frame_buffer[16 + 20 + 5] + (frame_buffer[16 + 20 + 4] << 8),
		       frame_buffer[16 + 20 + 7] + (frame_buffer[16 + 20 + 6] << 8));
	      println_text80(13, msg);
	      show_hex=0;
	    }
	    else if (frame_buffer[16 + 20] == 0x03) // DESTINATION UNREACHABLE
	      {
		printf("  ICMP: Destination Unreachable, code=%d, nexthopMTU=%d", frame_buffer[start_of_protocol_header + 1],
		       to_decimal(&frame_buffer[start_of_protocol_header + 6], 2));
	      }
	    else if (frame_buffer[16 + 20] == 0x05) // redirect
	      {
		printf("  ICMP: Redirect, code=%d, ipaddress=%d.%d.%d.%d,", frame_buffer[start_of_protocol_header + 1],
		       frame_buffer[start_of_protocol_header + 4], frame_buffer[start_of_protocol_header + 5],
		       frame_buffer[start_of_protocol_header + 6], frame_buffer[start_of_protocol_header + 7]);
	      }
	  }
	  else if (icmp_only) {
	    show_hex=0;
	  }
	  else if (frame_buffer[16 + 9] == 0x06) // TCP
	    {
	      printf("  TCP Protocol:\n");
	      printf("    source port is %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 0], 2));
	      printf("    destination port is %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 2], 2));
	      printf("    sequence number is %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 4], 4));
	      printf("    ackknowledement number is %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 8], 4));
	      printf("    data offset is %d\n", frame_buffer[start_of_protocol_header + 12] >> 4);
	      printf("    flags: ");
	      if (frame_buffer[start_of_protocol_header + 12] & 1) {
		printf("NS ");
	      }
	      if ((frame_buffer[start_of_protocol_header + 13] >> 7) & 1) {
		printf("CWR ");
	      }
	      if ((frame_buffer[start_of_protocol_header + 13] >> 6) & 1) {
		printf("ECE ");
	      }
	      if ((frame_buffer[start_of_protocol_header + 13] >> 5) & 1) {
		printf("URG ");
	      }
	      if ((frame_buffer[start_of_protocol_header + 13] >> 4) & 1) {
		printf("ACK ");
	      }
	      if ((frame_buffer[start_of_protocol_header + 13] >> 3) & 1) {
		printf("PSH ");
	      }
	      if ((frame_buffer[start_of_protocol_header + 13] >> 2) & 1) {
		printf("RST ");
	      }
	      if ((frame_buffer[start_of_protocol_header + 13] >> 1) & 1) {
		printf("SYN ");
	      }
	      if ((frame_buffer[start_of_protocol_header + 13] >> 0) & 1) {
		printf("FIN ");
	      }
	      printf("\n");
	      printf("    window size is %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 14], 2));
	      printf("    checksum is %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 16], 2));
	      printf("    urgent pointer is %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 18], 2));
	      
	      // data offset gives us the sie of the tcp header in 32 bit words,
	      // ie to get to the payload we increment by dataoffset * 4 bytes
	      start_of_protocol_header += ((frame_buffer[start_of_protocol_header + 12] >> 4) * 4);
	      
	      // HTTP tests
	      // evaulate the payload
	      if (frame_buffer[start_of_protocol_header + 0] == 'P' && frame_buffer[start_of_protocol_header + 1] == 'O'
		  && frame_buffer[start_of_protocol_header + 2] == 'S' && frame_buffer[start_of_protocol_header + 3] == 'T') {
		printf("HTTP: Post\n");
		for (i = 5; frame_buffer[start_of_protocol_header + i] != ' '; i++) {
		  putchar(frame_buffer[start_of_protocol_header + i]);
		}
		putchar('\n');
	      }
	      else if (frame_buffer[start_of_protocol_header + 0] == 'G' && frame_buffer[start_of_protocol_header + 1] == 'E'
		       && frame_buffer[start_of_protocol_header + 2] == 'T') {
		printf("HTTP: Get\n");
		for (i = 4; frame_buffer[start_of_protocol_header + i] != ' '; i++) {
		  putchar(frame_buffer[start_of_protocol_header + i]);
		}
		putchar('\n');
	      }
	    }
	  else if (frame_buffer[16 + 9] == 0x11) // UDP
	    {
	      printf("  UDP Protocol:\n");
	      // udp contains a source port
	      printf("    source port is %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 0], 2));
	      // a destination port
	      printf("    destination port is %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 2], 2));
	      // length
	      printf("    length is %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 4], 2));
	      // checksum
	      printf("    checksum is %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 6], 2));
	    }
	}
	else if (frame_buffer[15] == 0x06) // ARP
	  {
	    start_of_protocol_header = 16;
	    printf("  ARP protocol:\n");
	    printf("    Hardware type: %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 0], 2));
	    printf("    Protocol type: %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 2], 2));
	    printf("    Hardware address length: %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 4], 1));
	    printf("    Protocol address length: %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 5], 1));
	    printf("    Opcode: %d\n", to_decimal(&frame_buffer[start_of_protocol_header + 6], 2));
	    printf("    Sender Hardware Address: %02x:%02x:%02x:%02x:%02x:%02x\n", frame_buffer[start_of_protocol_header + 8],
		   frame_buffer[start_of_protocol_header + 9], frame_buffer[start_of_protocol_header + 10],
		   frame_buffer[start_of_protocol_header + 11], frame_buffer[start_of_protocol_header + 12],
		   frame_buffer[start_of_protocol_header + 13]);
	    printf("    Sender IP address: %d.%d.%d.%d\n", frame_buffer[start_of_protocol_header + 14],
		   frame_buffer[start_of_protocol_header + 15], frame_buffer[start_of_protocol_header + 16],
		   frame_buffer[start_of_protocol_header + 17]);
	    printf("    Target Hardware Address: %02x:%02x:%02x:%02x:%02x:%02x\n", frame_buffer[start_of_protocol_header + 18],
		   frame_buffer[start_of_protocol_header + 19], frame_buffer[start_of_protocol_header + 20],
		   frame_buffer[start_of_protocol_header + 21], frame_buffer[start_of_protocol_header + 22],
		   frame_buffer[start_of_protocol_header + 23]);
	    printf("    Target IP address: %d.%d.%d.%d\n", frame_buffer[start_of_protocol_header + 24],
		   frame_buffer[start_of_protocol_header + 25], frame_buffer[start_of_protocol_header + 26],
		   frame_buffer[start_of_protocol_header + 27]);
	  }
	if (show_hex) {
	  for (i = 0; i < 256 && i < len; i += 16) {
	    lfill((long)msg, 0, 160);
	    snprintf(msg, 160,
		     "       %04x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x  "
		     "%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
		     i, frame_buffer[14 + i], frame_buffer[15 + i], frame_buffer[16 + i], frame_buffer[17 + i],
		     frame_buffer[18 + i], frame_buffer[19 + i], frame_buffer[20 + i], frame_buffer[21 + i], frame_buffer[22 + i],
		     frame_buffer[23 + i], frame_buffer[24 + i], frame_buffer[25 + i], frame_buffer[26 + i], frame_buffer[27 + i],
		     frame_buffer[28 + i], frame_buffer[29 + i],
		     
		     safe_char(frame_buffer[14 + i]), safe_char(frame_buffer[15 + i]), safe_char(frame_buffer[16 + i]),
		     safe_char(frame_buffer[17 + i]), safe_char(frame_buffer[18 + i]), safe_char(frame_buffer[19 + i]),
		     safe_char(frame_buffer[20 + i]), safe_char(frame_buffer[21 + i]), safe_char(frame_buffer[22 + i]),
		     safe_char(frame_buffer[23 + i]), safe_char(frame_buffer[24 + i]), safe_char(frame_buffer[25 + i]),
		     safe_char(frame_buffer[26 + i]), safe_char(frame_buffer[27 + i]), safe_char(frame_buffer[28 + i]),
		     safe_char(frame_buffer[29 + i]));
	    println_text80(14, msg);
	  }
	}

#if 0
	// Wait for a key between each frame
	while(!PEEK(0xD610)) POKE(0xD020,PEEK(0xD020)+1);
	POKE(0xD610,0);
#endif	
      }
    }

    // Periodically ask for update to MDIO register read
    if (PEEK(0xD7FA) != last_frame_num) {

      // If in loopback mode, send a frame
      if (loopback_mode||send_loop_frame) {
	if (!saw_loop_frame) {
	  missed_loop_frames++;
	  println_text80(8,"Missed loopback frame. CRC error?");
	}
	// DST=broadcast
	for(i=0;i<6;i++) frame_buffer[i]=0xff;
	// SRC=01:02:03:40:50:60
	for(i=6;i<12;i++) frame_buffer[i]=(i-5)<<((i>8)?4:0);
	// TYPE=0x1234
	frame_buffer[12]=0x12;
	frame_buffer[13]=0x34;
	// 256 bytes of data in the frame

	switch(last_frame_num&3) {
	case 0:
	  // Ascending values
	  for(i=0;i<256;i++) loop_data[i]=i;      
	  break;
	case 1:
	  // All zeroes
	  for(i=0;i<256;i++) loop_data[i]=0;      
	  break;
	case 2:
	  // All $FF
	  for(i=0;i<256;i++) loop_data[i]=0xff;      
	  break;
	case 3:
	  // Pseudo-random data
	  for(i=0;i<256;i++) loop_data[i]=PEEK(256*(last_frame_num>>2));      
	  break;
	}

	loop_data[0]=total_loop_frames;
	loop_data[1]=total_loop_frames>>8;
	
	for(i=0;i<256;i++) frame_buffer[14+i]=loop_data[i];
	// Copy to eth frame buffer and TX frame of 14+256 bytes
	lcopy((unsigned long)frame_buffer,0xffde800,14+256);
	// Set frame length to 14 + 1*256
	POKE(0xD6E2,14);
	POKE(0xD6E3,1);
	// TX frame
	POKE(0xD6E4,0x01);
	saw_loop_frame=0;
	send_loop_frame=0;
      }

      // Report MDIO changes
      mdio1 = mdio_read_register(phy,0x01);
      // Also ignore bung reads where it reads back as 0xffff
      if ((mdio1 != last_mdio1)&& (mdio1 !=0xffff)) {
	
	snprintf(msg,80,"MDIO register 1 = $%04x, was $%04x",mdio1,last_mdio1);
	println_text80(7,msg);
	mdio_dump();
	// And re-prime to read register 1 again after dumping the registers
	POKE(0xD6E6L, 1);
	
	if ((mdio1 ^ last_mdio1) & (1 << 5)) {
	  if (mdio1 & (1 << 5))
	    println_text80(5, "PHY: Auto-negotiation complete");
	  else
	    println_text80(7, "PHY: Auto-negotiation in progress");
	}
	if ((mdio1 ^ last_mdio1) & (1 << 2)) {
	  if (mdio1 & (1 << 2))
	    println_text80(5, "PHY: Link is up");
	  else
	    println_text80(2, "PHY: Link DOWN");
	}
	
	TEST(1, mdio1, 4, "Remote fault");
	TEST(1, mdio1, 1, "Jabber detected");
	
	last_mdio1 = mdio1;
      }

      // Schedule next read
      last_frame_num = PEEK(0xD7FA);
    }

  }

#ifdef NATIVE_TEST
  return 0;
#endif
}
