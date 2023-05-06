#include <etherload_common.h>
#include "ethlet_all_done_basic65_map.h"
#include "ethlet_all_done_basic2_map.h"
#include "ethlet_all_done_jump_map.h"

#ifndef WINDOWS
#include <sys/ioctl.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

#include <logging.h>

extern unsigned char ethlet_all_done_basic2[];
extern int ethlet_all_done_basic2_len;
extern unsigned char ethlet_all_done_basic65[];
extern int ethlet_all_done_basic65_len;
extern unsigned char ethlet_all_done_jump[];
extern int ethlet_all_done_jump_len;

unsigned char colour_ram[1000];
unsigned char progress_screen[1000];

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
char *d81_image = NULL;
char *rom_file = NULL;

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
  CMD_OPTION("rom",         required_argument, 0,            'R', "file",   "Upload and use ROM <file> instead of the default one on SD card (prgname is optional in this case).");
  CMD_OPTION("c64mode",     no_argument,       0,            '4',   "",     "Reset to C64 mode after transfer.");
  CMD_OPTION("m65mode",     no_argument,       0,            '5',   "",     "Reset to MEGA65 mode after transfer.");
  CMD_OPTION("halt",        no_argument,       &halt,        1,     "",     "Halt and wait for next transfer after completion.");
  CMD_OPTION("jump",        required_argument, 0,            'j', "addr",   "Jump to provided address <addr> after loading (hex notation).");
  CMD_OPTION("bin",         required_argument, 0,            'b', "addr",   "Treat <prgname> as binary file and load at address <addr> (hex notation).");
  CMD_OPTION("cart-detect", no_argument,       &cart_detect, 1,     "",     "Enable detection of cartridge signature CBM80 at $8004 on reset.");
  CMD_OPTION("mount",       required_argument, 0,            'm', "file",   "Mount d81 file image <file> from SD card (eg. MEGA65.D81).");
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

void load_file(int fd, int start_addr)
{
  char msg[80];
  unsigned char buffer[1024];
  int offset = 0;
  int bytes;
  int address = start_addr;

  // Clear screen first
  log_debug("Clearing screen");
  memset(colour_ram, 0x01, 1000);
  memset(progress_screen, 0x20, 1000);
  send_mem(0x1f800, colour_ram, 1000);
  send_mem(0x0400, progress_screen, 1000);
  wait_all_acks();
  log_debug("Screen cleared.");

  progress_line(0, 0, 40);
  snprintf(msg, 40, "Loading \"%s\" at $%07X", filename, address);
  progress_print(0, 1, msg);
  progress_line(0, 2, 40);

  while ((bytes = read(fd, buffer, 1024)) != 0) {
    log_debug("Read %d bytes at offset %d", bytes, offset);

    offset += bytes;

    // Send screen with current loading state
    progress_line(0, 10, 40);
    snprintf(msg, 40, "Loading block @ $%07X", address);
    progress_print(0, 11, msg);
    progress_line(0, 12, 40);

    // Update screen, but only if we are not still waiting for a previous update
    // so that we don't get stuck in lock-step
    if (dmaload_no_pending_ack(0x0400 + 4 * 40))
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
}

int main(int argc, char **argv)
{
  int opt_index;

  init_cmd_options();

  // so we can see errors while parsing args
  log_setup(stderr, LOG_NOTE);

  if (argc == 1)
    usage(-3, "No arguments given!");

  int opt;
  while ((opt = getopt_long(argc, argv, "i:rR:45hj:b:m:0:", cmd_opts, &opt_index)) != -1) {
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
    case 'm':
      d81_image = strdup(optarg);
      break;
    case 'R':
      rom_file = strdup(optarg);
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

  if (!argv[optind] && !rom_file) {
    usage(-3, "Filename for upload not specified, aborting.");
  }

  if (argv[optind]) {
    filename = strdup(argv[optind]);
    check_file_access(filename, "programme");
  }
  else if(!rom_file) {
    usage(-3, "Filename for upload not specified, aborting.");
  }

  if (argc - optind > 1)
    usage(-3, "Unexpected extra commandline arguments.");

  log_debug("parameter parsing done");

  log_note("%s %s", TOOLNAME, version_string);

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

  // check rom file
  if (rom_file) {
    check_file_access(rom_file, "ROM");
    if (!filename && !reset64 && !reset65) {
      // reset to M65 mode if no prg and no reset mode is provided 
      reset65 = 1;
    }
  }

  int open_flags = O_RDONLY;
  int fd;
  int address;
  int start_addr;

  if (filename) {
#ifdef WINDOWS
    open_flags |= O_BINARY;
#endif
    fd = open(filename, open_flags);
    
    if (!use_binary) {
      // Read 2 byte load address
      unsigned char buffer[2]; 
      ssize_t bytes = read(fd, buffer, 2);
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
  }

  if (d81_image && strlen(d81_image) > 23) {
    d81_image[24] = '\0';
  }

  etherload_init(ip_address);
  ethl_setup_dmaload();
  ethl_set_queue_length(32);

  // Try to get MEGA65 to trigger the ethernet remote control hypperrupt
  trigger_eth_hyperrupt();

  if (filename) {
    load_file(fd, start_addr);
    close(fd);
    log_note("Sent %s to %s on port %d.", filename, ethl_get_ip_address(), ethl_get_port());
  }

  if (rom_file) {
    set_send_mem_rom_write_enable();
    int fd = open(rom_file, open_flags);
    load_file(fd, 0x20000);
    close(fd);
    log_note("Sent ROM %s to %s on port %d.", rom_file, ethl_get_ip_address(), ethl_get_port());
  }

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

    // patch in d81 filename
    if (d81_image) {
      memcpy(&ethlet_all_done_basic2[ethlet_all_done_basic2_offset_d81filename], d81_image, strlen(d81_image));
    }

    // patch in disabling of default rom load
    if (rom_file) {
      ethlet_all_done_basic2[ethlet_all_done_basic2_offset_enable_default_rom_load] = 0;
    }

    // patch in disabling of prg restore
    if (!filename) {
      ethlet_all_done_basic2[ethlet_all_done_basic2_offset_restore_prg] = 0;
    }


    send_ethlet(ethlet_all_done_basic2, ethlet_all_done_basic2_len);
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

    // patch in d81 filename
    if (d81_image) {
      memcpy(&ethlet_all_done_basic65[ethlet_all_done_basic65_offset_d81filename], d81_image, strlen(d81_image));
    }

    // patch in disabling of default rom load
    if (rom_file) {
      ethlet_all_done_basic65[ethlet_all_done_basic65_offset_enable_default_rom_load] = 0;
    }

    // patch in disabling of prg restore
    if (!filename) {
      ethlet_all_done_basic65[ethlet_all_done_basic65_offset_restore_prg] = 0;
    }

    send_ethlet(ethlet_all_done_basic65, ethlet_all_done_basic65_len);
  }
  else if (do_jump) {
    // patch in jump address
    log_note("Jumping to address $%04X", jump_addr);
    ethlet_all_done_jump[ethlet_all_done_jump_offset_jump_addr + 1] = jump_addr & 0xff;
    ethlet_all_done_jump[ethlet_all_done_jump_offset_jump_addr + 2] = jump_addr >> 8;

    // patch in d81 filename
    if (d81_image) {
      memcpy(&ethlet_all_done_jump[ethlet_all_done_jump_offset_d81filename], d81_image, strlen(d81_image));
    }

    send_ethlet(ethlet_all_done_jump, ethlet_all_done_jump_len);
  }

  etherload_finish();

  return 0;
}
