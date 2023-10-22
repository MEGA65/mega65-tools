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
#include <libgen.h>
#include <limits.h> // PATH_MAX

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
int file_offset = 0;
char ip_address[40];
char network_interface[256];
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

  CMD_OPTION("discover",    no_argument,       0,            'D',   "",     "Autodetect MEGA65 on local network, output IPv6 address and interface, and exit.");
  CMD_OPTION("ip",          required_argument, 0,            'i', "ipaddr", "IPv6 address and interface (eg. fe80::12ff:fe34:56%eth0) of MEGA65. Autodetected if not specified.");
  CMD_OPTION("run",         no_argument,       0,            'r',   "",     "Automatically RUN programme after loading.");
  CMD_OPTION("rom",         required_argument, 0,            'R', "file",   "Upload and use ROM <file> instead of the default one on SD card (prgname is optional in this case).");
  CMD_OPTION("c64mode",     no_argument,       0,            '4',   "",     "Reset to C64 mode after transfer.");
  CMD_OPTION("m65mode",     no_argument,       0,            '5',   "",     "Reset to MEGA65 mode after transfer.");
  CMD_OPTION("halt",        no_argument,       &halt,        1,     "",     "Halt and wait for next transfer after completion.");
  CMD_OPTION("jump",        required_argument, 0,            'j', "addr",   "Jump to provided address <addr> after loading (hex notation).");
  CMD_OPTION("bin",         required_argument, 0,            'b', "addr",   "Treat <prgname> as binary file and load at address <addr> (hex notation).");
  CMD_OPTION("offset",      required_argument, 0,            'o', "bytes",  "Skip first <bytes> bytes (hex notation) when loading the file.");
  CMD_OPTION("cart-detect", no_argument,       &cart_detect, 1,     "",     "Enable detection of cartridge signature CBM80 at $8004 on reset.");
  CMD_OPTION("mount",       required_argument, 0,            'm', "file",   "Mount d81 file image <file> from SD card (eg. MEGA65.D81).");
  // clang-format on
}

const char *absolute_program_path(const char *program_path) {
    static char abs_path[PATH_MAX];
    char *temp_path = strdup(program_path);
    char *temp_path2 = strdup(program_path);

    if (!temp_path || !temp_path2) {
        perror("strdup");
        free(temp_path);  // Free in case one succeeded
        free(temp_path2);  // Free in case one succeeded
        return NULL;
    }

    char *dir_path = dirname(temp_path);
    char *base_name = basename(temp_path2);

    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, base_name);

#ifdef WINDOWS
    if (!_fullpath(abs_path, full_path, sizeof(abs_path))) {
        perror("_fullpath");
        free(temp_path);
        free(temp_path2);
        return NULL;
    }
#else
    if (!realpath(full_path, abs_path)) {
        perror("realpath");
        free(temp_path);
        free(temp_path2);
        return NULL;
    }
#endif

    free(temp_path);
    free(temp_path2);
    return abs_path;
}

void discover_mega65(const char *progname)
{
  if (probe_mega65_ipv6_address(3000) != 0) {
    log_error("Unable to discover MEGA65 on local network");
    log_error("Please make sure the MEGA65 remote control is enabled via SHIFT+POUND.");
    log_error("The power LED should be flashing green/yellow.");
    log_error("If still having issues, please make sure your firewall is accepting UDP packets on port 4510 for application:\n\"%s\"", absolute_program_path(progname));
    exit(-1);
  }
  printf("MEGA65 found at %s%%%s\n", ethl_get_ip_address(), ethl_get_interface_name());
}

/**
 * @brief Loads a file into memory at the specified address.
 *
 * This function reads a file from the specified file descriptor and loads it into memory
 * starting at the specified address.
 *
 * @param fd The file descriptor of the file to load.
 * @param start_addr The address to start loading the file into memory.
 * @return Number of bytes loaded.
 */
int load_file(int fd, int start_addr, int timeout_ms)
{
  unsigned char buffer[1024];
  int offset = 0;
  int bytes;
  int address = start_addr;

  while ((bytes = read(fd, buffer, 1024)) != 0) {
    log_debug("Read %d bytes at offset %d", bytes, offset);
    offset += bytes;
    if (send_mem(address, buffer, bytes, timeout_ms) < 0) {
      log_error("Failed to send data to MEGA65");
      return -1;
    }
    address += bytes;
  }

  return address - start_addr;
}

int main(int argc, char **argv)
{
  int opt_index;
  int result;
  ip_address[0] = '\0';
  network_interface[0] = '\0';

  init_cmd_options();

  // so we can see errors while parsing args
  log_setup(stderr, LOG_NOTE);

  if (argc == 1)
    usage(-3, "No arguments given!");

  int opt;
  while ((opt = getopt_long(argc, argv, "Di:rR:45hj:b:o:m:0:", cmd_opts, &opt_index)) != -1) {
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
    case 'D':
      discover_mega65(argv[0]);
      exit(0);
    case 'i':
      if (parse_ipv6_and_interface(optarg, ip_address, network_interface) != 0) {
        exit(-1);
      }
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
    case 'o':
      if (sscanf(optarg, "%x", &file_offset) != 1) {
        log_crit("-o option needs hex offset");
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

#ifdef WINDOWS
    open_flags |= O_BINARY;
#endif

  if (filename) {
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

    lseek(fd, file_offset, SEEK_CUR);
    start_addr += file_offset;
  }


  if (d81_image && strlen(d81_image) > 23) {
    d81_image[24] = '\0';
  }

  if (ip_address[0] == '\0') {
    result = etherload_init(NULL, NULL);
  }
  else {
    result = etherload_init(ip_address, network_interface);
  }
  if (result < 0) {
    log_error("Unable to initialize ethernet communication");
    exit(-1);
  }
  ethl_setup_dmaload();
  ethl_set_queue_length(32);

  // Try to get MEGA65 to trigger the ethernet remote control hypperrupt
  if (trigger_eth_hyperrupt() < 0) {
    etherload_finish();
    exit(-1);
  }

  // Send echo packet to MEGA65, will be re-sent until MEGA65 responds
  // Once MEGA65 responds, we know ETHLOAD.M65 is running and ready to receive data
  if (ethl_ping(3000) < 0) {
    log_error("No response from MEGA65");
    etherload_finish();
    exit(-1);
  }

  // allow overwriting of ROM area
  set_send_mem_rom_write_enable();

  if (filename) {
    address = start_addr + load_file(fd, start_addr, 2000);
    if (address < 0) {
      log_error("Timeout while sending data to MEGA65");
      etherload_finish();
      close(fd);
      exit(-1);
    }
    close(fd);
    log_note("Sent %s to %s on port %d.", filename, ethl_get_ip_address(), ethl_get_port());
  }

  if (rom_file) {
    int fd = open(rom_file, open_flags);
    if (load_file(fd, 0x20000, 2000) < 0) {
      log_error("Timeout while sending data to MEGA65");
      etherload_finish();
      close(fd);
      exit(-1);
    }
    close(fd);
    log_note("Sent ROM %s to %s on port %d.", rom_file, ethl_get_ip_address(), ethl_get_port());
  }

  wait_all_acks(2000);

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
