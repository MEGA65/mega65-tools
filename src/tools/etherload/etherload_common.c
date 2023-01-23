#include "ethlet_dma_load_map.h"

#ifdef WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
// #include <netinet/in.h>
// #include <sys/types.h>
// #include <sys/ioctl.h>
#endif

#include <stdlib.h>
#include <unistd.h>
// #include <string.h>
#include <strings.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
// #include <math.h>
// #include <ctype.h>
// #include <getopt.h>

#include <logging.h>

static int sockfd;
static struct sockaddr_in servaddr;

#define PORTNUM 4510
#define MAX_UNACKED_FRAMES 32
static int frame_unacked[MAX_UNACKED_FRAMES] = { 0 };
static long frame_load_addrs[MAX_UNACKED_FRAMES] = { -1 };
static unsigned char unacked_frame_payloads[MAX_UNACKED_FRAMES][1280];

static int retx_interval = 1000;
static long long start_time;
static long long last_resend_time = 0;
static int resend_frame = 0;

static int packet_seq = 0;
static int last_rx_seq = 0;

extern char ethlet_dma_load[];
extern int ethlet_dma_load_len;

unsigned char hyperrupt_trigger[128];
unsigned char magic_string[12] = {
  0x65, 0x47, 0x53,       // 65 G S
  0x4b, 0x45, 0x59,       // KEY
  0x43, 0x4f, 0x44, 0x45, // CODE
  0x00, 0x80              // Magic key code $8000 = ethernet hypervisor trap
};

int etherload_init(const char* broadcast_address)
{
#ifdef WINDOWS
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    log_crit("unable to start-up Winsock v2.2");
    return -1;
  }
#endif

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  int broadcast_enable = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (char *)&broadcast_enable, sizeof(broadcast_enable));

#ifdef WINDOWS
  u_long non_blocking = 1;
  if (ioctlsocket(sockfd, FIONBIO, &non_blocking) != NO_ERROR) {
    log_crit("unable to set non-blocking socket operation");
    return -1;
  }
#else
  fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, NULL) | O_NONBLOCK);
#endif

  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr(broadcast_address);
  servaddr.sin_port = htons(PORTNUM);

  log_debug("Using dst-addr: %s", inet_ntoa(servaddr.sin_addr));
  log_debug("Using src-port: %d", ntohs(servaddr.sin_port));

  return 0;
}

void etherload_finish(void)
{
#ifdef WINDOWS
  WSACleanup();
#endif
}

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

int trigger_eth_hyperrupt(void)
{
  int offset = 0x38;
  memcpy(&hyperrupt_trigger[offset], magic_string, 12);

  sendto(sockfd, (void *)hyperrupt_trigger, sizeof hyperrupt_trigger, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  usleep(10000);

  start_time = gettime_us();
  last_resend_time = gettime_us();

  // Adapt ip address (modify last byte to use ip x.y.z.65 as dest address)
  servaddr.sin_addr.s_addr &= 0x00ffffff;
  servaddr.sin_addr.s_addr |= (65 << 24);

  return 0;
}

char* ethl_get_ip_address(void)
{
  return inet_ntoa(servaddr.sin_addr);
}

uint16_t ethl_get_port(void)
{
  return ntohs(servaddr.sin_port);
}

void update_retx_interval(void)
{
  int seq_gap = (packet_seq - last_rx_seq);
  retx_interval = 2000 + 10000 * seq_gap;
  if (retx_interval < 1000)
    retx_interval = 1000;
  if (retx_interval > 500000)
    retx_interval = 500000;
  //  printf("  retx interval=%dusec (%d vs %d)\n",retx_interval,packet_seq,last_rx_seq);
}

int check_if_ack(unsigned char *b)
{
  log_debug("Pending acks:");
  for (int i = 0; i < MAX_UNACKED_FRAMES; i++) {
    if (frame_unacked[i])
      log_debug("  Frame ID #%d : addr=$%lx", i, frame_load_addrs[i]);
  }

  // Set retry interval based on number of outstanding packets
  last_rx_seq = (b[ethlet_dma_load_offset_seq_num] + (b[ethlet_dma_load_offset_seq_num + 1] << 8));
  update_retx_interval();

  long ack_addr = (b[ethlet_dma_load_offset_dest_mb] << 20) + ((b[ethlet_dma_load_offset_dest_bank] & 0xf) << 16)
                + (b[ethlet_dma_load_offset_dest_address + 1] << 8) + (b[ethlet_dma_load_offset_dest_address + 0] << 0);
  log_debug("T+%lld : RXd frame addr=$%lx, rx seq=$%04x, tx seq=$%04x", gettime_us() - start_time, ack_addr, last_rx_seq,
      packet_seq);

// #define CHECK_ADDR_ONLY
#ifdef CHECK_ADDR_ONLY
  for (int i = 0; i < MAX_UNACKED_FRAMES; i++) {
    if (frame_unacked[i]) {
      if (ack_addr == frame_load_addrs[i]) {
        frame_unacked[i] = 0;
        log_debug("ACK addr=$%lx", frame_load_addrs[i]);
        return 1;
      }
    }
  }
#else
  for (int i = 0; i < MAX_UNACKED_FRAMES; i++) {
    if (frame_unacked[i]) {
      if (!memcmp(unacked_frame_payloads[i], b, 1280)) {
        frame_unacked[i] = 0;
        log_debug("ACK addr=$%lx", frame_load_addrs[i]);
        return 1;
      }
      else {
#if 0
	for(int j=0;j<1280;j++) {
	  if (unacked_frame_payloads[i][j]!=b[j]) {
	    log_debug("Mismatch frame id #%d offset %d : $%02x vs $%02x",
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

void maybe_send_ack(void);

int expect_ack(long load_addr, char *b)
{
  while (1) {
    int addr_dup = -1;
    int free_slot = -1;
    for (int i = 0; i < MAX_UNACKED_FRAMES; i++) {
      if (frame_unacked[i]) {
        if (frame_load_addrs[i] == load_addr) {
          addr_dup = i;
          break;
        }
      }
      if ((!frame_unacked[i]) && (free_slot == -1))
        free_slot = i;
    }
    if ((free_slot != -1) && (addr_dup == -1)) {
      // We have a free slot to put this frame, and it doesn't
      // duplicate the address of another frame.
      // Thus we can safely just note this one
      log_debug("Expecting ack of addr=$%lx @ %d", load_addr, free_slot);
      memcpy(unacked_frame_payloads[free_slot], b, 1280);
      frame_unacked[free_slot] = 1;
      frame_load_addrs[free_slot] = load_addr;
      return 0;
    }
    // We don't have a free slot, or we have an outstanding
    // frame with the same address that we need to see an ack
    // for first.

    // Check for the arrival of any acks
    unsigned char ackbuf[8192];
    int count = 0;
    int r = 0;
    while (r > -1 && count < 100) {
      r = recv(sockfd, (void *)ackbuf, sizeof(ackbuf), 0);
      if (r == 1280)
        check_if_ack(ackbuf);
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
  for (int i = 0; i < MAX_UNACKED_FRAMES; i++) {
    if (frame_unacked[i]) {
      if (frame_load_addrs[i] == addr)
        return 0;
    }
  }
  return 1;
}

void maybe_send_ack(void)
{
  int i = 0;
  int unackd[MAX_UNACKED_FRAMES];
  int ucount = 0;
  for (i = 0; i < MAX_UNACKED_FRAMES; i++)
    if (frame_unacked[i])
      unackd[ucount++] = i;

  if (ucount) {
    if ((gettime_us() - last_resend_time) > retx_interval) {

      //      if (retx_interval<500000) retx_interval*=2;

      resend_frame++;
      if (resend_frame >= ucount)
        resend_frame = 0;
      int id = unackd[resend_frame];
      if (0)
        log_warn("T+%lld : Resending addr=$%lx @ %d (%d unacked), seq=$%04x, data=%02x %02x", gettime_us() - start_time,
            frame_load_addrs[id], id, ucount, packet_seq, unacked_frame_payloads[id][ethlet_dma_load_offset_data + 0],
            unacked_frame_payloads[id][ethlet_dma_load_offset_data + 1]);

      long ack_addr = (unacked_frame_payloads[id][ethlet_dma_load_offset_dest_mb] << 20)
                    + ((unacked_frame_payloads[id][ethlet_dma_load_offset_dest_bank] & 0xf) << 16)
                    + (unacked_frame_payloads[id][ethlet_dma_load_offset_dest_address + 1] << 8)
                    + (unacked_frame_payloads[id][ethlet_dma_load_offset_dest_address + 0] << 0);

      if (ack_addr != frame_load_addrs[id]) {
        log_crit("Resending frame with incorrect load address: expected=$%lx, saw=$%lx", frame_load_addrs[id], ack_addr);
        exit(-1);
      }

      unacked_frame_payloads[id][ethlet_dma_load_offset_seq_num] = packet_seq;
      unacked_frame_payloads[id][ethlet_dma_load_offset_seq_num + 1] = packet_seq >> 8;
      packet_seq++;
      update_retx_interval();
      sendto(sockfd, (void *)unacked_frame_payloads[id], 1280, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
      last_resend_time = gettime_us();
    }
    return;
  }
  if (!ucount) {
    log_debug("No unacked frames");
    return;
  }
}

int wait_all_acks(void)
{
  while (1) {
    int unacked = -1;
    for (int i = 0; i < MAX_UNACKED_FRAMES; i++) {
      if (frame_unacked[i]) {
        unacked = i;
        break;
      }
    }
    if (unacked == -1)
      return 0;

    // Check for the arrival of any acks
    unsigned char ackbuf[8192];
    int count = 0;
    int r = 0;
    struct sockaddr_in src_address;
    socklen_t addr_len = sizeof(src_address);

    while (r > -1 && count < 100) {
      r = recvfrom(sockfd, (void *)ackbuf, sizeof(ackbuf), 0, (struct sockaddr *)&src_address, &addr_len);
      if (r > -1) {
        if (src_address.sin_addr.s_addr != servaddr.sin_addr.s_addr || src_address.sin_port != htons(PORTNUM)) {
          log_debug("Dropping unexpected packet from %s:%d", inet_ntoa(src_address.sin_addr), ntohs(src_address.sin_port));
          continue;
        }
        if (r == 1280)
          check_if_ack(ackbuf);
      }
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

int send_mem(unsigned int address, unsigned char *buffer, int bytes)
{
  // Set position of marker to draw in 1KB units
  ethlet_dma_load[3] = address >> 10;

  // Set load address of packet
  ethlet_dma_load[ethlet_dma_load_offset_dest_address] = address & 0xff;
  ethlet_dma_load[ethlet_dma_load_offset_dest_address + 1] = (address >> 8) & 0xff;
  ethlet_dma_load[ethlet_dma_load_offset_dest_bank] = (address >> 16) & 0x0f;
  ethlet_dma_load[ethlet_dma_load_offset_dest_mb] = (address >> 20);
  ethlet_dma_load[ethlet_dma_load_offset_byte_count] = bytes;
  ethlet_dma_load[ethlet_dma_load_offset_byte_count + 1] = bytes >> 8;

  // Copy data into packet
  memcpy(&ethlet_dma_load[ethlet_dma_load_offset_data], buffer, bytes);

  // Add to queue of packets with pending ACKs
  expect_ack(address, ethlet_dma_load);

  // Send the packet initially
  if (0)
    log_info("T+%lld : TX addr=$%x, seq=$%04x, data=%02x %02x ...", gettime_us() - start_time, address, packet_seq,
        ethlet_dma_load[ethlet_dma_load_offset_data], ethlet_dma_load[ethlet_dma_load_offset_data + 1]);
  ethlet_dma_load[ethlet_dma_load_offset_seq_num] = packet_seq;
  ethlet_dma_load[ethlet_dma_load_offset_seq_num + 1] = packet_seq >> 8;
  packet_seq++;
  update_retx_interval();
  sendto(sockfd, ethlet_dma_load, ethlet_dma_load_len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

  return 0;
}


int send_ethlet(const char data[], const int bytes)
{
  return sendto(sockfd, data, bytes, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
}
