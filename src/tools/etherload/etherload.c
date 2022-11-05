
/* Sample UDP client */

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>

#define PORTNUM 4510

void maybe_send_ack(void);
long long gettime_us(void);

long long start_time;

int packet_seq=0;

char all_done_routine[128] = {
  // Dummy inc $d020 jmp *-3 routine for debugging
  0xa9, 0x00, 0xee, 0x20, 0xd0, 0x4c, 0x2c, 0x68,

  // Production routine that skips the jmp *-3 loop
  //  0xa9, 0x00, 0xea, 0xea, 0xea, 0xea, 0xea, 0xea,

  // Copy routine to $0340
  0xa2, 0x00,
  0xbd, 0x44, 0x68,
  0x9d, 0x40, 0x03,
  0xe8,
  0xe0, 0x40,
  0xd0, 0xf5,
  0x4c, 0x40, 0x03,
  // All done routine that runs at $0340:
  // 1. M65 IO mode
  0xa9, 0x47,
  0x8d, 0x2f, 0xd0,
  0xa9, 0x53,
  0x8d, 0x2f, 0xd0,
  // 2. Clear MB of MAP
  0xa9, 0x00,
  0xa2, 0x0f,
  0xa0, 0x00,
  0xa3, 0x00,
  0x5c,
  0xea,
  // 3. Clear MAP of ethernet buffer
  0xa9, 0x00,
  0xa2, 0x00,
  0xa0, 0x00,
  0xa3, 0x00,
  0x5c,
  0xea,
  // 4. CLI
  0x18,

  // JMP_OFFSET=55 if nothing follows here
    
  // JMP 2061
#define JMP_OFFSET (55)
  0x4c,0x0d,0x08
};

unsigned char dma_load_routine[1280] = {

  // Routine that copies packet contents by DMA
  0xa9,0x00, // Dummy LDA #$xx for signature detection
  0xee,0x00,0x04, // Draw a marker on the screen to indicate frames loaded

// #define DEBUG_WAIT_FOR_KEY_ON_EACH_DATA_FRAME
#ifdef DEBUG_WAIT_FOR_KEY_ON_EACH_DATA_FRAME
  // Wait for $d610 press
  0xee,0x27,0x04,
  0xad,0x10,0xd6,
  0xf0,0xf8,
  0x8d,0x10,0xd6,

#define EXTRA_BYTES (3+3+2+3)
#else
#define EXTRA_BYTES 0
#endif

#define BYTE_COUNT (3+2+2 +EXTRA_BYTES)
  // 5a. Wait for TX ready
  0xad,0xe1,0xd6,
  0x29,0x10,
  0xf0,0xf9,
  
  0x8d, 0x07, 0xd7, // STA $D707 to trigger in-line DMA
  
#define DMALIST_OFFSET (2+3+BYTE_COUNT+3)  
  0x80,0xff, // SRC MB is $FF
#define DESTINATION_MB_OFFSET (DMALIST_OFFSET+3)
  0x81, 0x00,   // Destination MB 
  0x00,    // DMA end of option list
  0x04, // copy + chained
#define BYTE_COUNT_OFFSET (DMALIST_OFFSET+6)
  0x00, 0x04,       // DMA byte count
  0x00, 0xe9, 0x8d, // DMA source address (points to data in packet)
#define DESTINATION_ADDRESS_OFFSET (DMALIST_OFFSET+11)
  0x00, 0x10, // DMA Destination address (bottom 16 bits)
#define DESTINATION_BANK_OFFSET (DMALIST_OFFSET+13)
  0x00,       // DMA Destination bank
  0x00, // DMA Sub command
  0x00, 0x00, // DMA modulo (ignored)

  // Use chained DMA to copy packet to TX buffer, and then send it
  // so that we can get what will effectively be an ACK to each
  // packet received by the MEGA65.
  0x80,0xff,
  0x81,0xff,
  0x00,              // DMA end of option list
  0x04,              // DMA copy, chained
  0x00, 0x06,        // DMA byte count 
  0x02, 0xe8, 0x8d,  // DMA source address
  0x00, 0xe8, 0x8d,  // DMA destination address
  0x00,              // DMA Sub command
  0x00, 0x00,        // DMA modulo (ignored)

  // Use DMA to swap MAC addresses
  0x00,              // DMA end of option list
  0x04,              // DMA copy, chained
  0x06, 0x00,        // DMA byte count 
  0x08, 0xe8, 0x8d,  // DMA source address
  0x00, 0xe8, 0x8d,  // DMA destination address
  0x00,              // DMA Sub command
  0x00, 0x00,        // DMA modulo (ignored)

  0x00,              // DMA end of option list
  0x00,              // DMA copy, end of chain
  0x06, 0x00,        // DMA byte count 
  0xe9, 0x36, 0x8d,  // DMA source address
  0x06, 0xe8, 0x8d,  // DMA destination address
  0x00,              // DMA Sub command
  0x00, 0x00,        // DMA modulo (ignored)
  
  
  // Code resumes after DMA list here

  // Reverse port numbers
  0xad,0x24,0x68,
  0x8d,0x24,0x68,
  0xad,0x25,0x68,
  0x8d,0x25,0x68,

  0xad,0x26,0x68,
  0x8d,0x22,0x68,
  0xad,0x27,0x68,
  0x8d,0x23,0x68,

  // Set packet len
  0xa9,0x2a, 
  0x8d,0xe2,0xd6,
  0xa9,0x05,
  0x8d,0xe3,0xd6,

  // Set source IP last byte to 65
  0xa9,0x41,
  //  0xa9,0xff, // XXX DEBUG - set it to broadcast to avoid IP/UDP header crc changes
  0x8d,0x1d,0x68,
  // Set dest IP last byte to that of sender
  0xad,0x1f,0x68,
  0x8d,0x21,0x68,

  // Patch IP checksum from changing *.255 to *.65 = add $BE to low byte
  0xad,0x1b,0x68,
  0x18,
  0x69,0xbe,
  0x8d,0x19,0x68,
  0xad,0x1a,0x68,
  0x69,0x00,
  0x8d,0x18,0x68,
 
  // Patch UDP checksum from changing *.255 to *.65 = add $BE to low byte
  0xad,0x2b,0x68,
  0x18,
  0x69,0xbe,
  0x8d,0x29,0x68,
  0xad,0x2a,0x68,
  0x69,0x00,
  0x8d,0x28,0x68,

  // 5. TX packet
  0xa9,0x01,
  0x8d,0xe4,0xd6,

  // Return to packet wait loop
  0x60, // RTS 
  
#define DATA_OFFSET (0x100 - 0x2c)
};

unsigned char colour_ram[1000];
unsigned char progress_screen[1000];

int sockfd;
struct sockaddr_in servaddr;

int progress_print(int x,int y, char *msg)
{
  int ofs=y*40+x;
  for(int i=0;msg[i];i++) {
    if (msg[i]>='A'&&msg[i]<='Z') progress_screen[ofs]=msg[i]-0x40;
    else if (msg[i]>='a'&&msg[i]<='z') progress_screen[ofs]=msg[i]-0x60;
    else progress_screen[ofs]=msg[i];
    ofs++;
    if (ofs>999) ofs=999;
  }
  return 0;
}

int progress_line(int x,int y,int len)
{
  int ofs=y*40+x;
  for(int i=0;i<len;i++) {
    progress_screen[ofs]=67;    
    ofs++;
    if (ofs>999) ofs=999;
  }
  return 0;
}

unsigned char hyperrupt_trigger[128];
unsigned char magic_string[12]={
  0x65,0x47,0x53, // 65 G S
  0x4b,0x45,0x59, // KEY
  0x43,0x4f,0x44,0x45, // CODE
  0x00,0x80  // Magic key code $8000 = ethernet hypervisor trap
};


int trigger_eth_hyperrupt(void)
{
  int offset=0x38;
  memcpy(&hyperrupt_trigger[offset],magic_string,12);

  sendto(sockfd, hyperrupt_trigger, sizeof hyperrupt_trigger, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  usleep(10000);

  return 0;
}

void dump_bytes(char *msg,unsigned char *b,int len)
{
  fprintf(stderr,"%s:\n",msg);
  for(int i=0;i<len;i+=16) {
    fprintf(stderr,"%04x:",i);
    int max=16;
    if ((i+max)>len) max=len=i;
    for(int j=0;j<max;j++) {
      fprintf(stderr," %02x",b[i+j]);
    }
    for(int j=max;j<16;j++) fprintf(stderr,"   ");
    fprintf(stderr,"  ");
    for(int j=0;j<max;j++) {
      if (b[i+j]>=0x20&&b[i+j]<0x7f) fprintf(stderr,"%c",b[i+j]); else fprintf(stderr,"?");
    }
    fprintf(stderr,"\n");
  }
  return;
}

#define MAX_UNACKED_FRAMES 32
int frame_unacked[MAX_UNACKED_FRAMES]={0};
long frame_load_addrs[MAX_UNACKED_FRAMES]={-1};
unsigned char unacked_frame_payloads[MAX_UNACKED_FRAMES][1280];

int retx_interval=1000;

int check_if_ack(unsigned char *b)
{

#if 1
  printf("Pending acks:\n");  
  for(int i=0;i<MAX_UNACKED_FRAMES;i++) {
    if (frame_unacked[i]) printf("  Frame ID #%d : addr=$%lx\n",i,frame_load_addrs[i]);
  }
#endif

  long ack_addr=
    (b[DESTINATION_MB_OFFSET]<<20)
    +((b[DESTINATION_BANK_OFFSET]&0xf)<<16)
    +(b[DESTINATION_ADDRESS_OFFSET+1]<<8)
    +(b[DESTINATION_ADDRESS_OFFSET+0]<<0);
  
  printf("T+%lld : RXd frame addr=$%lx, rx seq=$%04x, tx seq=$%04x\n",
	 gettime_us()-start_time,
	 ack_addr,
	 b[254]+(b[255]<<8),packet_seq
	 );
  // Set retry interval based on number of outstanding packets
  int seq_gap=(packet_seq-(b[254]+(b[255]<<8)));
  retx_interval=10000*seq_gap;
  if (retx_interval<1000) retx_interval=1000;
  if (retx_interval>500000) retx_interval=500000;
  

#define CHECK_ADDR_ONLY
#ifdef CHECK_ADDR_ONLY
  for(int i=0;i<MAX_UNACKED_FRAMES;i++) {
    if (frame_unacked[i]) {
      if (ack_addr==frame_load_addrs[i]) {
        frame_unacked[i]=0;
	printf("ACK addr=$%lx\n",frame_load_addrs[i]);
	return 1;
      }
    }
  }
#else
  for(int i=0;i<MAX_UNACKED_FRAMES;i++) {
    if (frame_unacked[i]) {
      if (!memcmp(unacked_frame_payloads[i],b,1280)) {
        frame_unacked[i]=0;
	printf("ACK addr=$%lx\n",frame_load_addrs[i]);
	return 1;
      } else {
#if 0
	for(int j=0;j<1280;j++) {
	  if (unacked_frame_payloads[i][j]!=b[j]) {
	    printf("Mismatch frame id #%d offset %d : $%02x vs $%02x\n",
		   i,j,unacked_frame_payloads[i][j],b[j]);
	    dump_bytes("unacked",unacked_frame_payloads[i],128);
	    break;
	  }
	}
#endif
      }
    }
  }
#endif
  return 0;
}

long long last_resend_time=0;

// From os.c in serval-dna
long long gettime_us(void)
{
  long long retVal = -1;

  do {
    struct timeval nowtv;

    // If gettimeofday() fails or returns an invalid value, all else is lost!
    if (gettimeofday(&nowtv, NULL) == -1) {
      break;
    }

    if (nowtv.tv_sec < 0 || nowtv.tv_usec < 0 || nowtv.tv_usec >= 1000000) {
      break;
    }

    retVal = nowtv.tv_sec * 1000000LL + nowtv.tv_usec;
  } while (0);

  return retVal;
}

int resend_frame=0;

int expect_ack(long load_addr,unsigned char *b)
{
  while(1) {
    int addr_dup=-1;
    int free_slot=-1;
    for(int i=0;i<MAX_UNACKED_FRAMES;i++) {
      if (frame_unacked[i]) {
	if (frame_load_addrs[i]==load_addr) { addr_dup=i; break; }
      }
      if ((!frame_unacked[i])&&(free_slot==-1)) free_slot=i;
    }
    if ((free_slot!=-1)&&(addr_dup==-1)) {
      // We have a free slot to put this frame, and it doesn't
      // duplicate the address of another frame.
      // Thus we can safely just note this one
      printf("Expecting ack of addr=$%lx @ %d\n",load_addr,free_slot);
      memcpy(unacked_frame_payloads[free_slot],b,1280);
      frame_unacked[free_slot]=1;
      frame_load_addrs[free_slot]=load_addr;
      return 0;
    }
    // We don't have a free slot, or we have an outstanding
    // frame with the same address that we need to see an ack
    // for first.

    // Check for the arrival of any acks
    unsigned char ackbuf[8192];
    int count=0;
    int r=0;
    while(r>-1&&count<100) {
      r=recv(sockfd,ackbuf,sizeof(ackbuf),MSG_DONTWAIT);
      if (r==1280) check_if_ack(ackbuf);
    }
    // And re-send the first unacked frame from our list
    // (if there are still any unacked frames)
    maybe_send_ack();

    // Finally wait a short period of time, that should be slightly
    // longer than the time it takes to send a 1280 byte UDP frame.
    // On-wire frame will be ~1400 bytes = 11,200 bits = ~112 usec
    // So we will wait 200 usec.
    usleep(200);
    // XXX DEBUG slow things down
    //    usleep(10000);
  }
  return 0;
}

int no_pending_ack(int addr)
{
    for(int i=0;i<MAX_UNACKED_FRAMES;i++) {
      if (frame_unacked[i]) {
	if (frame_load_addrs[i]==addr) return 0;
      }
    }
    return 1;
}

void maybe_send_ack(void)
{
  int i=0;
  int unackd[MAX_UNACKED_FRAMES];
  int ucount=0;
  for(i=0;i<MAX_UNACKED_FRAMES;i++) if (frame_unacked[i]) unackd[ucount++]=i;

  if (ucount) {
    if ((gettime_us()-last_resend_time)>retx_interval) {

      //      if (retx_interval<500000) retx_interval*=2;
      
      resend_frame++;
      if (resend_frame>=ucount) resend_frame=0; 
      int id=unackd[resend_frame];
      if (1)
	printf("T+%lld : Resending addr=$%lx @ %d (%d unacked), seq=$%04x, data=%02x %02x\n",
	       gettime_us()-start_time,	       
	       frame_load_addrs[id],id,ucount,packet_seq,
	       unacked_frame_payloads[id][DATA_OFFSET+0],
	       unacked_frame_payloads[id][DATA_OFFSET+1]
	       );

      long ack_addr=
	(unacked_frame_payloads[id][DESTINATION_MB_OFFSET]<<20)
	+((unacked_frame_payloads[id][DESTINATION_BANK_OFFSET]&0xf)<<16)
	+(unacked_frame_payloads[id][DESTINATION_ADDRESS_OFFSET+1]<<8)
	+(unacked_frame_payloads[id][DESTINATION_ADDRESS_OFFSET+0]<<0);

      if (ack_addr!=frame_load_addrs[id]) {
	fprintf(stderr,"ERROR: Resending frame with incorrect load address: expected=$%lx, saw=$%lx\n",
		frame_load_addrs[id],ack_addr);
	exit(-1);
      }
      
      unacked_frame_payloads[id][254]=packet_seq;
      unacked_frame_payloads[id][255]=packet_seq>>8;
      packet_seq++;      
      sendto(sockfd, unacked_frame_payloads[id], 1280, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
      last_resend_time=gettime_us();
    }
    return;
  }
  if (!ucount) {
    printf("No unacked frames\n");
    return;
  }
}
  
int wait_all_acks(void)
{
  while(1) {
    int unacked=-1;
    for(int i=0;i<MAX_UNACKED_FRAMES;i++) {
      if (frame_unacked[i]) { unacked=i; break; }
    }
    if (unacked==-1) return 0;

    // Check for the arrival of any acks
    unsigned char ackbuf[8192];
    int count=0;
    int r=0;
    while(r>-1&&count<100) {
      r=recv(sockfd,ackbuf,sizeof(ackbuf),MSG_DONTWAIT);
      if (r==1280) check_if_ack(ackbuf);
    }

    maybe_send_ack();

    // Finally wait a short period of time, that should be slightly
    // longer than the time it takes to send a 1280 byte UDP frame.
    // On-wire frame will be ~1400 bytes = 11,200 bits = ~112 usec
    // So we will wait 200 usec.
    usleep(200);
  }
  return 0;
}


int send_mem(unsigned int address,unsigned char *buffer,int bytes)
{
  // Set position of marker to draw in 1KB units
  dma_load_routine[3]=address>>10;
  
  // Set load address of packet
  dma_load_routine[DESTINATION_ADDRESS_OFFSET] = address & 0xff;
  dma_load_routine[DESTINATION_ADDRESS_OFFSET + 1] = (address >> 8) & 0xff;
  dma_load_routine[DESTINATION_BANK_OFFSET] = (address>>16)&0x0f;
  dma_load_routine[DESTINATION_MB_OFFSET] = (address>>20);
  dma_load_routine[BYTE_COUNT_OFFSET] = bytes;
  dma_load_routine[BYTE_COUNT_OFFSET+1] = bytes>>8;
  
  // Copy data into packet
  memcpy(&dma_load_routine[DATA_OFFSET], buffer, bytes);

  // Add to queue of packets with pending ACKs
  expect_ack(address,dma_load_routine);

  // Send the packet initially
  printf("T+%lld : TX addr=$%x, seq=$%04x, data=%02x %02x ...\n",
	 gettime_us()-start_time,
	 address,packet_seq,
	 dma_load_routine[DATA_OFFSET],dma_load_routine[DATA_OFFSET+1]
	 );
  dma_load_routine[254]=packet_seq;
  dma_load_routine[255]=packet_seq>>8;
  packet_seq++;
  sendto(sockfd, dma_load_routine, sizeof dma_load_routine, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

  return 0;
}

int main(int argc, char **argv)
{

  start_time=gettime_us();
  
  if (argc != 3) {
    printf("usage: %s <IP address> <programme>\n", argv[0]);
    exit(1);
  }

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  int broadcastEnable = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (char *)&broadcastEnable, sizeof(broadcastEnable));

  fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, NULL) | O_NONBLOCK);
  
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr(argv[1]);
  servaddr.sin_port = htons(PORTNUM);

  // print out debug info
  printf("Using dst-addr: %s\n", inet_ntoa(servaddr.sin_addr));
  printf("Using src-port: %d\n", ntohs(servaddr.sin_port));

  // Try to get MEGA65 to trigger the ethernet remote control hypperrupt
  trigger_eth_hyperrupt();

  int fd = open(argv[2], O_RDWR);
  unsigned char buffer[1024];
  int offset = 0;
  int bytes;

  // Read 2 byte load address
  bytes = read(fd, buffer, 2);
  if (bytes < 2) {
    fprintf(stderr, "Failed to read load address from file '%s'\n", argv[2]);
    exit(-1);
  }

  char msg[80];
  
  int address = buffer[0] + 256 * buffer[1];
  printf("Load address of programme is $%04x\n", address);

  int start_addr=address;

  // Clear screen first
  printf("Clearing screen\n");
  memset(colour_ram,0x01,1000);
  memset(progress_screen,0x20,1000);
  send_mem(0xffd8000,colour_ram,1000);
  send_mem(0x0400,progress_screen,1000);
  wait_all_acks();
  printf("Screen cleared.\n");
  
  progress_line(0,0,40);
  snprintf(msg,40,"Loading \"%s\" at $%04X",argv[2],address);
  progress_print(0,1,msg);
  progress_line(0,2,40);
  
  while ((bytes = read(fd, buffer, 1024)) != 0) {
    printf("Read %d bytes at offset %d\n", bytes, offset);
    offset += bytes;

    // Send screen with current loading state
    progress_line(0,10,40);
    snprintf(msg,40,"Loading block @ $%04X",address);
    progress_print(0,11,msg);
    progress_line(0,12,40);

    // Update screen, but only if we are not still waiting for a previous update
    //    if (no_pending_ack(0x0400+4*40))
      send_mem(0x0400+4*40,&progress_screen[4*40],1000-4*40);
    
    send_mem(address,buffer,bytes);
    
    address += bytes;
  }

  memset(progress_screen,0x20,1000);
  snprintf(msg,40,"Loaded $%04X - $%04X",start_addr,address);
  progress_line(0,15,40);
  progress_print(0,16,msg);
  progress_line(0,17,40);
  send_mem(0x0400+4*40,&progress_screen[4*40],1000-4*40);
  
  // print out debug info
  printf("Sent %s to %s on port %d.\n\n", argv[2], inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port));

  printf("Now tell MEGA65 that we are all done.\n");

  wait_all_acks();

  // XXX - We don't check that this last packet has arrived, as it doesn't have an ACK mechanism (yet)
  // XXX - We should make it ACK as well.
  all_done_routine[JMP_OFFSET+1]=0x0d;
  all_done_routine[JMP_OFFSET+2]=0x08;
  if (1)
  for(int i=0;i<1;i++) {
    sendto(sockfd, all_done_routine, sizeof all_done_routine, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
    usleep(10000);
  }
  
  return 0;
}
