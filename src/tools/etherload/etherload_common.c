#ifdef __linux__
#define _GNU_SOURCE
#endif
#include "etherload_common.h"
#include "ethlet_set_ip_address_map.h"
#include "ethlet_dma_load_map.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#ifdef WINDOWS
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#endif // WINDOWS

#include <logging.h>

#ifdef WINDOWS
typedef SOCKET SOCKETTYPE;
#else
typedef int SOCKETTYPE;
#endif

#ifdef WINDOWS
static int wsa_init_done = 0;
#endif
static SOCKETTYPE sockfd;
static struct sockaddr_in6 servaddr;
static struct sockaddr_in6 broadcast_addr;
#define MAX_INTERFACES 256
static struct sockaddr_in6 if_addrs[MAX_INTERFACES];


#define PORTNUM 4510
#define MAX_UNACKED_FRAMES 256
static int frame_unacked[MAX_UNACKED_FRAMES] = { 0 };
static unsigned char unacked_frame_payloads[MAX_UNACKED_FRAMES][1500];
static int unacked_frame_lengths[MAX_UNACKED_FRAMES] = { 0 };
static long long unacked_sent_times[MAX_UNACKED_FRAMES] = { 0 };

static int queue_length = 4;
static int retx_interval = 1000;
static int retx_cnt = 0;
static long long ack_timeout = 4000000; // usec
static long long start_time;
static long long last_resend_time = 0;
static long long last_rx_time = -1;
static long long last_rx_intv = 0;

static int packet_seq = 0;
static int last_rx_seq = 0;

static get_packet_seq_callback_t get_packet_seq = NULL;
static match_payloads_callback_t match_payloads = NULL;
static is_duplicate_callback_t is_duplicate = NULL;
static embed_packet_seq_callback_t embed_packet_seq = NULL;
static timeout_handler_callback_t timeout_handler = NULL;

extern unsigned char ethlet_set_ip_address[];
extern int ethlet_set_ip_address_len;
extern unsigned char ethlet_dma_load[];
extern int ethlet_dma_load_len;

static int dma_load_rom_write_enable = 0;

unsigned char hyperrupt_trigger[128];
unsigned char magic_string[12] = {
  0x65, 0x47, 0x53,       // 65 G S
  0x4b, 0x45, 0x59,       // KEY
  0x43, 0x4f, 0x44, 0x45, // CODE
  0x00, 0x80              // Magic key code $8000 = ethernet hypervisor trap
};

int ethl_configure_ip_address_and_interface(const char *ip_address, const char *ifname);
int close_socket(SOCKETTYPE fd);
int enumerate_interfaces(void);

int set_socket_non_blocking(int fd)
{
#ifdef WINDOWS
  u_long non_blocking = 1;
  if (ioctlsocket(fd, FIONBIO, &non_blocking) != NO_ERROR) {
    log_crit("unable to set non-blocking socket operation");
    return -1;
  }
#else
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, NULL) | O_NONBLOCK);
#endif
  return 0;
}

int etherload_init(const char *target_ip_address, const char *ifname)
{
#ifdef WINDOWS
  if (!wsa_init_done) {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
      log_crit("unable to start-up Winsock v2.2");
      return -1;
    }
    wsa_init_done = 1;
  }
#endif

  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin6_family = AF_INET6;
  servaddr.sin6_port = htons(PORTNUM);

  if (target_ip_address == NULL) {
    if (probe_mega65_ipv6_address(2500) < 0) {
      log_crit("Unable to find a mega65 on the local network");
      return -1;
    }
  }
  else {
    if (ethl_configure_ip_address_and_interface(target_ip_address, ifname) < 0) {
      log_crit("Unable to configure IP address and interface");
      return -1;
    }
  }

  sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
  setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_IF, (void *)&servaddr.sin6_scope_id, sizeof(servaddr.sin6_scope_id));
  set_socket_non_blocking(sockfd);

  memset(&broadcast_addr, 0, sizeof(broadcast_addr));
  broadcast_addr.sin6_family = AF_INET6;
  broadcast_addr.sin6_port = htons(PORTNUM);
  inet_pton(AF_INET6, "ff02::1", &broadcast_addr.sin6_addr);
  broadcast_addr.sin6_scope_id = servaddr.sin6_scope_id;

  return 0;
}

void etherload_finish(void)
{
#ifdef WINDOWS
  if (wsa_init_done) {
    WSACleanup();
  }
#endif
}

void ethl_setup_callbacks(get_packet_seq_callback_t c1, match_payloads_callback_t c2, is_duplicate_callback_t c3,
    embed_packet_seq_callback_t c4, timeout_handler_callback_t c5)
{
  get_packet_seq = c1;
  match_payloads = c2;
  is_duplicate = c3;
  embed_packet_seq = c4;
  timeout_handler = c5;
}

// From os.c in serval-dna
static long long gettime_us(void)
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

int parse_ipv6_and_interface(const char *in_ip_address_and_interface, char *out_ip_address, char *out_network_interface)
{
  char *temp = strdup(in_ip_address_and_interface);
  char *ip_address = strtok(temp, "%");
  char *network_interface = strtok(NULL, "%");
  if (ip_address == NULL || network_interface == NULL) {
    log_error("Invalid IP address and interface format: %s", in_ip_address_and_interface);
    free(temp);
    return -1;
  }
  strcpy(out_ip_address, ip_address);
  strcpy(out_network_interface, network_interface);
  free(temp);
  return 0;
}

int probe_mega65_ipv6_address(int timeout_ms)
{
  long long start, now;
  int return_code = -1;

#ifdef WINDOWS
  if (!wsa_init_done) {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
      log_crit("unable to start-up Winsock v2.2");
      return -1;
    }
    wsa_init_done = 1;
  }
#endif

  int num_if = enumerate_interfaces();
  if (num_if == 0) {
    log_crit("Discovery of MEGA65 needs to have IPv6 enabled on the interface connected with the MEGA65 network.");
    return -1;
  }
  SOCKETTYPE discoverfd[MAX_INTERFACES];
  memset(discoverfd, 0, sizeof(discoverfd));

  for (int idx = 0; idx < num_if; ++idx) {
    // creating an IPv6 UDP server socket and listen on port 4510 for incoming packets
    discoverfd[idx] = socket(AF_INET6, SOCK_DGRAM, 0);
    if (discoverfd < 0) {
      log_crit("Unable to create socket");
      goto leave_probe_mega65;
    }
    set_socket_non_blocking(discoverfd[idx]);
    int enable = 1;
    if (setsockopt(discoverfd[idx], IPPROTO_IPV6, IPV6_V6ONLY, (void *)&enable, sizeof(int)) < 0) {
      log_crit("Unable to set IPV6_V6ONLY");
      goto leave_probe_mega65;
    }
    if (setsockopt(discoverfd[idx], SOL_SOCKET, SO_REUSEADDR, (void *)&enable, sizeof(int)) < 0) {
      log_crit("setsockopt(SO_REUSEADDR) failed");
      goto leave_probe_mega65;
    }
#ifndef WINDOWS
    if (setsockopt(discoverfd[idx], SOL_SOCKET, SO_REUSEPORT, (void *)&enable, sizeof(int)) < 0) {
      log_crit("setsockopt(SO_REUSEPORT) failed");
      goto leave_probe_mega65;
    }
#endif
    struct ipv6_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.ipv6mr_interface = if_addrs[idx].sin6_scope_id;
    inet_pton(AF_INET6, "ff02::1", &mreq.ipv6mr_multiaddr);
    if (setsockopt(discoverfd[idx], IPPROTO_IPV6, IPV6_JOIN_GROUP, (char*)&mreq, sizeof(mreq)) < 0) {
        log_crit("setsockopt(IPV6_JOIN_GROUP) failed");
        goto leave_probe_mega65;
    }
    struct sockaddr_in6 listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin6_family = AF_INET6;
    listen_addr.sin6_port = htons(PORTNUM);
    listen_addr.sin6_addr = in6addr_any;
    if (bind(discoverfd[idx], (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
      log_crit("Unable to bind socket");
      goto leave_probe_mega65;
    }
  }

  // Buffer to hold received data
  char buffer[8192];

  // sockaddr_in6 struct to hold the source address
  struct sockaddr_in6 src_addr;
  socklen_t addr_len = sizeof(src_addr);

  start = gettime_us();
  do {

    for (int idx = 0; idx < num_if; ++idx) {
      // Receive the packet and obtain the source address
      ssize_t num_bytes = recvfrom(discoverfd[idx], buffer, sizeof(buffer), 0,
                                  (struct sockaddr *)&src_addr, &addr_len);
#ifdef WINDOWS
      if (num_bytes == SOCKET_ERROR) {
        int error_code = WSAGetLastError();
        if (error_code == WSAEWOULDBLOCK) {
          // No data available yet
          usleep(1000);
          now = gettime_us();
          continue;
        } else {
          log_crit("Error receiving packet: %d\n", WSAGetLastError());
          goto leave_probe_mega65;
        }
      }
#else
      if (num_bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          // No data available yet
          usleep(1000);
          now = gettime_us();
          continue;
        }
        else {
          log_crit("Error receiving packet, error code: %d", errno);
          goto leave_probe_mega65;
        }
      }
#endif
      if (num_bytes == 6 && memcmp(buffer, "mega65", 6) == 0) {
        log_debug("Found mega65 discover packet");

        // set etherload server address and interface
        memcpy(&servaddr.sin6_addr, &src_addr.sin6_addr, sizeof(servaddr.sin6_addr));
        servaddr.sin6_scope_id = src_addr.sin6_scope_id;
        return_code = 0;
        goto leave_probe_mega65;
      }
    
    } // for (int idx = 0; idx < num_if; ++idx)

    usleep(1000);
    now = gettime_us();

  } while (now - start < timeout_ms * 1000);

leave_probe_mega65:
  for (int idx = 0; idx < num_if; ++idx) {
    if (discoverfd[idx] == 0) {
      break;
    }
    close_socket(discoverfd[idx]);
  }
  return return_code;
}

int ethl_configure_ip_address_and_interface(const char *ip_address, const char *ifname)
{
  int result;

  result = inet_pton(AF_INET6, ip_address, &servaddr.sin6_addr);
  if (result <= 0) {
    log_error("Invalid IP address format: %s", ip_address);
    return -1;
  }

  int ifindex = if_nametoindex(ifname);
  if (ifindex == 0) {
    log_error("Unable to find interface %s", ifname);
    return -1;
  }
  servaddr.sin6_scope_id = ifindex;

  return 0;
}

int trigger_eth_hyperrupt()
{
  int offset = 0x24;
  memcpy(&hyperrupt_trigger[offset], magic_string, 12);
  sendto(sockfd, (void *)hyperrupt_trigger, sizeof(hyperrupt_trigger), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
  usleep(10000);
  start_time = gettime_us();
  last_resend_time = gettime_us();
  return 0;
}

char *ethl_get_ip_address(void)
{
  static char ip_address[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &(servaddr.sin6_addr), ip_address, INET6_ADDRSTRLEN);
  return ip_address;
}

char *ethl_get_interface_name(void)
{
  static char ifname[IF_NAMESIZE];
  if_indextoname(servaddr.sin6_scope_id, ifname);
  return ifname;
}

uint16_t ethl_get_port(void)
{
  return ntohs(servaddr.sin6_port);
}

int ethl_get_socket(void)
{
  return sockfd;
}

struct sockaddr_in6 *ethl_get_server_addr(void)
{
  return &servaddr;
}

int get_num_unacked_frames(void)
{
  int num_unacked = 0;
  for (int i = 0; i < queue_length; i++) {
    if (frame_unacked[i])
      ++num_unacked;
  }
  return num_unacked;
}

void update_retx_interval(void)
{
  // int num_unacked = get_num_unacked_frames();
  // int seq_gap = (packet_seq - last_rx_seq);
  retx_interval = 2000 + retx_cnt * last_rx_intv;
  if (retx_interval < 50000)
    retx_interval = 50000;
  if (retx_interval > 1000000)
    retx_interval = 1000000;
  // printf("  retx interval=%dusec (cnt=%d last_rx_intv=%lld)\n", retx_interval, retx_cnt, last_rx_intv);
}

int check_if_ack(uint8_t *rx_payload, int len)
{
  // Set retry interval based on number of outstanding packets
  log_debug("Received packet, checking seq_num");
  int seq_num = (*get_packet_seq)(rx_payload, len);
  if (seq_num < 0) {
    return 0;
  }
  log_debug("Received packet, seq_num=%d", seq_num);
  last_rx_seq = seq_num;
  long long now = gettime_us();
  last_rx_intv = (last_rx_time == -1) ? 0 : now - last_rx_time;
  last_rx_time = now;
  retx_cnt = 0;
  update_retx_interval();

  for (int i = 0; i < queue_length; i++) {
    if (frame_unacked[i]) {
      if ((*match_payloads)(rx_payload, len, unacked_frame_payloads[i], unacked_frame_lengths[i])) {
        log_debug("Found match in slot #%d, freeing...", i);
        frame_unacked[i] = 0;
        last_resend_time = gettime_us();
        return 1;
      }
    }
  }
  log_debug("No match for packet");
  return 0;
}

void maybe_send_ack(void);

int expect_ack(uint8_t *payload, int len, int timeout_ms)
{
  long long start = gettime_us();
  while (1) {
    int duplicate = -1;
    int free_slot = -1;
    for (int i = 0; i < queue_length; i++) {
      if (frame_unacked[i]) {
        if (is_duplicate(payload, len, unacked_frame_payloads[i], unacked_frame_lengths[i])) {
          duplicate = i;
          break;
        }
      }
      if ((!frame_unacked[i]) && (free_slot == -1))
        free_slot = i;
    }
    if ((free_slot != -1) && (duplicate == -1)) {
      // We have a free slot to put this frame, and it doesn't
      // duplicate the request of another frame.
      // Thus we can safely just note this one
      embed_packet_seq(payload, len, packet_seq);
      packet_seq++;
      update_retx_interval();
      memcpy(unacked_frame_payloads[free_slot], payload, len);
      unacked_frame_lengths[free_slot] = len;
      unacked_sent_times[free_slot] = gettime_us();
      frame_unacked[free_slot] = 1;
      last_resend_time = gettime_us();
      return 0;
    }
    // We don't have a free slot, or we have an outstanding
    // frame with the same request that we need to see an ack
    // for first.

    // Check for the arrival of any acks
    unsigned char ackbuf[8192];
    // int count = 0;
    int r = 0;
    struct sockaddr_in6 src_address;
    socklen_t addr_len = sizeof(src_address);

    while (r > -1 /*&& count++ < 100*/) {
      r = recvfrom(sockfd, (void *)ackbuf, sizeof(ackbuf), 0, (struct sockaddr *)&src_address, &addr_len);
      if (r > -1) {
        if (memcmp(&(src_address.sin6_addr), &(servaddr.sin6_addr), 16) != 0 || src_address.sin6_port != htons(PORTNUM)) {
          char str[INET6_ADDRSTRLEN];
          inet_ntop(AF_INET6, &(src_address.sin6_addr), str, INET6_ADDRSTRLEN);
          log_debug("Dropping unexpected packet from %s:%d", str, ntohs(src_address.sin6_port));
          continue;
        }
        if (r > 0)
          check_if_ack(ackbuf, r);
      }
    }
    // And re-send the first unacked frame from our list
    // (if there are still any unacked frames)
    maybe_send_ack();

    // Finally wait a short period of time
    usleep(20);
    // XXX DEBUG slow things down
    //    usleep(10000);

    if (gettime_us() - start > timeout_ms * 1000) {
      log_debug("Timeout waiting for new ack slot");
      return -1;
    }
  }
  return 0;
}

void maybe_send_ack(void)
{
  int i = 0;
  int id = 0;
  long long oldest_sent_time = -1;
  for (i = 0; i < queue_length; i++) {
    if (frame_unacked[i]) {
      if (oldest_sent_time == -1 || unacked_sent_times[i] < oldest_sent_time) {
        oldest_sent_time = unacked_sent_times[i];
        id = i;
      }
    }
  }

  if (oldest_sent_time != -1) {
    long long now = gettime_us();
    if ((now - last_resend_time) > retx_interval) {
      log_debug("now %lld last %lld diff %lld intv %d", now, last_resend_time, now - last_resend_time, retx_interval);
      //      if (retx_interval<500000) retx_interval*=2;

      /*
            if (0)
              log_debug("T+%lld : Resending addr=$%lx @ %d (%d unacked), seq=$%04x, data=%02x %02x", gettime_us() -
         start_time, frame_load_addrs[id], id, ucount, packet_seq, unacked_frame_payloads[id][ethlet_dma_load_offset_data +
         0], unacked_frame_payloads[id][ethlet_dma_load_offset_data + 1]);

            long ack_addr = (unacked_frame_payloads[id][ethlet_dma_load_offset_dest_mb] << 20)
                          + ((unacked_frame_payloads[id][ethlet_dma_load_offset_dest_bank] & 0xf) << 16)
                          + (unacked_frame_payloads[id][ethlet_dma_load_offset_dest_address + 1] << 8)
                          + (unacked_frame_payloads[id][ethlet_dma_load_offset_dest_address + 0] << 0);

            if (ack_addr != frame_load_addrs[id]) {
              log_crit("Resending frame with incorrect load address: expected=$%lx, saw=$%lx", frame_load_addrs[id],
         ack_addr); exit(-1);
            }
      */

      if (timeout_handler && (now - oldest_sent_time > ack_timeout)) {
        if (last_rx_time != -1 && (now - last_rx_time > ack_timeout)) {
          timeout_handler();
        }
      }

      log_debug("Resending packet seq #%d", get_packet_seq(unacked_frame_payloads[id], unacked_frame_lengths[id]));
      ++retx_cnt;
      update_retx_interval();
      sendto(sockfd, (void *)unacked_frame_payloads[id], unacked_frame_lengths[id], 0, (struct sockaddr *)&servaddr,
          sizeof(servaddr));
      last_resend_time = gettime_us();
      log_debug("Pending acks:");
      for (int i = 0; i < queue_length; i++) {
        if (frame_unacked[i])
          log_debug("  Frame ID #%d : seq_num=%d", i, get_packet_seq(unacked_frame_payloads[i], unacked_frame_lengths[i]));
      }
    }
    return;
  }
  if (oldest_sent_time == -1) {
    log_debug("No unacked frames");
    return;
  }
}

int wait_ack_slots_available(int num_free_slots_needed, int timeout_ms)
{
  long long start = gettime_us();
  while (gettime_us() - start < timeout_ms * 1000) {
    int num_free_slots = queue_length - get_num_unacked_frames();
    if (num_free_slots >= num_free_slots_needed)
      return 0;

    // Check for the arrival of any acks
    unsigned char ackbuf[8192];
    // int count = 0;
    int r = 0;
    struct sockaddr_in6 src_address;
    socklen_t addr_len = sizeof(src_address);

    while (r > -1 /*&& count++ < 100*/) {
      r = recvfrom(sockfd, (void *)ackbuf, sizeof(ackbuf), 0, (struct sockaddr *)&src_address, &addr_len);
      if (r > -1) {
        if (memcmp(&(src_address.sin6_addr), &(servaddr.sin6_addr), 16) != 0 || src_address.sin6_port != htons(PORTNUM)) {
          char str[INET6_ADDRSTRLEN];
          inet_ntop(AF_INET6, &(src_address.sin6_addr), str, INET6_ADDRSTRLEN);
          log_debug("Dropping unexpected packet from %s:%d", str, ntohs(src_address.sin6_port));
          continue;
        }
        if (r > 0)
          check_if_ack(ackbuf, r);
      }
    }

    maybe_send_ack();

    // Finally wait a short period of time
    usleep(20);
  }
  return -1;
}

int wait_all_acks(int timeout_ms)
{
  return wait_ack_slots_available(queue_length, timeout_ms);
}

int send_ethlet(const uint8_t data[], const int bytes)
{
  return sendto(sockfd, (char *)data, bytes, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
}

long dmaload_parse_load_addr(uint8_t *payload)
{
  return (payload[ethlet_dma_load_offset_dest_mb] << 20) + ((payload[ethlet_dma_load_offset_dest_bank] & 0xf) << 16)
       + (payload[ethlet_dma_load_offset_dest_address + 1] << 8) + (payload[ethlet_dma_load_offset_dest_address + 0] << 0);
}

int dmaload_get_packet_seq(uint8_t *payload, int len)
{
  if (len != 1280) {
    return -1;
  }

  int seq_num = payload[ethlet_dma_load_offset_seq_num] + (payload[ethlet_dma_load_offset_seq_num + 1] << 8);

  long ack_addr = dmaload_parse_load_addr(payload);
  log_debug("  Frame addr=$%lx, seq=%d", ack_addr, seq_num);

  return seq_num;
}

int dmaload_match_payloads(uint8_t *rx_payload, int rx_len, uint8_t *tx_payload, int tx_len)
{
  if (rx_len != tx_len) {
    return 0;
  }

  int load_addr = dmaload_parse_load_addr(tx_payload);

// #define CHECK_ADDR_ONLY
#ifdef CHECK_ADDR_ONLY
  int ack_addr = dmaload_parse_load_addr(rx_payload);
  if (ack_addr == load_addr) {
    log_debug("ACK addr=$%lx", load_addr);
    return 1;
  }
#else
  if (!memcmp(rx_payload, tx_payload, rx_len)) {
    log_debug("ACK addr=$%lx", load_addr);
    return 1;
  }
#endif
  return 0;
}

int dmaload_is_duplicate(uint8_t *payload, int len, uint8_t *cmp_payload, int cmp_len)
{
  if (len != 1280 || cmp_len != 1280) {
    return 0;
  }

  return (dmaload_parse_load_addr(payload) == dmaload_parse_load_addr(cmp_payload)) ? 1 : 0;
}

int dmaload_embed_packet_seq(uint8_t *payload, int len, int seq_num)
{
  if (len != 1280) {
    return 0;
  }
  payload[ethlet_dma_load_offset_seq_num] = seq_num;
  payload[ethlet_dma_load_offset_seq_num + 1] = seq_num >> 8;
  return 1;
}

int ethl_send_packet(uint8_t *payload, int len, int timeout_ms)
{
  int ret = 0;
  if (expect_ack(payload, len, timeout_ms) < 0) {
    log_debug("Timeout waiting for new ack slot");
    return -1;
  }
  do {
    ret = sendto(sockfd, (char *)payload, len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  } while (ret < 0 && errno == EAGAIN);
  return 0;
}

int ethl_send_packet_unscheduled(uint8_t *payload, int len)
{
  sendto(sockfd, (char *)payload, len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  return 0;
}

int ethl_schedule_ack(uint8_t *payload, int len, int timeout_ms)
{
  if (expect_ack(payload, len, timeout_ms) < 0) {
    log_debug("Timeout waiting for new ack slot");
    return -1;
  }
  return 0;
}

int ethl_set_queue_length(uint16_t length)
{
  if (length > MAX_UNACKED_FRAMES) {
    log_error("Requested etherload queue length %d exceeds maximum capacity %d\n", length, MAX_UNACKED_FRAMES);
    return 1;
  }
  if (queue_length != length && get_num_unacked_frames() != 0) {
    log_crit("Queue length set while there were still unacked frames\n");
  }
  queue_length = length;
  return 0;
}

int ethl_get_current_seq_num()
{
  return packet_seq;
}

void set_send_mem_rom_write_enable()
{
  dma_load_rom_write_enable = 1;
}

int send_mem(unsigned int address, unsigned char *buffer, int bytes, int timeout_ms)
{
  static int rom_write_enabled = 0;

  const int dmaload_len = 1280;
  uint8_t *payload = (uint8_t *)ethlet_dma_load;

  if (!rom_write_enabled && dma_load_rom_write_enable) {
    rom_write_enabled = 1;
    payload[ethlet_dma_load_offset_rom_write_enable + 1] = 1;
  }
  else {
    payload[ethlet_dma_load_offset_rom_write_enable + 1] = 0;
  }

  // Set load address of packet
  payload[ethlet_dma_load_offset_dest_address] = address & 0xff;
  payload[ethlet_dma_load_offset_dest_address + 1] = (address >> 8) & 0xff;
  payload[ethlet_dma_load_offset_dest_bank] = (address >> 16) & 0x0f;
  payload[ethlet_dma_load_offset_dest_mb] = (address >> 20);
  payload[ethlet_dma_load_offset_byte_count] = bytes;
  payload[ethlet_dma_load_offset_byte_count + 1] = bytes >> 8;

  // Copy data into packet
  memcpy(&payload[ethlet_dma_load_offset_data], buffer, bytes);

  // Send the packet initially
  if (0)
    log_info("T+%lld : TX addr=$%x, seq=$%04x, data=%02x %02x ...", gettime_us() - start_time, address, packet_seq,
        payload[ethlet_dma_load_offset_data], payload[ethlet_dma_load_offset_data + 1]);
  if (ethl_send_packet(payload, dmaload_len, timeout_ms) < 0) {
    log_error("Unable to send new packet");
    return -1;
  }
  return 0;
}

void ethl_setup_dmaload(void)
{
  get_packet_seq = dmaload_get_packet_seq;
  match_payloads = dmaload_match_payloads;
  is_duplicate = dmaload_is_duplicate;
  embed_packet_seq = dmaload_embed_packet_seq;
}

int dmaload_no_pending_ack(int addr)
{
  for (int i = 0; i < queue_length; i++) {
    if (frame_unacked[i]) {
      if (dmaload_parse_load_addr(unacked_frame_payloads[i]) == addr)
        return 0;
    }
  }
  return 1;
}

int close_socket(SOCKETTYPE sockfd)
{
#ifdef WINDOWS
  return closesocket(sockfd);
#else
  return close(sockfd);
#endif
}

/**
 * @brief Enumerate all IPv6 interfaces on the system
 * 
 * Fills the if_addrs array with the IPv6 addresses of all active interfaces.
 * 
 * @return int The number of interfaces found
 */
int enumerate_interfaces()
{
  memset(if_addrs, 0, sizeof(if_addrs));
  int cur_if = 0;

#ifdef WINDOWS
  PIP_ADAPTER_ADDRESSES addresses = NULL;
  ULONG outBufLen = 0;
  GetAdaptersAddresses(AF_INET6, 0, NULL, addresses, &outBufLen);
  addresses = (IP_ADAPTER_ADDRESSES *)malloc(outBufLen);

  if (GetAdaptersAddresses(AF_INET6, 0, NULL, addresses, &outBufLen) == NO_ERROR) {
    PIP_ADAPTER_ADDRESSES currAdapterAddresses = addresses;
    while (currAdapterAddresses) {
      if (currAdapterAddresses->OperStatus != IfOperStatusUp) {
        log_debug("Skipping interface (down): %s (%ls)", currAdapterAddresses->AdapterName, currAdapterAddresses->FriendlyName);
        currAdapterAddresses = currAdapterAddresses->Next;
        continue;
      }
      log_debug("\nFound active adapter: %s (%ls)\n", currAdapterAddresses->AdapterName, currAdapterAddresses->FriendlyName);
      PIP_ADAPTER_UNICAST_ADDRESS currUnicastAddress = currAdapterAddresses->FirstUnicastAddress;
      while (currUnicastAddress) {
        struct sockaddr_in6 *sockAddr6 = (struct sockaddr_in6 *)currUnicastAddress->Address.lpSockaddr;
        if (sockAddr6->sin6_family == AF_INET6 && IN6_IS_ADDR_LINKLOCAL(&sockAddr6->sin6_addr)) {
          char strBuffer[INET6_ADDRSTRLEN] = {0};
          if (InetNtop(AF_INET6, &sockAddr6->sin6_addr, strBuffer, INET6_ADDRSTRLEN) != NULL) {
            log_debug("  IPv6 Address: %s\n", strBuffer);
            char ifname[MAX_INTERFACE_NAME_LEN];
            if_indextoname(sockAddr6->sin6_scope_id, ifname);
            log_debug("  Interface: %s (id %d)\n", ifname, sockAddr6->sin6_scope_id);
            if (cur_if < MAX_INTERFACES) {
              memcpy(&if_addrs[cur_if], sockAddr6, sizeof(struct sockaddr_in6));
              cur_if++;
            }
          }
        }
        currUnicastAddress = currUnicastAddress->Next;
      }
      currAdapterAddresses = currAdapterAddresses->Next;
    }
  }

  free(addresses);
#else
  // Linux / macOS implementation:
  struct ifaddrs *ifaddr, *ifa;

  if (getifaddrs(&ifaddr) == -1) {
    perror("getifaddrs");
    exit(EXIT_FAILURE);
  }

  for (ifa = ifaddr; ifa != NULL && cur_if < MAX_INTERFACES; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET6) {
        continue;
    }

    if (ifa->ifa_addr->sa_family == AF_INET6 && (ifa->ifa_flags & IFF_UP)) {
      struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)ifa->ifa_addr;
      // Ensure it's a link-local address
      if (IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr)) {
        memcpy(&if_addrs[cur_if], addr6, sizeof(struct sockaddr_in6));
        ++cur_if;
      }
    }
  }

  freeifaddrs(ifaddr);

#endif
  return cur_if;
}
