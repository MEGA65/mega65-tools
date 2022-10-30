
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

  0xa2, 0x00, 0xbd, 0x44, 0x68, 0x9d, 0x40, 0x03, 0xe8, 0xe0, 0x40, 0xd0, 0xf5, 0x4c, 0x40, 0x03, 0xa9, 0x47, 0x8d, 0x2f,
  0xd0, 0xa9, 0x53, 0x8d, 0x2f, 0xd0, 0xa9, 0x00, 0xa2, 0x0f, 0xa0, 0x00, 0xa3, 0x00, 0x5c, 0xea, 0xa9, 0x00, 0xa2, 0x00,
  0xa0, 0x00, 0xa3, 0x00, 0x5c, 0xea, 0x68, 0x68, 0x60
};

char dma_load_routine[128 + 1024] = {
  // Routine that copies packet contents by DMA
  0xa9,0x00, // Dummy LDA #$xx for signature detection
  0x8d, 0x07, 0xd7, // STA $D707 to trigger in-line DMA

  // SRC MB is $FF
  0x80,0xff,
#define DESTINATION_MB_OFFSET 0x08
  // Destination MB 
  0x81, 0x00,  
  // DMA end of option list
  0x00,
  
  // DMA list begins at offset $0030
  0x00, // DMA command ($0030)
#define BYTE_COUNT_OFFSET 0x0B
  0x00, 0x04,       // DMA byte count ($0031-$0032)
  0x80, 0xe8, 0x8d, // DMA source address (points to data in packet)
#define DESTINATION_ADDRESS_OFFSET 0x10
  0x00, 0x10, // DMA Destination address (bottom 16 bits)
#define DESTINATION_BANK_OFFSET 0x12
  0x00,       // DMA Destination bank
  0x00, // DMA Sub command
  0x00, 0x00, // DMA modulo (ignored)
  
  // Code resumes after DMA list here
  0x60, // RTS 
  
// Packet ID number at offset $003B
#define PACKET_NUMBER_OFFSET 0x17
  0x30,0x00,
#define DATA_OFFSET (0x80 - 0x2c)
};

int sockfd;
struct sockaddr_in servaddr;

int send_mem(unsigned int address,unsigned char *buffer,int bytes)
{
  // Set load address of packet
  dma_load_routine[DESTINATION_ADDRESS_OFFSET] = address & 0xff;
  dma_load_routine[DESTINATION_ADDRESS_OFFSET + 1] = (address >> 8) & 0xff;
  dma_load_routine[DESTINATION_BANK_OFFSET] = (address>>16)&0x03;
  
  // Copy data into packet
  memcpy(&dma_load_routine[DATA_OFFSET], buffer, bytes);
  
  printf("Sending $%07X, len = %d\n",address,bytes);
  sendto(sockfd, dma_load_routine, sizeof dma_load_routine, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

  // Allow enough time for the MEGA65 to receive and process the packet
  usleep(1500);
  
  dma_load_routine[PACKET_NUMBER_OFFSET]++;
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

  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr(argv[1]);
  servaddr.sin_port = htons(PORTNUM);

  // print out debug info
  printf("Using dst-addr: %s\n", inet_ntoa(servaddr.sin_addr));
  printf("Using src-port: %d\n", ntohs(servaddr.sin_port));

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

  int address = buffer[0] + 256 * buffer[1];
  printf("Load address of programme is $%04x\n", address);

  while ((bytes = read(fd, buffer, 1024)) != 0) {
    printf("Read %d bytes at offset %d\n", bytes, offset);
    offset += bytes;

    send_mem(address,buffer,bytes);
    
    address += bytes;
  }

  // print out debug info
  printf("Sent %s to %s on port %d.\n\n", argv[2], inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port));

  if (1) {

    printf("Now tell MEGA65 that we are all done");

    int i;
    for (i = 0; i < 10; i++) {

      printf(".");

      sendto(sockfd, all_done_routine, sizeof all_done_routine, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

      usleep(150);
    }

    printf("\n");
  }

  return 0;
}
