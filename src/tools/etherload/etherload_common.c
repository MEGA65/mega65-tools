#include "etherload_common.h"
#include "ethlet_dma_load_map.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>

#include <logging.h>

static int sockfd;
static struct sockaddr_in servaddr;

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

extern char ethlet_dma_load[];
extern int ethlet_dma_load_len;

static int dma_load_rom_write_enable = 0;

unsigned char hyperrupt_trigger[128];
unsigned char magic_string[12] = {
  0x65, 0x47, 0x53,       // 65 G S
  0x4b, 0x45, 0x59,       // KEY
  0x43, 0x4f, 0x44, 0x45, // CODE
  0x00, 0x80              // Magic key code $8000 = ethernet hypervisor trap
};

int etherload_init(const char *broadcast_address)
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

  if (servaddr.sin_addr.s_addr == INADDR_NONE) {
    log_crit("Invalid IP address provided: %s", broadcast_address);
    return -1;
  }

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
  // servaddr.sin_addr.s_addr |= (6 << 24);

  return 0;
}

char *ethl_get_ip_address(void)
{
  return inet_ntoa(servaddr.sin_addr);
}

uint16_t ethl_get_port(void)
{
  return ntohs(servaddr.sin_port);
}

int ethl_get_socket(void)
{
  return sockfd;
}

struct sockaddr_in *ethl_get_server_addr(void)
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
  if (retx_interval < 10000)
    retx_interval = 10000;
  if (retx_interval > 500000)
    retx_interval = 500000;
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

int expect_ack(uint8_t *payload, int len)
{
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
    struct sockaddr_in src_address;
    socklen_t addr_len = sizeof(src_address);

    while (r > -1 /*&& count++ < 100*/) {
      r = recvfrom(sockfd, (void *)ackbuf, sizeof(ackbuf), 0, (struct sockaddr *)&src_address, &addr_len);
      if (r > -1) {
        if (src_address.sin_addr.s_addr != servaddr.sin_addr.s_addr || src_address.sin_port != htons(PORTNUM)) {
          log_debug("Dropping unexpected packet from %s:%d", inet_ntoa(src_address.sin_addr), ntohs(src_address.sin_port));
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

int wait_ack_slots_available(int num_free_slots_needed)
{
  while (1) {
    int num_free_slots = queue_length - get_num_unacked_frames();
    if (num_free_slots >= num_free_slots_needed)
      return 0;

    // Check for the arrival of any acks
    unsigned char ackbuf[8192];
    // int count = 0;
    int r = 0;
    struct sockaddr_in src_address;
    socklen_t addr_len = sizeof(src_address);

    while (r > -1 /*&& count++ < 100*/) {
      r = recvfrom(sockfd, (void *)ackbuf, sizeof(ackbuf), 0, (struct sockaddr *)&src_address, &addr_len);
      if (r > -1) {
        if (src_address.sin_addr.s_addr != servaddr.sin_addr.s_addr || src_address.sin_port != htons(PORTNUM)) {
          log_debug("Dropping unexpected packet from %s:%d", inet_ntoa(src_address.sin_addr), ntohs(src_address.sin_port));
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
  return 0;
}

int wait_all_acks(void)
{
  return wait_ack_slots_available(queue_length);
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

int ethl_send_packet(uint8_t *payload, int len)
{
  expect_ack(payload, len);
  sendto(sockfd, (char *)payload, len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  return 0;
}

int ethl_send_packet_unscheduled(uint8_t *payload, int len)
{
  sendto(sockfd, (char *)payload, len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  return 0;
}

int ethl_schedule_ack(uint8_t *payload, int len)
{
  expect_ack(payload, len);
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

int send_mem(unsigned int address, unsigned char *buffer, int bytes)
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

  // Set position of marker to draw in 1KB units
  payload[3] = address >> 10;

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
  ethl_send_packet(payload, dmaload_len);
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
