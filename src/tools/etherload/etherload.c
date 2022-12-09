#include "ethlet_dma_load_map.h"
#include "ethlet_all_done_basic65_map.h"
#include "ethlet_all_done_basic2_map.h"
#include "ethlet_all_done_jump_map.h"

#ifdef WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/ioctl.h>
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
#include <getopt.h>

#include <logging.h>

#define PORTNUM 4510

void maybe_send_ack(void);
long long gettime_us(void);

long long start_time;

int packet_seq = 0;
int last_rx_seq = 0;

extern unsigned char c64_loram[1024];
extern char ethlet_dma_load[];
extern int ethlet_dma_load_len;
extern char ethlet_all_done_basic2[];
extern int ethlet_all_done_basic2_len;
extern char ethlet_all_done_basic65[];
extern int ethlet_all_done_basic65_len;
extern char ethlet_all_done_jump[];
extern int ethlet_all_done_jump_len;

unsigned char colour_ram[1000];
unsigned char progress_screen[1000];

int sockfd;
struct sockaddr_in servaddr;

#define MAX_TERM_WIDTH 100 // for help only currently

#define TOOLNAME "MEGA65 Ethernet Loading Tool"
#if defined(WINDOWS)
#define PROGNAME "etherload.exe"
#elif defined(__APPLE__)
#define PROGNAME "etherload.osx"
#else
#define PROGNAME "etherload"
#endif

extern const char *version_string;

#define MAX_CMD_OPTS 50
int cmd_count = 0, cmd_log_start = -1, cmd_log_end = -1;
char *cmd_desc[MAX_CMD_OPTS];
char *cmd_arg[MAX_CMD_OPTS];
struct option cmd_opts[MAX_CMD_OPTS];
#define CMD_OPTION(Oname, Ohas, Oflag, Oval, Oarg, Odesc)                                                                   \
  cmd_opts[cmd_count].name = Oname;                                                                                         \
  cmd_opts[cmd_count].has_arg = Ohas;                                                                                       \
  cmd_opts[cmd_count].flag = Oflag;                                                                                         \
  cmd_opts[cmd_count].val = Oval;                                                                                           \
  cmd_arg[cmd_count] = Oarg;                                                                                                \
  cmd_desc[cmd_count++] = Odesc

int loglevel = LOG_NOTE;
int reset64 = 0;
int reset65 = 0;
int do_run = 0;
int cart_detect = 0;
int halt = 0;
int do_jump = 0;
int jump_addr = 0;
int use_binary = 0;
int bin_load_addr = 0;
char *ip_address = NULL;
char *filename = NULL;

int get_terminal_size(int max_width)
{
  int width = 80;
#ifndef WINDOWS
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != -1)
    width = w.ws_col;
#else
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
    width = csbi.dwSize.X;
#endif
  return max_width > 0 && width > max_width ? max_width : width;
}

char *wrap_line(const char *line, int wrap, int *offset)
{
  int pos;
  char *buffer;

  if (strlen(line) <= wrap) {
    *offset = -1;
    return strdup(line);
  }

  for (pos = wrap; line[pos] != ' '; pos--)
    ;
  buffer = malloc(pos + 1);
  if (buffer != NULL) {
    strncpy(buffer, line, pos);
    buffer[pos] = 0;
  }
  *offset = pos + 1;

  return buffer;
}

int check_file_access(char *file, char *purpose)
{
  FILE *f = fopen(file, "rb");
  if (!f) {
    log_crit("cannot access %s file '%s'", purpose, file);
    exit(-1);
  }
  fclose(f);

  return 0;
}

void usage(int exitcode, char *message)
{
  char optstr[MAX_TERM_WIDTH + 1], *argstr, *temp;
  int optlen, offset = 0, first, width = get_terminal_size(MAX_TERM_WIDTH) - 1;

  fprintf(stderr, TOOLNAME "\n");
  fprintf(stderr, "Version: %s\n\n", version_string);

  fprintf(stderr, PROGNAME ": [options] [prgname]\n");

  for (int i = 0; i < cmd_count; i++) {
    if (cmd_opts[i].val && !cmd_opts[i].flag && cmd_opts[i].val < 0x80) {
      if (cmd_opts[i].has_arg == 2)
        snprintf(optstr, width, "-%c[<%s>] | --%s", cmd_opts[i].val, cmd_arg[i], cmd_opts[i].name);
      else
        snprintf(optstr, width, "-%c|--%s", cmd_opts[i].val, cmd_opts[i].name);
    }
    else
      snprintf(optstr, width, "--%s", cmd_opts[i].name);

    optlen = strlen(optstr);
    argstr = optstr + optlen;
    if (cmd_opts[i].has_arg == 2)
      snprintf(argstr, width - optlen - 5, "[=<%s>]", cmd_arg[i]);
    else if (cmd_opts[i].has_arg == 1)
      snprintf(argstr, width - optlen - 5, " <%s>", cmd_arg[i]);

    fprintf(stderr, "  %-15s ", optstr);
    if (strlen(optstr) > 15)
      fprintf(stderr, "\n                  ");

    first = 1;
    argstr = cmd_desc[i];
    while (1) {
      temp = wrap_line(argstr, width - 20, &offset);
      if (!first)
        fprintf(stderr, "                  ");
      else
        first = 0;
      fprintf(stderr, "%s\n", temp);
      free(temp);
      if (offset == -1)
        break;
      argstr += offset;
    }
  }
  fprintf(stderr, "\n");

  if (message != NULL)
    fprintf(stderr, "%s\n", message);

  exit(exitcode);
}

void init_cmd_options(void)
{
  // clang-format off
  CMD_OPTION("help",        no_argument,       0,            'h', "",       "Display help and exit.");
  cmd_log_start = cmd_count;
  CMD_OPTION("quiet",       no_argument,       &loglevel,    1,   "",       "Only display errors or critical errors.");
  CMD_OPTION("verbose",     no_argument,       &loglevel,    4,   "",       "More verbose logging.");
  CMD_OPTION("debug",       no_argument,       &loglevel,    5,   "",       "Enable debug logging.");
  cmd_log_end = cmd_count;
  CMD_OPTION("log",         required_argument, 0,            '0', "level",  "Set log <level> to argument (0-5, critical, error, warning, notice, info, debug).");

  CMD_OPTION("ip",          required_argument, 0,            'i', "ipaddr", "Set IPv4 broadcast address of the subnet where MEGA65 is connected (eg. 192.168.1.255).");
  CMD_OPTION("run",         no_argument,       0,            'r',   "",     "Automatically RUN programme after loading.");
  CMD_OPTION("c64mode",     no_argument,       0,            '4',   "",     "Reset to C64 mode after transfer.");
  CMD_OPTION("m65mode",     no_argument,       0,            '5',   "",     "Reset to MEGA65 mode after transfer.");
  CMD_OPTION("halt",        no_argument,       &halt,        1,     "",     "Halt and wait for next transfer after completion.");
  CMD_OPTION("jump",        required_argument, 0,            'j', "addr",   "Jump to provided address <addr> after loading (hex notation).");
  CMD_OPTION("bin",         required_argument, 0,            'b', "addr",   "Treat <prgname> as binary file and load at address <addr>.");
  CMD_OPTION("cart-detect", no_argument,       &cart_detect, 1,     "",     "Enable detection of cartridge signature CBM80 at $8004 on reset.");
  // clang-format on
}

int progress_print(int x, int y, char *msg)
{
  int ofs = y * 40 + x;
  for (int i = 0; msg[i]; i++) {
    if (msg[i] >= 'A' && msg[i] <= 'Z')
      progress_screen[ofs] = msg[i] - 0x40;
    else if (msg[i] >= 'a' && msg[i] <= 'z')
      progress_screen[ofs] = msg[i] - 0x60;
    else
      progress_screen[ofs] = msg[i];
    ofs++;
    if (ofs > 999)
      ofs = 999;
  }
  return 0;
}

int progress_line(int x, int y, int len)
{
  int ofs = y * 40 + x;
  for (int i = 0; i < len; i++) {
    progress_screen[ofs] = 67;
    ofs++;
    if (ofs > 999)
      ofs = 999;
  }
  return 0;
}

unsigned char hyperrupt_trigger[128];
unsigned char magic_string[12] = {
  0x65, 0x47, 0x53,       // 65 G S
  0x4b, 0x45, 0x59,       // KEY
  0x43, 0x4f, 0x44, 0x45, // CODE
  0x00, 0x80              // Magic key code $8000 = ethernet hypervisor trap
};

int trigger_eth_hyperrupt(void)
{
  int offset = 0x38;
  memcpy(&hyperrupt_trigger[offset], magic_string, 12);

  sendto(sockfd, (void *)hyperrupt_trigger, sizeof hyperrupt_trigger, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  usleep(10000);

  return 0;
}

void dump_bytes(char *msg, unsigned char *b, int len)
{
  fprintf(stderr, "%s:\n", msg);
  for (int i = 0; i < len; i += 16) {
    fprintf(stderr, "%04x:", i);
    int max = 16;
    if ((i + max) > len)
      max = len = i;
    for (int j = 0; j < max; j++) {
      fprintf(stderr, " %02x", b[i + j]);
    }
    for (int j = max; j < 16; j++)
      fprintf(stderr, "   ");
    fprintf(stderr, "  ");
    for (int j = 0; j < max; j++) {
      if (b[i + j] >= 0x20 && b[i + j] < 0x7f)
        fprintf(stderr, "%c", b[i + j]);
      else
        fprintf(stderr, "?");
    }
    fprintf(stderr, "\n");
  }
  return;
}

#define MAX_UNACKED_FRAMES 32
int frame_unacked[MAX_UNACKED_FRAMES] = { 0 };
long frame_load_addrs[MAX_UNACKED_FRAMES] = { -1 };
unsigned char unacked_frame_payloads[MAX_UNACKED_FRAMES][1280];

int retx_interval = 1000;

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

long long last_resend_time = 0;

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

int resend_frame = 0;

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

int main(int argc, char **argv)
{
  int opt_index;
  start_time = gettime_us();

  init_cmd_options();

  // so we can see errors while parsing args
  log_setup(stderr, LOG_NOTE);

  if (argc == 1)
    usage(-3, "No arguments given!");

  int opt;
  while ((opt = getopt_long(argc, argv, "i:r45hj:b:0:", cmd_opts, &opt_index)) != -1) {
    if (opt == 0) {
      if (opt_index >= cmd_log_start && opt_index < cmd_log_end)
        log_setup(stderr, loglevel);
      continue;
    }
    // fprintf(stderr, "got %02x %p %d\n", opt, optarg, opt_index);
    switch (opt) {
    case '0':
      loglevel = log_parse_level(optarg);
      if (loglevel == -1)
        log_warn("failed to parse log level!");
      else
        log_setup(stderr, loglevel);
      break;
    case 'h':
      usage(0, NULL);
    case 'i':
      ip_address = strdup(optarg);
      break;
    case 'j':
      do_jump = 1;
      if (sscanf(optarg, "%x", &jump_addr) != 1) {
        log_crit("-j option needs hex addr");
        exit(-1);
      }
      break;
    case 'b':
      use_binary = 1;
      if (sscanf(optarg, "%x", &bin_load_addr) != 1) {
        log_crit("-b option needs hex addr");
        exit(-1);
      }
      break;
    case '4':
      reset64 = 1;
      break;
    case '5':
      reset65 = 1;
      break;
    case 'r':
      do_run = 1;
      break;
    default: // can not happen?
      usage(-3, "Unknown option.");
    }
  }

  if (!argv[optind]) {
    usage(-3, "Filename for upload not specified, aborting.");
  }

  filename = strdup(argv[optind]);
  check_file_access(filename, "programme");

  if (argc - optind > 1)
    usage(-3, "Unexpected extra commandline arguments.");

  log_debug("parameter parsing done");

  log_note("%s %s", TOOLNAME, version_string);

  // check if init_fpgajtag did totally fail
  if (ip_address == NULL) {
    log_crit("broadcast ip address not specified, aborting.");
    exit(1);
  }

  // check reset options
  if (halt) {
    if (do_run) {
      log_crit("options --halt and --run can't be specified together, aborting.");
      exit(1);
    }
    if (do_jump) {
      log_crit("options --halt and --jump can't be specified together, aborting.");
      exit(1);
    }
    if (reset64 || reset65) {
      log_crit("option --halt can't be specified together with reset options, aborting.");
      exit(1);
    }
  }
  else if (do_jump) {
    if (do_run) {
      log_crit("options --jump and --run can't be specified together, aborting.");
      exit(1);
    }
    if (reset64 || reset65) {
      log_crit("option --jump can't be specified together with reset options, aborting.");
      exit(1);
    }
  }
  else {
    if (reset64 && reset65) {
      log_crit("both reset options --c64mode and --m65mode specified, aborting.");
      exit(1);
    }
  }

#ifdef WINDOWS
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    log_crit("unable to start-up Winsock v2.2");
    exit(-1);
  }
#endif

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  int broadcast_enable = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (char *)&broadcast_enable, sizeof(broadcast_enable));

#ifdef WINDOWS
  u_long non_blocking = 1;
  if (ioctlsocket(sockfd, FIONBIO, &non_blocking) != NO_ERROR) {
    log_crit("unable to set non-blocking socket operation");
    exit(-1);
  }
#else
  fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, NULL) | O_NONBLOCK);
#endif

  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr(ip_address);
  servaddr.sin_port = htons(PORTNUM);

  log_debug("Using dst-addr: %s", inet_ntoa(servaddr.sin_addr));
  log_debug("Using src-port: %d", ntohs(servaddr.sin_port));

  int open_flags = O_RDONLY;
#ifdef WINDOWS
  open_flags |= O_BINARY;
#endif
  int fd = open(filename, open_flags);
  unsigned char buffer[1024];
  int offset = 0;
  int bytes;

  int address;
  int start_addr;

  if (!use_binary) {
    // Read 2 byte load address
    bytes = read(fd, buffer, 2);
    if (bytes < 2) {
      log_crit("Failed to read load address from file '%s'", filename);
      exit(-1);
    }
    address = buffer[0] + 256 * buffer[1];
    start_addr = address;
    log_info("Load address of programme is $%04x", start_addr);
  }
  else {
    start_addr = bin_load_addr;
    address = start_addr;
    log_info("Load address of file is $%04x", start_addr);
  }

  if (!halt && !do_jump && !reset64 && !reset65) {
    // Try to automatically determine reset mode (c64 vs. m65)
    if (start_addr == 0x801) {
      log_note("PRG is C64 @0801");
      reset64 = 1;
    }
    else if (start_addr == 0x2001) {
      log_note("PRG is MEGA65 @2001");
      reset65 = 1;
    }
    else {
      log_crit("can't determine reset mode (c64/m65) from programme load address $%04x", start_addr);
      exit(-1);
    }
  }

  // Try to get MEGA65 to trigger the ethernet remote control hypperrupt
  trigger_eth_hyperrupt();

  // Adapt ip address (modify last byte to use ip x.y.z.65 as dest address)
  servaddr.sin_addr.s_addr &= 0x00ffffff;
  servaddr.sin_addr.s_addr |= (65 << 24);

  char msg[80];

  last_resend_time = gettime_us();

  // Clear screen first
  log_debug("Clearing screen");
  memset(colour_ram, 0x01, 1000);
  memset(progress_screen, 0x20, 1000);
  send_mem(0x1f800, colour_ram, 1000);
  send_mem(0x0400, progress_screen, 1000);
  wait_all_acks();
  log_debug("Screen cleared.");

  progress_line(0, 0, 40);
  snprintf(msg, 40, "Loading \"%s\" at $%04X", filename, address);
  progress_print(0, 1, msg);
  progress_line(0, 2, 40);

  while ((bytes = read(fd, buffer, 1024)) != 0) {
    log_debug("Read %d bytes at offset %d", bytes, offset);

    offset += bytes;

    // Send screen with current loading state
    progress_line(0, 10, 40);
    snprintf(msg, 40, "Loading block @ $%04X", address);
    progress_print(0, 11, msg);
    progress_line(0, 12, 40);

    // Update screen, but only if we are not still waiting for a previous update
    // so that we don't get stuck in lock-step
    if (no_pending_ack(0x0400 + 4 * 40))
      send_mem(0x0400 + 4 * 40, &progress_screen[4 * 40], 1000 - 4 * 40);

    send_mem(address, buffer, bytes);

    address += bytes;
  }

  memset(progress_screen, 0x20, 1000);
  snprintf(msg, 40, "Loaded $%04X - $%04X", start_addr, address);
  progress_line(0, 15, 40);
  progress_print(0, 16, msg);
  progress_line(0, 17, 40);
  send_mem(0x0400 + 4 * 40, &progress_screen[4 * 40], 1000 - 4 * 40);

  log_note("Sent %s to %s on port %d.", filename, inet_ntoa(servaddr.sin_addr), ntohs(servaddr.sin_port));

  wait_all_acks();

  log_info("Now telling MEGA65 that we are all done...");

  // XXX - We don't check that this last packet has arrived, as it doesn't have an ACK mechanism (yet)
  // XXX - We should make it ACK as well.

  if (reset64) {
    log_note("Reset to C64 mode");
    // patch in end address
    ethlet_all_done_basic2[ethlet_all_done_basic2_offset_data_end_address] = address & 0xff;
    ethlet_all_done_basic2[ethlet_all_done_basic2_offset_data_end_address + 1] = address >> 8;

    // patch in do_run
    ethlet_all_done_basic2[ethlet_all_done_basic2_offset_do_run] = do_run;

    // patch in cartridge signature enable
    ethlet_all_done_basic2[ethlet_all_done_basic2_offset_enable_cart_signature] = cart_detect;

    sendto(sockfd, ethlet_all_done_basic2, ethlet_all_done_basic2_len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  }
  else if (reset65) {
    log_note("Reset to MEGA65 mode");
    // patch in end address
    ethlet_all_done_basic65[ethlet_all_done_basic65_offset_autostart] = address & 0xff;
    ethlet_all_done_basic65[ethlet_all_done_basic65_offset_autostart + 1] = address >> 8;

    // patch in do_run
    ethlet_all_done_basic65[ethlet_all_done_basic65_offset_autostart + 2] = do_run;

    // patch in cartridge signature enable
    ethlet_all_done_basic65[ethlet_all_done_basic65_offset_autostart + 3] = cart_detect;

    sendto(sockfd, ethlet_all_done_basic65, ethlet_all_done_basic65_len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  }
  else if (do_jump) {
    // patch in jump address
    log_note("Jumping to address $%04X", jump_addr);
    ethlet_all_done_jump[ethlet_all_done_jump_offset_jump_addr + 1] = jump_addr & 0xff;
    ethlet_all_done_jump[ethlet_all_done_jump_offset_jump_addr + 2] = jump_addr >> 8;

    sendto(sockfd, ethlet_all_done_jump, ethlet_all_done_jump_len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
  }

#ifdef WINDOWS
  WSACleanup();
#endif

  return 0;
}
