
/* Sample UDP client */

#include "helper_dma_load_routine_map.h"
#include "helper_all_done_routine_map.h"

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
#include <math.h>
#include <ctype.h>

#define PORTNUM 4510

void maybe_send_ack(void);
long long gettime_us(void);

long long start_time;

int packet_seq=0;
int last_rx_seq=0;

extern unsigned char c64_loram[1024];

extern char all_done_routine[];
extern int all_done_routine_len;
static const int cpuSpeedOffset = all_done_routine_offset_cpuspeed - all_done_routine_entry;
static const int jmpOffset = all_done_routine_offset_jump - all_done_routine_entry;

extern char dma_load_routine[];
extern int dma_load_routine_len;
static const int destMbOffset      = dma_load_routine_offset_dest_mb      - dma_load_routine_entry;
static const int destAddressOffset = dma_load_routine_offset_dest_address - dma_load_routine_entry;
static const int destBankOffset    = dma_load_routine_offset_dest_bank    - dma_load_routine_entry;
static const int byteCountOffset   = dma_load_routine_offset_byte_count   - dma_load_routine_entry;
static const int seqNumOffset      = dma_load_routine_offset_seq_num      - dma_load_routine_entry;
static const int dataOffset        = dma_load_routine_offset_data         - dma_load_routine_entry;

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

void update_retx_interval(void)
{
  int seq_gap=(packet_seq-last_rx_seq);
  retx_interval=2000+10000*seq_gap;
  if (retx_interval<1000) retx_interval=1000;
  if (retx_interval>500000) retx_interval=500000;
  //  printf("  retx interval=%dusec (%d vs %d)\n",retx_interval,packet_seq,last_rx_seq);
}



int check_if_ack(unsigned char *b)
{

#if 0
  printf("Pending acks:\n");  
  for(int i=0;i<MAX_UNACKED_FRAMES;i++) {
    if (frame_unacked[i]) printf("  Frame ID #%d : addr=$%lx\n",i,frame_load_addrs[i]);
  }
#endif

  // Set retry interval based on number of outstanding packets
  last_rx_seq = (b[seqNumOffset]+(b[seqNumOffset+1]<<8));
  update_retx_interval();

#if 0
  printf("T+%lld : RXd frame addr=$%lx, rx seq=$%04x, tx seq=$%04x\n",
	 gettime_us()-start_time,
	 ack_addr,
	 last_rx_seq,packet_seq
	 );
#endif
  
//#define CHECK_ADDR_ONLY
#ifdef CHECK_ADDR_ONLY
  long ack_addr=
    (b[destMbOffset]<<20)
    +((b[destBankOffset]&0xf)<<16)
    +(b[destAddressOffset+1]<<8)
    +(b[destAddressOffset+0]<<0);
  for(int i=0;i<MAX_UNACKED_FRAMES;i++) {
    if (frame_unacked[i]) {
      if (ack_addr==frame_load_addrs[i]) {
        frame_unacked[i]=0;
	//	printf("ACK addr=$%lx\n",frame_load_addrs[i]);
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

void maybe_send_ack(void);

int expect_ack(long load_addr, char *b)
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
      //      printf("Expecting ack of addr=$%lx @ %d\n",load_addr,free_slot);
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
      if (0)
	printf("T+%lld : Resending addr=$%lx @ %d (%d unacked), seq=$%04x, data=%02x %02x\n",
	       gettime_us()-start_time,	       
	       frame_load_addrs[id],id,ucount,packet_seq,
	       unacked_frame_payloads[id][dataOffset+0],
	       unacked_frame_payloads[id][dataOffset+1]
	       );

      long ack_addr=
	(unacked_frame_payloads[id][destMbOffset]<<20)
	+((unacked_frame_payloads[id][destBankOffset]&0xf)<<16)
	+(unacked_frame_payloads[id][destAddressOffset+1]<<8)
	+(unacked_frame_payloads[id][destAddressOffset+0]<<0);

      if (ack_addr!=frame_load_addrs[id]) {
	fprintf(stderr,"ERROR: Resending frame with incorrect load address: expected=$%lx, saw=$%lx\n",
		frame_load_addrs[id],ack_addr);
	exit(-1);
      }
      
      unacked_frame_payloads[id][seqNumOffset]=packet_seq;
      unacked_frame_payloads[id][seqNumOffset+1]=packet_seq>>8;
      packet_seq++;      
      update_retx_interval();
      sendto(sockfd, unacked_frame_payloads[id], 1280, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
      last_resend_time=gettime_us();
    }
    return;
  }
  if (!ucount) {
    //    printf("No unacked frames\n");
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
  dma_load_routine[destAddressOffset] = address & 0xff;
  dma_load_routine[destAddressOffset + 1] = (address >> 8) & 0xff;
  dma_load_routine[destBankOffset] = (address>>16)&0x0f;
  dma_load_routine[destMbOffset] = (address>>20);
  dma_load_routine[byteCountOffset] = bytes;
  dma_load_routine[byteCountOffset+1] = bytes>>8;
  
  // Copy data into packet
  memcpy(&dma_load_routine[dataOffset], buffer, bytes);

  // Add to queue of packets with pending ACKs
  expect_ack(address,dma_load_routine);

  // Send the packet initially
  if (0)
    printf("T+%lld : TX addr=$%x, seq=$%04x, data=%02x %02x ...\n",
	   gettime_us()-start_time,
	   address,packet_seq,
	   dma_load_routine[dataOffset],dma_load_routine[dataOffset+1]
	   );
  dma_load_routine[seqNumOffset]=packet_seq;
  dma_load_routine[seqNumOffset+1]=packet_seq>>8;
  packet_seq++;
  update_retx_interval();
  sendto(sockfd, dma_load_routine, dma_load_routine_len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

  return 0;
}

int main(int argc, char **argv)
{

  start_time=gettime_us();
  
  if (argc != 3) {
    printf("usage: %s <IP address> <programme>\n", argv[0]);
    exit(1);
  }
  const char* ipAddress = argv[1];
  const char* programmePath = argv[2];

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  int broadcastEnable = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (char *)&broadcastEnable, sizeof(broadcastEnable));

  fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, NULL) | O_NONBLOCK);
  
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr(ipAddress);
  servaddr.sin_port = htons(PORTNUM);

  // print out debug info
#if 0
  printf("Using dst-addr: %s\n", inet_ntoa(servaddr.sin_addr));
  printf("Using src-port: %d\n", ntohs(servaddr.sin_port));
#endif
  
  // Try to get MEGA65 to trigger the ethernet remote control hypperrupt
  trigger_eth_hyperrupt();

  int fd = open(programmePath, O_RDWR);
  unsigned char buffer[1024];
  int offset = 0;
  int bytes;

  // Read 2 byte load address
  bytes = read(fd, buffer, 2);
  if (bytes < 2) {
    fprintf(stderr, "Failed to read load address from file '%s'\n", programmePath);
    exit(-1);
  }

  char msg[80];
  
  int address = buffer[0] + 256 * buffer[1];
  printf("Load address of programme is $%04x\n", address);

  int start_addr=address;

  last_resend_time=gettime_us();

  // Clear screen first
  //  printf("Clearing screen\n");
  memset(colour_ram,0x01,1000);
  memset(progress_screen,0x20,1000);
  send_mem(0x1f800,colour_ram,1000);
  send_mem(0x0400,progress_screen,1000);
  wait_all_acks();
  //  printf("Screen cleared.\n");

  // Load C64 low memory, so that we can run C64 mode programs
  // even if the machine was in C65 mode or some random state first
  // (its probably the IRQ vectors etc that are important here)
  send_mem(0x0002,&c64_loram[0],0xfe);
  send_mem(0x0200,&c64_loram[0x1fe],0x200);
  
  progress_line(0,0,40);
  snprintf(msg,40,"Loading \"%s\" at $%04X",programmePath,address);
  progress_print(0,1,msg);
  progress_line(0,2,40);

  int entry_point=0x080d;
  
  while ((bytes = read(fd, buffer, 1024)) != 0) {
    //    printf("Read %d bytes at offset %d\n", bytes, offset);

    if (!offset) {
      for(int i=0;i<255;i++) if (buffer[i]==0x9e) {
	  // Found SYS command -- try to work out the address
	  int ofs=i+1;
	  float mult=1;
	  int val=0;
	  if (buffer[ofs]==0xff&&buffer[ofs+1]==0xac) { mult=3.14159265; ofs+=2; }
	  while(buffer[ofs]) {
	    if (isdigit(buffer[ofs])) {
	      val*=10;
	      val+=buffer[ofs]-'0';
	    }
	    ofs++;
	    if (buffer[ofs]==':') break;
	  }
	  entry_point=mult*val;
	  //	  printf("Program entry point via SYS %d\n",entry_point);
	  break;
	}
    }
    
    offset += bytes;

    // Send screen with current loading state
    progress_line(0,10,40);
    snprintf(msg,40,"Loading block @ $%04X",address);
    progress_print(0,11,msg);
    progress_line(0,12,40);

    // Update screen, but only if we are not still waiting for a previous update
    // so that we don't get stuck in lock-step
    if (no_pending_ack(0x0400+4*40))
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
  printf("Sent %s to %s on port %d.\n", programmePath, inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port));

  printf("Now telling MEGA65 that we are all done...\n");

  wait_all_acks();

  // XXX - We don't check that this last packet has arrived, as it doesn't have an ACK mechanism (yet)
  // XXX - We should make it ACK as well.
  printf("Program entry point via SYS %d\n",entry_point);
  all_done_routine[jmpOffset+1]=entry_point;
  all_done_routine[jmpOffset+2]=entry_point>>8;

  if (entry_point<8192) {
    // Probably C64 mode, so 1MHz CPU
    all_done_routine[cpuSpeedOffset+1]=64;
  } else {
    // Probably C65 mode, so 40MHz CPU, and don't stomp IO mode
    all_done_routine[cpuSpeedOffset+1]=65;
    all_done_routine[cpuSpeedOffset+4]=0xad; // turn STA $D02F into LDA $D02F
  }
  
  // Instead, we just send it 10 times to make sure
  for(int i=0;i<10;i++) {
    sendto(sockfd, all_done_routine, all_done_routine_len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  }
  
  return 0;
}
