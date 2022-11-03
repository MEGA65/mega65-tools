
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

#define PORTNUM 4510

char all_done_routine[128] = {
  // Dummy inc $d020 jmp *-3 routine for debugging
  //  0xa9, 0x00, 0xee, 0x20, 0xd0, 0x4c, 0x2c, 0x68,

  // 0xa9, 0x00, 0xee, 0x20, 0xd0, 0x4c, 0x2e, 0x68,
  0xa9, 0x00, 0xea, 0xea, 0xea, 0xea, 0xea, 0xea,

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
  // JMP 2061
#define JMP_OFFSET 55
  0x4c,0x0d,0x08
};

char dma_load_routine[256 + 1024] = {

  // Routine that copies packet contents by DMA
  0xa9,0x00, // Dummy LDA #$xx for signature detection
  0x8d, 0x07, 0xd7, // STA $D707 to trigger in-line DMA
    
#define DMALIST_OFFSET (2+3)
  // SRC MB is $FF
  0x80,0xff,
#define DESTINATION_MB_OFFSET (DMALIST_OFFSET+3)
  // Destination MB 
  0x81, 0x00,  
  // DMA end of option list
  0x00,
  
  0x04, 
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
  0x00,              // DMA copy, end of chain
  0x00, 0x06,        // DMA byte count 
  0x02, 0xe8, 0x8d,  // DMA source address
  0x00, 0xe8, 0x8d,  // DMA destination address
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

  // Copy source MAC to dest MAC
  0xad,0x08,0x68,0x8d,0x00,0x68,
  0xad,0x09,0x68,0x8d,0x01,0x68,
  0xad,0x0a,0x68,0x8d,0x02,0x68,
  0xad,0x0b,0x68,0x8d,0x03,0x68,
  0xad,0x0c,0x68,0x8d,0x04,0x68,
  0xad,0x0d,0x68,0x8d,0x05,0x68,
  
  // Set our MAC address
  0xad,0xe9,0xd6,0x8d,0x06,0x68,
  0xad,0xea,0xd6,0x8d,0x07,0x68,
  0xad,0xeb,0xd6,0x8d,0x08,0x68,
  0xad,0xec,0xd6,0x8d,0x09,0x68,
  0xad,0xed,0xd6,0x8d,0x0a,0x68,
  0xad,0xee,0xd6,0x8d,0x0b,0x68,
  
  // Set packet len
  0xa9,0x2a, 
  0x8d,0xe2,0xd6,
  0xa9,0x05,
  0x8d,0xe3,0xd6,

  // Set source IP last byte to 65
  0xa9,0x41,
  0x8d,0x1d,0x68,
  // Set dest IP last byte to that of sender
  0xad,0x1f,0x68,
  0x8d,0x21,0x68,
  
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

  while(1) {
    // Release 3 key
    magic_string[10]=0x26; magic_string[11]=0x10; 
    sendto(sockfd, hyperrupt_trigger, sizeof hyperrupt_trigger, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
    usleep(10000);
    return 0;
  }
  return 0;
  
  magic_string[10]=0x26;
  magic_string[11]=0x00;
  sendto(sockfd, hyperrupt_trigger, sizeof hyperrupt_trigger, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  usleep(500000);
  magic_string[11]=0x10;
  sendto(sockfd, hyperrupt_trigger, sizeof hyperrupt_trigger, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  
  usleep(500000);
  return 0;
}

int send_mem(unsigned int address,unsigned char *buffer,int bytes)
{
  // Set load address of packet
  dma_load_routine[DESTINATION_ADDRESS_OFFSET] = address & 0xff;
  dma_load_routine[DESTINATION_ADDRESS_OFFSET + 1] = (address >> 8) & 0xff;
  dma_load_routine[DESTINATION_BANK_OFFSET] = (address>>16)&0x03;
  
  // Copy data into packet
  memcpy(&dma_load_routine[DATA_OFFSET], buffer, bytes);
  
  //  printf("Sending $%07X, len = %d\n",address,bytes);

  // XXX - Assumes no packet loss, otherwise pieces will be missing from memory!
  // So for now we crudely send each packet 3x
  sendto(sockfd, dma_load_routine, sizeof dma_load_routine, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

  // Allow enough time for the MEGA65 to receive and process the packet
  // 1KB every 1.5ms = ~600KB/sec
  // XXX - If we implemented a proper protocol with responses and acknowledgments etc, we could go much faster,
  // by allowing some window of un-acked frames. But this will do for now.
  usleep(1500);
  
  return 0;
}

int main(int argc, char **argv)
{

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

  memset(colour_ram,0x01,1000);
  send_mem(0xffd8000,colour_ram,1000);

  memset(progress_screen,0x20,1000);
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
    
    send_mem(0x0400,progress_screen,1000);
    
    send_mem(address,buffer,bytes);
    
    address += bytes;
  }

  memset(progress_screen,0x20,1000);
  snprintf(msg,40,"Loaded $%04X - $%04X",start_addr,address);
  progress_line(0,15,40);
  progress_print(0,16,msg);
  progress_line(0,17,40);
  send_mem(0x0400,progress_screen,1000);
  
  // print out debug info
  printf("Sent %s to %s on port %d.\n\n", argv[2], inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port));

  printf("Now tell MEGA65 that we are all done.\n");
  

  all_done_routine[JMP_OFFSET+1]=0x0d;
  all_done_routine[JMP_OFFSET+2]=0x08;
  sendto(sockfd, all_done_routine, sizeof all_done_routine, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  
  return 0;
}
