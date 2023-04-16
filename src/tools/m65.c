/*
  Load the specified program into memory on the C65GS via the serial monitor.

  We add some convenience features:

  1. If an optional file name is provided, then we stuff the keyboard buffer
  with the LOAD command.  We check if we are in C65 mode, and if so, do GO64
  (look for reversed spaces at $0800 for C65 ROM detection).  Keyboard buffer @ $34A,
  buffer length @ $D0 in C65 mode, same as C128.  Then buffer is $277 in C64
  mode, buffer length @ $C6 in C64 mode.

  2. If an optional bitstream file is provided, then we use fpgajtag to load
  the bitstream via JTAG.  fpgajtag is now compiled internally to this program.

  Copyright (C) 2014-2020 Paul Gardner-Stephen
  Portions Copyright (C) 2013 Serval Project Inc.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <pthread.h>
#include <libusb.h>

#ifndef WINDOWS
#include <glob.h>
#include <sys/ioctl.h>
#else
#include <windows.h>
#endif

#include <m65common.h>
#include <logging.h>
#include <screen_shot.h>
#include <fpgajtag.h>

#define UT_TIMEOUT 10
#define UT_RES_TIMEOUT 127
#define MAX_TERM_WIDTH 100 // for help only currently

#define TOOLNAME "MEGA65 Cross-Development Tool"
#if defined(WINDOWS)
#define PROGNAME "m65.exe"
#elif defined(__APPLE__)
#define PROGNAME "m65.osx"
#else
#define PROGNAME "m65"
#endif
#ifdef WINDOWS
#define DEVICENAME "COM6"
#elif __APPLE__
#define DEVICENAME "/dev/cu.usbserial-6"
#else
#define DEVICENAME "/dev/ttyUSB1"
#endif

extern int pending_vf011_read;
extern int pending_vf011_write;
extern int pending_vf011_device;
extern int pending_vf011_track;
extern int pending_vf011_sector;
extern int pending_vf011_side;

extern int debug_serial;
int debug_load_memory = 0;

extern unsigned char recent_bytes[4];

int osk_enable = 0;

int not_already_loaded = 1;

int unit_test_mode = 0;
int unit_test_timeout = UT_TIMEOUT;

int halt = 0;

char *load_binary = NULL;

int viciv_mode_report(unsigned char *viciv_regs);

void do_exit(int retval);

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
int pal_mode = 0;
int ntsc_mode = 0;
int reset_first = 0;

int boundary_scan = 0;
char boundary_xdc[1024] = "";
char boundary_bsdl[1024] = "";
char jtag_sensitivity[1024] = "";

int hyppo_report = 0;
unsigned char hyppo_buffer[1024];

int counter = 0;

int show_audio_mixer = 0;
char *set_mixer_args = NULL;
int state = 99;
unsigned int name_len, name_lo, name_hi, name_addr = -1;
int do_go64 = 0;
int do_run = 0;
int comma_eight_comma_one = 0;
int ethernet_video = 0;
int ethernet_cpulog = 0;
int virtual_f011 = 0;
char *d81file = NULL;
char *filename = NULL;
char *romfile = NULL;
char *logfile = NULL;
char *unittest_logfile = NULL;
char *flashmenufile = NULL;
char *charromfile = NULL;
char *colourramfile = NULL;
FILE *f = NULL;
FILE *fd81 = NULL;
char *search_path = ".";
char *bitstream = NULL;
char *vivado_bat = NULL;
char *hyppo = NULL;
char *jtag_serial = NULL;
char *serial_port = NULL;
char modeline_cmd[1024] = "";
int break_point = -1;
int jtag_only = 0;
int bitstream_only = 0;
uint32_t zap_addr;
int zap = 0;
int wait_for_bitstream = 0;
int memsave_start = -1, memsave_end = -1;
char *memsave_filename = NULL;

int hypervisor_paused = 0;

int screen_shot = 0;
char *screen_shot_file = NULL;
int screen_rows_remaining = 0;
int next_screen_address = 0;
int screen_line_offset = 0;
unsigned char screen_line_buffer[256];

char *type_text = NULL;
int type_text_cr = 0;

int no_cart = 0;

#define READ_SECTOR_BUFFER_ADDRESS 0xFFD6c00
#define WRITE_SECTOR_BUFFER_ADDRESS 0xFFD6c00
int sdbuf_request_addr = 0;
unsigned char sd_sector_buf[512];
int saved_track = 0;
int saved_sector = 0;
int saved_side = 0;

int first_load = 1;
int first_go64 = 1;

unsigned char viciv_regs[0x100];
int mode_report = 0;

long long last_virtual_time = 0;
int last_virtual_writep = 0;
int last_virtual_track = -1;
int last_virtual_sector = -1;
int last_virtual_side = -1;

long long vf011_first_read_time = 0;
int vf011_bytes_read = 0;

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
  int pos, extra = 1;
  char *buffer;

  if (strlen(line) <= wrap) {
    *offset = -1;
    return strdup(line);
  }

  // look if there is a \n less then wrap away
  for (pos = 0; pos <= wrap && line[pos] && line[pos] != '\n'; pos++)
    ;
  if (pos <= wrap) {
    buffer = malloc(pos + 1);
    if (buffer != NULL) {
      if (pos > 0)
        strncpy(buffer, line, pos);
      buffer[pos] = 0;
    }
    *offset = pos + 1;
    return buffer;
  }

  // search backwards from wrap to last space
  for (pos = wrap; line[pos] != ' ' && pos > 0; pos--)
    ;
  // no space found, just wrap to width
  if (pos == 0) {
    pos = wrap;
    extra = 0;
  }
  buffer = malloc(pos + 1);
  if (buffer != NULL) {
    strncpy(buffer, line, pos);
    buffer[pos] = 0;
  }
  *offset = pos + extra;

  return buffer;
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
  CMD_OPTION("help",      0, 0,         'h', "",      "Display help and exit.");
  cmd_log_start = cmd_count;
  CMD_OPTION("quiet",     0, &loglevel, 1,   "",      "Only display errors or critical errors.");
  CMD_OPTION("verbose",   0, &loglevel, 4,   "",      "More verbose logging.");
  CMD_OPTION("debug",     0, &loglevel, 5,   "",      "Enable debug logging.");
  cmd_log_end = cmd_count;
  CMD_OPTION("log",       1, 0,         '0', "level", "Set log <level> to argument (0-5, critical, error, warning, notice, info, debug).");

  CMD_OPTION("autodiscover", 0, 0,      'j', "",
                  "Try to autodiscover device and exit. "
                  "NOTE: m65 always tries to autodiscover the device if needed. This option is just for debugging.");
  CMD_OPTION("device",    1, 0,         'l', "port",  "Name of serial <port> to use, e.g., "DEVICENAME".");
  CMD_OPTION("jtagser",   1, 0,         'f', "serial","Select which FPGA to reconfigure by specifying JTAG <serial>.");
  CMD_OPTION("speed",     1, 0,         's', "230400|1000000|1500000|2000000|4000000",
                  "Speed of serial port in <bits per second> (defaults to 2000000). This needs to match the speed your bitstream uses!");
  CMD_OPTION("usedk",     0, 0,         'K', "",      "Use DK backend for libUSB, if available.");

  CMD_OPTION("bootslot",  1, 0,         'Z', "slot|addr", "Reconfigure FPGA from specified <slot> (argument<8) or <addr>ess (hex) in flash.");
  CMD_OPTION("bit",       1, 0,         'b', "file",  "name of a FPGA bitstream <file> to load.");
  CMD_OPTION("bitonly",   1, 0,         'q', "file",  "name of a FPGA bitstream <file> to load and then directly quit. Use this for cores other than MEGA65.");
  CMD_OPTION("vivadopath",1, 0,         'v', "",      "The location of the Vivado executable to use for -b on Windows.");

  CMD_OPTION("reset",     0, 0,         'F', "",      "Force reset on start.");
  CMD_OPTION("halt",      0, 0,         'H', "",      "Halt CPU after loading ROMs and program.");
  CMD_OPTION("nocart",    0, 0,         'N', "",      "Disable a running cartridge, and boot to C64 mode.");
  CMD_OPTION("run",       0, 0,         'r', "",      "Automatically RUN programme after loading.");
  CMD_OPTION("binary",    0, 0,         '1', "",      "Honor load address specified inside the PRG.");
  CMD_OPTION("break",     1, 0,         'B', "addr",  "set a breakpoint at <addr>ess (hex) on synchronising, and then immediately exit.");

  CMD_OPTION("hyppostatus", 0, 0,       'X', "",      "Show a report of current Hypervisor status.");
  CMD_OPTION("inject",    1, 0,         '@', "file@addr", "Load a binary <file> at <addr>ess (hex).");
  CMD_OPTION("c64mode",   0, 0,         '4', "",      "Switch to C64 mode.");
  CMD_OPTION("volume",    1, 0,         'A', "x[-y]=p", "Set audio coefficient(s) <x> (and optionally up to <y>) to <p> percent of maximum volume.");
  CMD_OPTION("mixer",     0, 0,         'a', "",      "Read and display audio cross-bar mixer status.");
  CMD_OPTION("pal",       0, 0,         'p', "",      "switch to PAL video mode.");
  CMD_OPTION("ntsc",      0, 0,         'n', "",      "switch to NTSC video mode.");

  CMD_OPTION("virtuald81",1, 0,         'd', "d81",   "enable virtual D81 access on local <d81> image.");

  CMD_OPTION("unittest",  2, 0,         'u', "timeout", "run program in unit test mode (<timeout> in seconds, defaults to 10).");
  CMD_OPTION("utlog",     1, 0,         'w', "file",  "append unit test results to <file>.");

  CMD_OPTION("screenshot", 2, 0,        'S', "file",
                  "show text rendering of MEGA65 screen, optionally save PNG screenshot to <file>. "
                  "Use 0 as <file> to not save a PNG screenshot. <file> defaults to "
                  "'mega65-screen-XXXXXX.png' with XXXXXX being autoincremented.");

  CMD_OPTION("hyppo",     1, 0,         'k', "file",  "HICKUP <file> to replace the HYPPO in the bitstream.");
    /* NOTE: You can use bitstream and/or HYPPO from the Jenkins server by using @issue/tag/hardware
       for the bitstream, and @issue/tag for HYPPO. */
  CMD_OPTION("flashmenu", 1, 0,         'U', "file",  "Flash menu <file> to preload at $50000-$57FFF.");
  CMD_OPTION("basicrom",  1, 0,         'R', "file",  "BASIC ROM <file> to preload at $20000-$3FFFF.");
  CMD_OPTION("charrom",   1, 0,         'C', "file",  "Character ROM <file> to preload at $FF7E000.");
  CMD_OPTION("colourrom", 1, 0,         'c', "file",  "Colour RAM <file> to preload at $FF80000.");

  CMD_OPTION("vtype",     1, 0,         't', "-|text",
                  "Type <text> via keyboard virtualisation.\nThe following escape sequences are supported:\n"
                  "    ~M  RETURN      ~T  INST/DEL\n"
                  "    ~C  RUN/STOP    ~1  F1\n"
                  "    ~D  DOWN        ~3  F3\n"
                  "    ~U  UP          ~5  F5\n"
                  "    ~L  LEFT        ~7  F7\n"
                  "    ~R  RIGHT       ~z  sleep 1 sec\n"
                  "    ~H  HOME        ~Z  sleep 2 sec\n"
                  "<-> will read input and display a live screen from the MEGA65.\n"
                  "Warning: this is awfully slow and has lots of problems!");
  CMD_OPTION("vtyperet",  1, 0,         'T', "-|text", "As virttype, but add a RETURN at the end of the line.");

  CMD_OPTION("memsave",   1, 0,         0x81, "[addr:addr=]filename", "saves memory range addr:addr (hex) to filename. "
                  "If addr range is omitted, save current basic memory. "
                  "Only BASIC save without addr range will add load addr in front of data!");

  CMD_OPTION("boundaryscan", 1, 0,      'J', "xdc,bsdl[,sens[,log]]",
                  "Do JTAG boundary scan of attached FPGA, using the provided <xdc> and <bsdl> files. "
                  "A <sens>itivity list can also be provided, to restrict the set of signals monitored. "
                  "Result is logged into the specified <log> file, if provided.");
  CMD_OPTION("ethvideo",  0, 0,         'E', "",      "Enable streaming of video via ethernet.");
  CMD_OPTION("ethcpulog", 0, 0,         'L', "",      "Enable streaming of CPU instruction log via ethernet.");
  CMD_OPTION("phoneosk",  0, 0,         'o', "",      "Enable on-screen keyboard (MEGAphone).");
  CMD_OPTION("debugloadmem", 0, &debug_load_memory, 1, "", "DEBUG - test load memory function.");
  // clang-format on
}

int virtual_f011_read(int device, int track, int sector, int side)
{

  pending_vf011_read = 0;

  long long start = gettime_ms();

  if (!vf011_first_read_time)
    vf011_first_read_time = start - 1;

#if 0
#ifdef WINDOWS
  fprintf(stderr,"T+%I64d ms : Servicing hypervisor request for F011 FDC sector read.\n",
	  gettime_ms()-start);
#else
  fprintf(stderr,"T+%lld ms : Servicing hypervisor request for F011 FDC sector read.\n",
	  gettime_ms()-start);
#endif
#endif

  if (fd81 == NULL) {

    fd81 = fopen(d81file, "rb+");
    if (!fd81) {
      log_crit("could not open D81 file: '%s'", d81file);
      exit(-1);
    }
  }

  // Only actually load new sector contents if we don't think it is a duplicate request
  if ( // ((gettime_ms()-last_virtual_time)>100)
       // ||
      (last_virtual_writep) || (last_virtual_track != track) || (last_virtual_sector != sector)
      || (last_virtual_side != side)) {
    last_virtual_time = gettime_ms();
    last_virtual_track = track;
    last_virtual_sector = sector;
    last_virtual_side = side;

    /* read the block */
    unsigned char buf[512];
    int b = -1;
    int physical_sector = (side == 0 ? sector - 1 : sector + 9);

    int result = fseek(fd81, (track * 20 + physical_sector) * 512, SEEK_SET);

    if (result) {
      log_crit("error finding D81 sector %d @ 0x%x", result, (track * 20 + physical_sector) * 512);
      exit(-2);
    }
    else {
      b = fread(buf, 1, 512, fd81);
      //	fprintf(stderr, " bytes read: %d @ 0x%x\n", b,(track*20+physical_sector)*512);

      if (b == 512) {

        //      dump_bytes(0,"The sector",buf,512);

        /* send block to m65 memory */
        push_ram(READ_SECTOR_BUFFER_ADDRESS, 0x200, buf);
      }
    }
  }

  /* signal done/result */
  real_stop_cpu();
  mega65_poke(0xffd3086, side & 0x7f);
  start_cpu();

  vf011_bytes_read += 256;
  log_info("READ  device: %d  track: %d  sector: %d  side: %d @ %3.2fKB/sec", device, track, sector, side,
      vf011_bytes_read * 1.0 / (gettime_ms() - vf011_first_read_time));

  return 0;
}

int virtual_f011_write(int device, int track, int sector, int side)
{

  pending_vf011_write = 0;

  long long start = gettime_ms();

  if (!vf011_first_read_time)
    vf011_first_read_time = start - 1;

  log_debug("servicing hypervisor request for F011 FDC sector write.");

  if (fd81 == NULL) {

    fd81 = fopen(d81file, "wb+");
    if (!fd81) {
      log_crit("could not open D81 file: '%s'", d81file);
      exit(-1);
    }
  }

  last_virtual_time = gettime_ms();
  last_virtual_track = track;
  last_virtual_sector = sector;
  last_virtual_side = side;

  unsigned char buf[512];
  fetch_ram(WRITE_SECTOR_BUFFER_ADDRESS, 512, buf);

  int physical_sector = (side == 0 ? sector - 1 : sector + 9);

  int result = fseek(fd81, (track * 20 + physical_sector) * 512, SEEK_SET);

  if (result) {
    log_crit("failed to find D81 sector %d @ 0x%x", result, (track * 20 + physical_sector) * 512);
    exit(-2);
  }
  else {
    int b = fwrite(buf, 1, 512, fd81);
    //	log_debeg("bytes read: %d @ 0x%x", b,(track*20+physical_sector)*512);

    if (b != 512) {
      log_warn("short write of %d bytes", b);
    }
  }

  /* signal done/result */
  real_stop_cpu();
  mega65_poke(0xffd3086, side & 0x0f);
  start_cpu();

  vf011_bytes_read += 256;
  log_info("WRITE device: %d  track: %d  sector: %d  side: %d @ %3.2fKB/sec", device, track, sector, side,
      vf011_bytes_read * 1.0 / (gettime_ms() - vf011_first_read_time));

  return 0;
}

uint32_t uint32_from_buf(unsigned char *b, int ofs)
{
  uint32_t v = 0;
  v = b[ofs + 0];
  v |= (b[ofs + 1] << 8);
  v |= (b[ofs + 2] << 16);
  v |= (b[ofs + 3] << 24);
  return v;
}

int memory_save(const int start, const int end, const char *filename)
{
  int memsave_start_addr = -1;
  int memsave_end_addr = -1;
  int cur_addr, count;
  unsigned char membuf[4096], is_basic = 0;
  FILE *o;

  if (start == -1 && end == -1) {
    log_debug("memory_save: no memory range given, detecting BASIC program");
    if (saw_c64_mode) {
      fetch_ram(0x2b, 4, membuf);
      memsave_start_addr = membuf[0] + (membuf[1] << 8);
      memsave_end_addr = membuf[2] + (membuf[3] << 8);
    }
    else {
      fetch_ram(0x82, 2, membuf);
      memsave_start_addr = 0x2001;
      memsave_end_addr = membuf[0] + (membuf[1] << 8);
    }
    if (memsave_end_addr - memsave_start_addr < 3) {
      log_note("BASIC memory is empty, nothing to save!");
      return -1;
    }
    log_note("saving BASIC memory $%04X-$%04X", memsave_start_addr, memsave_end_addr);
    is_basic = 1;
  }
  else {
    if (end < start) {
      memsave_start_addr = end;
      memsave_end_addr = start;
    }
    else {
      memsave_start_addr = start;
      memsave_end_addr = end;
    }
  }

  o = fopen(filename, "w");
  if (!o) {
    log_error("could not open memory save file '%s'", filename);
    return -1;
  }
  log_debug("memory_save: opened '%s' for writing", filename);

  if (is_basic) {
    membuf[0] = memsave_start_addr & 0xff;
    membuf[1] = (memsave_start_addr >> 8) & 0xff;
    fwrite(membuf, 2, 1, o);
  }

  log_debug("memory_save: saving memory $%08x-%08x", memsave_start_addr, memsave_end_addr);
  cur_addr = memsave_start_addr;
  while (cur_addr < memsave_end_addr) {
    count = memsave_end_addr - cur_addr;
    if (count > 4096)
      count = 4096;
    log_debug("memory_save: fetching $%08x %d", cur_addr, count);
    fetch_ram(cur_addr, count, membuf);
    fwrite(membuf, count, 1, o);
    cur_addr += count;
  }

  fclose(o);
  log_debug("memory_save: closed output file");

  log_note("saved memory dump $%08x-$%08x to '%s'", memsave_start_addr, memsave_end_addr, filename);

  return 0;
}

void show_hyppo_report(void)
{
  // Buffer starats at $BC00 in HYPPO
  // $BC00 - $BCFF = DOS work area
  // $BD00 - $BDFF = Process Descriptor
  // $BE00 - $BEFF = Stack
  // $BF00 - $BFFF = ZP

  unsigned char syspart_buffer[0x40];
  unsigned char disktable_buffer[0x100];
  unsigned char pd_buffer[0x0100];

  fetch_ram(0xfffbb00, 0x100, disktable_buffer);
  fetch_ram(0xfffbbc0, 0x40, syspart_buffer);
  fetch_ram(0xfffbc00, 0x400, hyppo_buffer);
  fetch_ram(0xfffbd00, 0x100, pd_buffer);
  printf("HYPPO status:\n");

  printf("Disk count = $%02x\n", hyppo_buffer[0x001]);
  printf("Default Disk = $%02x\n", hyppo_buffer[0x002]);
  printf("Current Disk = $%02x\n", hyppo_buffer[0x003]);
  printf("Disk Table offset = $%02x\n", hyppo_buffer[0x004]);
  printf("Cluster of current directory = $%02x%02x%02x%02x\n", hyppo_buffer[0x008], hyppo_buffer[0x007], hyppo_buffer[0x006],
      hyppo_buffer[0x005]);
  printf("opendir_cluster = $%02x%02x%02x%02x\n", hyppo_buffer[0x00c], hyppo_buffer[0x00b], hyppo_buffer[0x00a],
      hyppo_buffer[0x009]);
  printf("opendir_sector = $%02x\n", hyppo_buffer[0x00d]);
  printf("opendir_entry = $%02x\n", hyppo_buffer[0x00e]);

  // Dirent struct follows:
  // 64 bytes for file name
  printf("dirent structure:\n");
  printf("  Filename = '%s'\n", &hyppo_buffer[0x00f]);
  printf("  Filename len = $%02x\n", hyppo_buffer[0x04f]);
  printf("  Short name = '%s'\n", &hyppo_buffer[0x050]);
  printf("  Start cluster = $%02x%02x%02x%02x\n", hyppo_buffer[0x060], hyppo_buffer[0x05f], hyppo_buffer[0x05e],
      hyppo_buffer[0x05d]);
  printf("  File length = $%02x%02x%02x%02x\n", hyppo_buffer[0x064], hyppo_buffer[0x063], hyppo_buffer[0x062],
      hyppo_buffer[0x061]);
  printf("  ATTRIB byte = $%02x\n", hyppo_buffer[0x065]);
  printf("Requested filename len = $%02x\n", hyppo_buffer[0x66]);
  printf("Requested filename = '%s'\n", &hyppo_buffer[0x67]);
  printf("sectorsread = $%02x%02x\n", hyppo_buffer[0xaa], hyppo_buffer[0xa9]);
  printf("bytes_remaining = $%02x%02x%02x%02x\n", hyppo_buffer[0xae], hyppo_buffer[0x0ad], hyppo_buffer[0xac],
      hyppo_buffer[0xab]);
  printf("current sector = $%02x%02x%02x%02x, ", hyppo_buffer[0xb2], hyppo_buffer[0x0b1], hyppo_buffer[0xb0],
      hyppo_buffer[0xaf]);
  printf("current cluster = $%02x%02x%02x%02x\n", hyppo_buffer[0xb6], hyppo_buffer[0x0b5], hyppo_buffer[0xb4],
      hyppo_buffer[0xb3]);
  printf("current sector in cluster = $%02x\n", hyppo_buffer[0xb7]);

  for (int fd = 0; fd < 4; fd++) {
    int fd_o = 0xb8 + fd * 0x10;
    printf("File descriptor #%d:\n", fd);
    printf("  disk ID = $%02x, ", hyppo_buffer[fd_o + 0]);
    printf("  mode = $%02x\n", hyppo_buffer[fd_o + 1]);
    printf("  start cluster = $%02x%02x%02x%02x, ", hyppo_buffer[fd_o + 5], hyppo_buffer[fd_o + 4], hyppo_buffer[fd_o + 3],
        hyppo_buffer[fd_o + 2]);
    printf("  current cluster = $%02x%02x%02x%02x\n", hyppo_buffer[fd_o + 9], hyppo_buffer[fd_o + 8], hyppo_buffer[fd_o + 7],
        hyppo_buffer[fd_o + 6]);
    printf("  sector in cluster = $%02x, ", hyppo_buffer[fd_o + 10]);
    printf("  offset in sector = $%02x%02x\n", hyppo_buffer[fd_o + 12], hyppo_buffer[fd_o + 11]);
    //    printf("  file offset = $%02x%02x (x 256 bytes? not used?)\n",
    //	   hyppo_buffer[fd_o+14],hyppo_buffer[fd_o+13]);
  }

  printf("Disk Table:\n");
  printf("  Disk# Start     Size       FS FAT_Len   SysLen Rsv RootDir Clusters  CSz #FATs Cluster0\n");
  for (int disk = 0; disk < 6; disk++) {
    printf("   #%d   $%08x $%08x  %02x $%08x $%04x  %02x  $%04x   $%08x %02x  %02x    $%08x\n", disk,
        uint32_from_buf(disktable_buffer, disk * 32 + 0), uint32_from_buf(disktable_buffer, disk * 32 + 4),
        (int)disktable_buffer[disk * 32 + 8], uint32_from_buf(disktable_buffer, disk * 32 + 9),
        uint32_from_buf(disktable_buffer, disk * 32 + 0x0d) & 0xffff, (int)disktable_buffer[disk * 32 + 0x0f],
        uint32_from_buf(disktable_buffer, disk * 32 + 0x10) & 0xffff, uint32_from_buf(disktable_buffer, disk * 32 + 0x12),
        (int)disktable_buffer[disk * 32 + 0x16], (int)disktable_buffer[disk * 32 + 0x17],
        uint32_from_buf(disktable_buffer, disk * 32 + 0x18));
  }
  printf("Current file descriptor # = $%02x\n", hyppo_buffer[0xf4]);
  printf("Current file descriptor offset = $%02x\n", hyppo_buffer[0xf5]);
  printf("Dos error code = $%02x\n", hyppo_buffer[0xf6]);
  printf("SYSPART error code = $%02x\n", hyppo_buffer[0xf7]);
  printf("SYSPART present = $%02x\n", hyppo_buffer[0xf8]);
  printf("SYSPART start = $%02x%02x%02x%02x\n", syspart_buffer[3], syspart_buffer[2], syspart_buffer[1], syspart_buffer[0]);

  printf("\nProcess descriptor:\n");
  printf("    Process ID: $%02x\n", pd_buffer[0x00]);
  printf("  Process name: ");
  for (int i = 0; i < 16; i++)
    printf("%c", pd_buffer[0x01 + i]);
  printf("\n");
}

int type_serial_mode = 0;

void do_type_key(unsigned char key)
{
  char cmd[1024];
  int c1 = 0x7f;
  int c2 = 0x7f;

  if (key == '~') {
    type_serial_mode ^= 1;
    if (type_serial_mode) {
      log_note("switching to ASCII stuffing of buffered UART");
    }
    return;
  }

  // Type directly to MEGA65 buffered serial port in ASCII
  if (type_serial_mode) {
    snprintf(cmd, 1024, "sffd30e3 %02x\n", key);
    slow_write(fd, cmd, strlen(cmd));
    return;
  }

  // left shift for upper case letters
  if (key >= 0x41 && key <= 0x5A)
    c2 = 0x0f;

  switch (key) {
    // Punctuation that requires shifts
  case '!':
    key = '1';
    c2 = 0x0f;
    break;
  case '\"':
    key = '2';
    c2 = 0x0f;
    break;
  case '#':
    key = '3';
    c2 = 0x0f;
    break;
  case '$':
    key = '4';
    c2 = 0x0f;
    break;
  case '%':
    key = '5';
    c2 = 0x0f;
    break;
  case '(':
    key = '8';
    c2 = 0x0f;
    break;
  case ')':
    key = '9';
    c2 = 0x0f;
    break;
  case '?':
    key = '/';
    c2 = 0x0f;
    break;
  case '<':
    key = ',';
    c2 = 0x0f;
    break;
  case '>':
    key = '.';
    c2 = 0x0f;
    break;
  }

  switch (key) {
  case 0x03:
    c1 = 0x3f;
    break; // RUN/STOP
  case 0x1d:
    c1 = 0x02;
    break; // Cursor right
  case 0x9d:
    c1 = 0x02;
    c2 = 0x0f;
    break; // Cursor left
  case 0x11:
    c1 = 0x07;
    break; // Cursor down
  case 0x91:
    c1 = 0x07;
    c2 = 0x0f;
    break; // Cursor up
  case 0x0d:
    c1 = 0x01;
    break; // RETURN
  case 0x14:
    c1 = 0x00;
    break; // INST/DEL
  case 0xF1:
    c1 = 0x04;
    break; // F1
  case 0xF3:
    c1 = 0x05;
    break; // F3
  case 0xF5:
    c1 = 0x06;
    break; // F5
  case 0xF7:
    c1 = 0x03;
    break; // F7

  case '3':
    c1 = 0x08;
    break;
  case 'w':
    c1 = 0x09;
    break;
  case 'a':
    c1 = 0x0a;
    break;
  case '4':
    c1 = 0x0b;
    break;
  case 'z':
    c1 = 0x0c;
    break;
  case 's':
    c1 = 0x0d;
    break;
  case 'e':
    c1 = 0x0e;
    break;

  case '5':
    c1 = 0x10;
    break;
  case 'r':
    c1 = 0x11;
    break;
  case 'd':
    c1 = 0x12;
    break;
  case '6':
    c1 = 0x13;
    break;
  case 'c':
    c1 = 0x14;
    break;
  case 'f':
    c1 = 0x15;
    break;
  case 't':
    c1 = 0x16;
    break;
  case 'x':
    c1 = 0x17;
    break;

  case '7':
    c1 = 0x18;
    break;
  case 'y':
    c1 = 0x19;
    break;
  case 'g':
    c1 = 0x1a;
    break;
  case '8':
    c1 = 0x1b;
    break;
  case 'b':
    c1 = 0x1c;
    break;
  case 'h':
    c1 = 0x1d;
    break;
  case 'u':
    c1 = 0x1e;
    break;
  case 'v':
    c1 = 0x1f;
    break;

  case '9':
    c1 = 0x20;
    break;
  case 'i':
    c1 = 0x21;
    break;
  case 'j':
    c1 = 0x22;
    break;
  case '0':
    c1 = 0x23;
    break;
  case 'm':
    c1 = 0x24;
    break;
  case 'k':
    c1 = 0x25;
    break;
  case 'o':
    c1 = 0x26;
    break;
  case 'n':
    c1 = 0x27;
    break;

  case '+':
    c1 = 0x28;
    break;
  case 'p':
    c1 = 0x29;
    break;
  case 'l':
    c1 = 0x2a;
    break;
  case '-':
    c1 = 0x2b;
    break;
  case '.':
    c1 = 0x2c;
    break;
  case ':':
    c1 = 0x2d;
    break;
  case '@':
    c1 = 0x2e;
    break;
  case ',':
    c1 = 0x2f;
    break;

  case '}':
    c1 = 0x30;
    break; // British pound symbol
  case '*':
    c1 = 0x31;
    break;
  case ';':
    c1 = 0x32;
    break;
  case 0x13:
    c1 = 0x33;
    break; // home
    // case '': c1=0x34; break; right shift
  case '=':
    c1 = 0x35;
    break;
    // What was this with 0x91?
    //	case 0x91: c1=0x36; break;
  case '/':
    c1 = 0x37;
    break;

  case '1':
    c1 = 0x38;
    break;
  case '_':
    c1 = 0x39;
    break;
    // case '': c1=0x3a; break; control
  case '2':
    c1 = 0x3b;
    break;
  case ' ':
    c1 = 0x3c;
    break;
    // case '': c1=0x3d; break; C=
  case 'q':
    c1 = 0x3e;
    break;
  case 0x0c:
    c1 = 0x3f;
    break;

  default:
    c1 = 0x7f;
  }
  //  fprintf(stderr,"keys $%02x $%02x\n",c1,c2);
  snprintf(cmd, 1024, "sffd3615 %02x %02x\n", c1, c2);
  slow_write(fd, cmd, strlen(cmd));

  // Allow time for a keyboard scan interrupt to occur
  usleep(20000);

  // Stop pressing keys
  slow_write(fd, "sffd3615 7f 7f 7f \n", 19);

  // Allow time for a keyboard scan interrupt to occur
  usleep(20000);
}

void do_type_text(char *type_text)
{
  log_note("typing text via virtual keyboard...");

#ifndef WINDOWS
  time_t last_time_check = 0;

  int use_line_mode = 0;
#endif

  if (!strcmp(type_text, "-")) {
#ifndef WINDOWS
    if (use_line_mode) {
#endif
      fprintf(stderr, "Reading input from stdin.\nType . on a line by itself to end.\n");
      char line[1024];
      line[0] = 0;
      fgets(line, 1024, stdin);
      while (line[0]) {
        while (line[0] && ((line[strlen(line) - 1] == '\n') || line[strlen(line) - 1] == '\r'))
          line[strlen(line) - 1] = 0;
        if (!strcmp(line, "."))
          break;

        for (int i = 0; line[i]; i++)
          do_type_key(line[i]);

        // carriage return at end of line
        slow_write(fd, "sffd3615 01 7f 7f \n", 19);
        // Allow time for a keyboard scan interrupt
        usleep(20000);
        // release keys
        slow_write(fd, "sffd3615 7f 7f 7f \n", 19);
        // Allow time for a keyboard scan interrupt
        usleep(20000);

        // Display screen updates while typing if requested
        if (screen_shot) {
          real_stop_cpu();
          get_video_state();
          do_screen_shot_ascii();
          start_cpu();
        }

        line[0] = 0;
        fgets(line, 1024, stdin);
      }
#ifndef WINDOWS
    }
    else {
      // Char mode

      // XXX Windows needs a quite different approach.
      // See, e.g.: https://cpp.hotexamples.com/examples/-/-/ReadConsoleInput/cpp-readconsoleinput-function-examples.html
      // But probably easier to just add this functionality in to tayger's MEGA65 Connect programme instead.

      struct termios old_tio, new_tio;
      int c;

      /* get the terminal settings for stdin */
      tcgetattr(STDIN_FILENO, &old_tio);

      /* we want to keep the old setting to restore them a the end */
      new_tio = old_tio;

      /* disable canonical mode (buffered i/o) and local echo */
      new_tio.c_lflag &= (~ICANON & ~ECHO);

      /* set the new settings immediately */
      tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

      // Display screen updates while typing if requested
      if (screen_shot) {
        real_stop_cpu();
        get_video_state();
        do_screen_shot_ascii();
        start_cpu();
        // Clear screen
        printf("%c[2J", 0x1b);
      }

      fprintf(stderr, "Reading input from terminal in character mode.\n"
                      "Type CONTROL-Y to end.\n");

      // make stdin non-blocking
      fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

      c = getc(stdin);
      if (c == -1)
        c = 0;
      while (c != 25) {
        //        printf("$%02x -> ", c);
        switch (c) {
        case 0x7f:
          c = 0x14;
          break; // DELETE
        case 0x0a:
          c = 0x0d;
          break;   // RETURN
        case 0x09: // TAB = RUN/STOP
          printf("TAB\n");
          c = 0x03;
          break;
        case 0x1b:
          // Escape code
          printf("ESC code: ");
          c = 0;
          while (!c || (c == -1))
            c = getc(stdin);
          printf("ESC code: $%02x", c);
          if (c == '[') {
            c = 0;
            while (!c || (c == -1))
              c = getc(stdin);
            switch (c) {
            case 0x41:
              c = 0x91;
              break; // up
            case 0x42:
              c = 0x11;
              break; // down
            case 0x43:
              c = 0x1d;
              break; // right
            case 0x44:
              c = 0x9d;
              break; // left
            case 0x48:
              c = 0x13;
              break; // home
            default:
              c = 0;
            }
          }
          else
            c = 0;
        }
        //        printf("$%02x\n", c);
        if (c && (c != -1)) {
          // Display screen updates while typing if requested
          if (screen_shot) {
            // Cursor to home position
            printf("%c[1;1H", 0x1b);
            real_stop_cpu();
            get_video_state();
            do_screen_shot_ascii();
            start_cpu();
          }
          do_type_key(c);
          if (c != -1)
            printf("Key $%02x    \n", c);
        }
        else {
          usleep(1000);
          //	  printf("."); fflush(stdout);
          if (time(0) != last_time_check) {
            // Cursor to home position
            printf("%c[1;1H", 0x1b);
            real_stop_cpu();
            get_video_state();
            do_screen_shot_ascii();
            start_cpu();
            last_time_check = time(0);
          }
        }
        c = getc(stdin);
      }
      /* enable canonical mode (buffered i/o) and local echo */
      new_tio.c_lflag |= (ICANON | ECHO);

      /* set the new settings immediately */
      tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    }
#endif
  }
  else {
    int i;
    unsigned char c1;
    for (i = 0; type_text[i]; i++) {
      if (type_text[i] == '~') {
        // eos after tilde? break out of loop!
        if (type_text[i + 1] == 0)
          break;
        // control sequences (remember to UPDATE USAGE!)
        switch (type_text[i + 1]) {
        case 'C':
          c1 = 0x03;
          break; // RUN/STOP
        case 'D':
          c1 = 0x11;
          break; // down
        case 'U':
          c1 = 0x91;
          break; // up
        case 'L':
          c1 = 0x9D;
          break; // left
        case 'H':
          c1 = 0x13;
          break; // HOME
        case 'R':
          c1 = 0x1D;
          break; // right
        case 'M':
          c1 = 0x0D;
          break; // RETURN
        case 'T':
          c1 = 0x14;
          break; // INST/DEL
        case '1':
          c1 = 0xF1;
          break; // F1
        case '3':
          c1 = 0xF3;
          break; // F3
        case '5':
          c1 = 0xF5;
          break; // F5
        case '7':
          c1 = 0xF7;
          break; // F7
        case 'Z':
          sleep(1);
        case 'z':
          sleep(1);
          break;
        }
        do_type_key(c1);
        i++;
      }
      else
        do_type_key(type_text[i]);
    }

    // RETURN at end if requested
    if (type_text_cr) {
      slow_write(fd, "sffd3615 01 7f 7f \n", 19);
      // Allow time for a keyboard scan interrupt
      usleep(20000);
    }
  }
  // Stop pressing keys
  slow_write(fd, "sffd3615 7f 7f 7f \n", 19);
}

char line[1024];
int line_len = 0;

void *run_boundary_scan(void *argp)
{
  xilinx_boundaryscan(boundary_xdc[0] ? boundary_xdc : NULL, boundary_bsdl[0] ? boundary_bsdl : NULL,
      jtag_sensitivity[0] ? jtag_sensitivity : NULL);
  return (void *)NULL;
}

#ifndef WINDOWS
#define MAX_THREADS 16
int thread_count = 0;
pthread_t threads[MAX_THREADS];
#endif

void download_bitstream(void)
{
  int issue, tag;
  char target[1024] = "mega65r2";
  if (sscanf(bitstream, "@%d/%d/%s", &issue, &tag, target) < 2) {
    fprintf(stderr,
        "ERROR: @ directive to download bitstreams must be in the format issue/tag/hardware, e.g., 168/1/mega65r2\n");
    exit(-3);
  }

  char filename[8192];
  snprintf(filename, 8192, "%s/.netrc", getenv("HOME"));
  FILE *nf = fopen(filename, "rb");
  if (!nf) {
    fprintf(stderr, "WARNING: You don't have a .netrc file.  You probably want to set one up with something like this:\n"
                    "    machine  app.scryptos.com\n"
                    "    login    <ask deft for one>\n"
                    "    password <ask deft for one>\n"
                    "So that you don't get asked by cadaver all the time for a username and password.\n");
  }
  fclose(nf);
  snprintf(filename, 8192, "/usr/bin/cadaver");
  nf = fopen(filename, "rb");
  if (!nf) {
    fprintf(stderr, "ERROR: You don't seem to have cadaver installed.\n"
                    "If you are on Ubuntu linux, try:\n"
                    "   sudo apt-get install cadaver\n"
                    "I'll try anyway, in case you just have it installed in a funny place.\n");
  }
  fclose(nf);

  fprintf(stderr, "Fetching bitstream from scryptos archive...\n");
  unlink("/tmp/monitor_load.folder.txt");
  char cmd[8192];
  snprintf(cmd, 8192,
      "echo ls | cadaver "
      "\"https://app.scryptos.com/webdav/MEGA/groups/MEGA65%%20filehost/ShareFolder/Bitstreams/Jenkins-Out/mega65-core/"
      "issues/%d/\" | grep \"%d-\" | cut -c9-40 | sed 's/ //g' > /tmp/monitor_load.folder.txt",
      issue, tag);
  system(cmd);

  FILE *f = fopen("/tmp/monitor_load.folder.txt", "rb");
  if (!f) {
    fprintf(stderr, "ERROR: Could not read WebDAV retrieved folder name from /tmp/monitor_load.folder.txt\n");
    exit(-2);
  }
  char folder[1024] = "";
  fread(folder, 1, 1024, f);
  fclose(f);
  while (folder[0] && folder[strlen(folder) - 1] < ' ')
    folder[strlen(folder) - 1] = 0;
  fprintf(stderr, "Resolved %d/%d to %d/%s\n", issue, tag, issue, folder);

  unlink("/tmp/monitor_load.bit");
  snprintf(cmd, 8192,
      "echo \"get %s.bit /tmp/monitor_load.bit\" | cadaver  "
      "\"https://app.scryptos.com/webdav/MEGA/groups/MEGA65%%20filehost/ShareFolder/Bitstreams/Jenkins-Out/mega65-core/"
      "issues/%d/%s/\"",
      target, issue, folder);
  fprintf(stderr, "%s\n", cmd);
  system(cmd);
  bitstream = "/tmp/monitor_load.bit";
}

void download_hyppo(void)
{
  int issue, tag;
  if (sscanf(hyppo, "@%d/%d", &issue, &tag) < 2) {
    fprintf(stderr, "ERROR: @ directive to download HICKUP.M65 must be in the format issue/tag, e.g., 168/1\n");
    exit(-3);
  }

  char filename[8192];
  snprintf(filename, 8192, "%s/.netrc", getenv("HOME"));
  FILE *nf = fopen(filename, "rb");
  if (!nf) {
    fprintf(stderr, "WARNING: You don't have a .netrc file.  You probably want to set one up with something like this:\n"
                    "    machine  app.scryptos.com\n"
                    "    login    <ask deft for one>\n"
                    "    password <ask deft for one>\n"
                    "So that you don't get asked by cadaver all the time for a username and password.\n");
  }
  fclose(nf);
  snprintf(filename, 8192, "/usr/bin/cadaver");
  nf = fopen(filename, "rb");
  if (!nf) {
    fprintf(stderr, "ERROR: You don't seem to have cadaver installed.\n"
                    "If you are on Ubuntu linux, try:\n"
                    "   sudo apt-get install cadaver\n"
                    "I'll try anyway, in case you just have it installed in a funny place.\n");
  }
  fclose(nf);

  fprintf(stderr, "Fetching HICKUP.M65 from scryptos archive...\n");
  unlink("/tmp/monitor_load.folder.txt");
  char cmd[8192];
  snprintf(cmd, 8192,
      "echo ls | cadaver "
      "\"https://app.scryptos.com/webdav/MEGA/groups/MEGA65%%20filehost/ShareFolder/Bitstreams/Jenkins-Out/mega65-core/"
      "issues/%d/\" | grep \"%d-\" | cut -c9-40 | sed 's/ //g' > /tmp/monitor_load.folder.txt",
      issue, tag);
  system(cmd);

  FILE *f = fopen("/tmp/monitor_load.folder.txt", "rb");
  if (!f) {
    fprintf(stderr, "ERROR: Could not read WebDAV retrieved folder name from /tmp/monitor_load.folder.txt\n");
    exit(-2);
  }
  char folder[1024] = "";
  fread(folder, 1, 1024, f);
  fclose(f);
  while (folder[0] && folder[strlen(folder) - 1] < ' ')
    folder[strlen(folder) - 1] = 0;
  fprintf(stderr, "Resolved %d/%d to %d/%s\n", issue, tag, issue, folder);

  unlink("/tmp/monitor_load.HICKUP.M65");
  snprintf(cmd, 8192,
      "echo \"get HICKUP.M65 /tmp/monitor_load.HICKUP.M65\" | cadaver  "
      "\"https://app.scryptos.com/webdav/MEGA/groups/MEGA65%%20filehost/ShareFolder/Bitstreams/Jenkins-Out/mega65-core/"
      "issues/%d/%s/\"",
      issue, folder);
  fprintf(stderr, "%s\n", cmd);
  system(cmd);
  hyppo = "/tmp/monitor_load.HICKUP.M65";
}

void load_bitstream(char *bitstream)
{
  if (vivado_bat != NULL) {
    /*  For Windows we just call Vivado to do the FPGA programming,
        while we are having horrible USB problems otherwise. */
    unsigned int fpga_id = 0x3631093;

    FILE *f = fopen(bitstream, "r");
    if (f) {
      unsigned char buff[8192];
      int len = fread(buff, 1, 8192, f);
      for (int i = 0; i < len; i += 4) {
        if ((buff[i + 0] == 0x30) && (buff[i + 1] == 0x01) && (buff[i + 2] == 0x80) && (buff[i + 3] == 0x01)) {
          i += 4;
          fpga_id = buff[i + 0] << 24;
          fpga_id |= buff[i + 1] << 16;
          fpga_id |= buff[i + 2] << 8;
          fpga_id |= buff[i + 3] << 0;

          log_info("detected FPGA ID %x from bitstream file", fpga_id);
          break;
        }
      }
      fclose(f);
    }
    else
      log_warn("could not open bitstream file '%s'", bitstream);

    char *part_name = "xc7a100t_0";
    log_info("expecting FPGA Part ID %x", fpga_id);
    if (fpga_id == 0x3636093)
      part_name = "xc7a200t_0";

    FILE *tclfile = fopen("temp.tcl", "w");
    if (!tclfile) {
      log_crit("Could not create temp.tcl");
      exit(-1);
    }
    fprintf(tclfile,
        "open_hw\n"           // "open_hw_manager\n"
        "connect_hw_server\n" // -allow_non_jtag\n"
        "open_hw_target\n"
        "current_hw_device [get_hw_devices %s]\n"
        "refresh_hw_device -update_hw_probes false [lindex [get_hw_devices %s] 0]\n"
        "refresh_hw_device -update_hw_probes false [lindex [get_hw_devices %s] 0]\n"
        "\n"
        "set_property PROBES.FILE {} [get_hw_devices %s]\n"
        "set_property FULL_PROBES.FILE {} [get_hw_devices %s]\n"
        "set_property PROGRAM.FILE {%s} [get_hw_devices %s]\n"
        "refresh_hw_device -update_hw_probes false [lindex [get_hw_devices %s] 0]\n"
        "program_hw_devices [get_hw_devices %s]\n"
        "refresh_hw_device [lindex [get_hw_devices %s] 0]\n"
        "quit\n",
        part_name, part_name, part_name, part_name, part_name, bitstream, part_name, part_name, part_name, part_name);
    fclose(tclfile);
    char cmd[8192];
    snprintf(cmd, 8192, "%s -mode batch -nojournal -nolog -notrace -source temp.tcl", vivado_bat);
    log_note("Running %s...", cmd);
    system(cmd);
    unlink("temp.tcl");
  }
  else {
    // No Vivado.bat, so try to use internal fpgajtag implementation.
    fpgajtag_main(bitstream);
  }
  log_note("Bitstream loaded");
}

void enter_hypervisor_mode(void)
{
  /* Ach! for some things we want to make sure we are in the hypervisor.
     This is a bit annoying, as we have to make sure we save state etc
     properly.
  */
  monitor_sync();
  real_stop_cpu();
  slow_write_safe(fd, "sffd367e 0\r", 11);
  slow_write_safe(fd, "\r", 1);
}

void return_from_hypervisor_mode(void)
{
  monitor_sync();
  slow_write_safe(fd, "sffd367f 0\r", 11);
  monitor_sync();
  slow_write_safe(fd, "t0\r", 3);
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

// returns 1 if there is a vf011 reqest, 0 if not
unsigned char check_for_vf011_requests()
{

  if (!virtual_f011) {
    return 0;
  }

  if (recent_bytes[3] == '!') {
    // Handle request
    recent_bytes[3] = 0;
    pending_vf011_read = 1;
    pending_vf011_device = 0;
    pending_vf011_track = recent_bytes[0] & 0x7f;
    pending_vf011_sector = recent_bytes[1] & 0x7f;
    pending_vf011_side = recent_bytes[2] & 0x0f;
    return 1;
  }
  if (recent_bytes[3] == 0x5c) {
    // Handle request
    recent_bytes[3] = 0;
    pending_vf011_write = 1;
    pending_vf011_device = 0;
    pending_vf011_track = recent_bytes[0] & 0x7f;
    pending_vf011_sector = recent_bytes[1] & 0x7f;
    pending_vf011_side = recent_bytes[2] & 0x0f;
    return 1;
  }
  return 0;
}

void handle_vf011_requests()
{
  if (!virtual_f011) {
    return;
  }

  while (pending_vf011_read || pending_vf011_write) {
    if (pending_vf011_read)
      virtual_f011_read(pending_vf011_device, pending_vf011_track, pending_vf011_sector, pending_vf011_side);
    if (pending_vf011_write)
      virtual_f011_write(pending_vf011_device, pending_vf011_track, pending_vf011_sector, pending_vf011_side);
  }
}

char *test_states[16] = { "START", " SKIP", " PASS", " FAIL", "ERROR", "C#$05", "C#$06", "C#$07", "C#$08", "C#$09", "C#$0A",
  "C#$0B", "C#$0C", "  LOG", " NAME", " DONE" };

char msgbuf[160], *endp;
char testname[160], testlog[160];
unsigned char inbuf[8192];
unsigned int failcount, test_last_issue, test_last_sub;
FILE *logPtr;

void unit_test_logline(unsigned short issue, unsigned char sub, unsigned char state, char *msg)
{
  char outstring[255];
  char temp[255];

  struct timeval currentTime;

  gettimeofday(&currentTime, NULL);
  strftime(outstring, 255, "%Y-%m-%dT%H:%M:%S", gmtime((const time_t *)&(currentTime.tv_sec)));

  snprintf(temp, 255, ".%03dZ %s (Issue#%04d, Test #%03d", (unsigned int)currentTime.tv_usec / 1000, test_states[state],
      issue, sub);
  strncat(outstring, temp, 254);

  if (msg) {
    snprintf(temp, 255, " - %s)", msg);
  }
  else {
    snprintf(temp, 255, ")");
  }
  strncat(outstring, temp, 254);

  log_note(outstring);
  if (logPtr) {
    fprintf(logPtr, "%s\n", outstring);
    fflush(logPtr);
  }
}

void unit_test_log(unsigned char bytes[4])
{
  unsigned short test_issue = bytes[0] + (bytes[1] << 8);
  unsigned char test_sub = bytes[2];
  // dump_bytes(0, "bytes", bytes, 4);
  // fprintf(stderr, "issue = %d, sub = %d\n", test_issue, test_sub);

  // check for log message, but no PASS/FAIL follows
  if (testlog[0] && bytes[3] != 0xf2 && bytes[3] != 0xf3) {
    unit_test_logline(test_last_issue, test_last_sub, 0xd, testlog);
    testlog[0] = 0;
  }

  unit_test_logline(test_issue, test_sub, bytes[3] - 0xf0, testlog[0] ? testlog : (testname[0] ? testname : NULL));
  testlog[0] = 0;
  test_last_issue = test_issue;
  test_last_sub = test_sub;

  switch (bytes[3]) {
  case 0xf0: // Starting a test
    break;
  case 0xf1: // Skipping a test
    break;
  case 0xf2: // Test pass
    break;
  case 0xf3: // Test failure (ie test ran, but detected failure of test condition)
    failcount++;
    break;
  case 0xf4: // Error trying to run test
    failcount++;
    break;
  case 0xfd: // Log message
    break;
  case 0xfe: // Set name of current test
    break;
  case 0xff: // Last test complete
    log_note("terminating after completion of unit test");
    if (logPtr) {
      if (failcount > 0)
        fprintf(logPtr, "!!!!! FAILCOUNT: %d\n", failcount);
      else
        fprintf(logPtr, "===== FAILCOUNT: 0\n");
      fprintf(logPtr, "<<<<< TEST COMPLETED\n");
      fclose(logPtr);
    }
    do_exit(failcount);
    break;
  }
}

void enterTestMode()
{
  unsigned char receiveString, recent_bytes_fill = 0;
  int currentMessagePos;
  time_t currentTime;

  log_note("Entering unit test mode. Waiting for test results.");
  testname[0] = 0; // initialize test name with empty string
  testlog[0] = 0;
  receiveString = 0;
  failcount = 0;
  logPtr = NULL;

  currentTime = time(NULL);

  if (unittest_logfile) {

    logPtr = fopen(unittest_logfile, "a");
    if (!logPtr) {
      log_error("could not open logfile %s for appending. aborting", unittest_logfile);
      exit(127);
    }

    log_note("logging test results in %s", unittest_logfile);
    fprintf(logPtr, ">>>>> TEST: %s\n===== BITSTREAM: %s\n===== MODELCODE: %02X\n===== MODEL: %s\n===== ROM: %s\n", filename,
        system_bitstream_version, system_hardware_model, system_hardware_model_name, system_rom_version);
  }
  log_note("System model: %s", system_hardware_model_name);
  log_note("System CORE version: %s", system_bitstream_version);
  log_note("System ROM version: %s", system_rom_version);

  while (time(NULL) - currentTime < unit_test_timeout) {

    if (!wait_for_serial(WAIT_READ, unit_test_timeout >> 1, 0))
      continue;
    int b = serialport_read(fd, inbuf, 8192);

    for (int i = 0; i < b; i++) {

      // message receive mode: fill message buffer until end of string is reached
      if (receiveString) {
        msgbuf[currentMessagePos] = inbuf[i];

        // (ugly workaround: use pound sign as string end marker, because zeroes
        // sometimes get corrupted when using the serial line...)
        if (msgbuf[currentMessagePos] == 92) {
          msgbuf[currentMessagePos] = 0;
          receiveString = 0;
          // don't check recent_bytes_fill here! msg bytes are not
          // but into the buffer!
          if (recent_bytes[3] == 0xfd) { // log message to console
            strncpy(testlog, msgbuf, 160);
          }
          else if (recent_bytes[3] == 0xfe) { // set current test name
            strncpy(testname, msgbuf, 160);
          }
          bzero(recent_bytes, 4);
          recent_bytes_fill = 0;
        }

        currentMessagePos++;
      }
      else {
        recent_bytes[0] = recent_bytes[1];
        recent_bytes[1] = recent_bytes[2];
        recent_bytes[2] = recent_bytes[3];
        recent_bytes[3] = inbuf[i];
        // count if we have 4 bytes in recent_bytes
        if (recent_bytes_fill < 4)
          recent_bytes_fill++;
        // only check for vf011 request if buffer is full!
        if (recent_bytes_fill > 3)
          if (check_for_vf011_requests())
            recent_bytes_fill = 0;
      }
      handle_vf011_requests();

      // fprintf(stderr, "recent_bytes_fill: %d\n", recent_bytes_fill);
      // dump_bytes(0, "bytes", recent_bytes, 4);

      // not receiving a string? handle unit test token if needed
      if (!receiveString && recent_bytes_fill > 3) {
        // check if we should receive a string
        if (recent_bytes[3] == 0xfe || recent_bytes[3] == 0xfd) {
          // if we are starting to receive a new log line, and already have one, we need to output it
          if (recent_bytes[3] == 0xfd && testlog[0]) {
            currentTime = time(NULL);
            unit_test_logline(test_last_issue, test_last_sub, 0xd, testlog);
            testlog[0] = 0;
          }
          // receive message
          receiveString = 1;
          currentMessagePos = 0;
        }
        else if (recent_bytes[3] >= 0xf0) {
          // handle unit test token and update time
          currentTime = time(NULL);
          unit_test_log(recent_bytes);
          recent_bytes_fill = 0;
        }
      }
    }
  }

  log_error("timeout encountered while running tests. aborting.");
  if (logPtr) {
    fprintf(logPtr, "!!!!! TIMEOUT\n");
    fprintf(logPtr, "!!!!! FAILCOUNT: %d\n", failcount + 1);
    fprintf(logPtr, "<<<<< TEST COMPLETED\n");
    fclose(logPtr);
  }
  do_exit(UT_RES_TIMEOUT);
}

int main(int argc, char **argv)
{
  int opt_index;
  start_time = time(0);

  init_cmd_options();

  // so we can see errors while parsing args
  log_setup(stderr, LOG_NOTE);

  if (argc == 1)
    usage(-3, "No arguments given!");

  int opt;
  while ((opt = getopt_long(
              argc, argv, "@:14aA:B:b:q:c:C:d:DEFHf:jJ:Kk:Ll:MnNoprR:S::s:t:T:u::U:v:V:w:XZ:h0:", cmd_opts, &opt_index))
         != -1) {
    if (opt == 0) {
      if (opt_index >= cmd_log_start && opt_index < cmd_log_end)
        log_setup(stderr, loglevel);
      if (ethernet_cpulog && ethernet_video) {
        log_crit("Can't specify multiple ethernet streaming options!");
        exit(-3);
      }
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
    case 'D':
      debug_serial = 1;
      break;
    case 'h':
      usage(0, NULL);
    case '@':
      load_binary = optarg;
      wait_for_bitstream = 1;
      break;
    case 'a':
      show_audio_mixer = 1;
      break;
    case 'A':
      set_mixer_args = optarg;
    case 'N':
      no_cart = 1;
      break;
    case 'X':
      hyppo_report = 1;
      break;
    case 'K':
      fpgajtag_usbdk_enable = 1;
      break;
    case 'Z': {
      // Zap (reconfig) FPGA via MEGA65 reconfig registers
      sscanf(optarg, "%x", &zap_addr);
      zap = 1;
    } break;
    case 'B':
      sscanf(optarg, "%x", &break_point);
      break;
    case 'L':
      if (ethernet_video) {
        log_crit("Can't specify multiple ethernet streaming options!");
        exit(-3);
      }
      else {
        ethernet_cpulog = 1;
      }
      break;
    case 'E':
      if (ethernet_cpulog) {
        log_crit("Can't specify multiple ethernet streaming options!");
        exit(-3);
      }
      else {
        ethernet_video = 1;
      }
      break;
    case 'U':
      flashmenufile = strdup(optarg);
      check_file_access(optarg, "flash menu");
      break;
    case 'R':
      romfile = strdup(optarg);
      check_file_access(optarg, "ROM");
      break;
    case 'H':
      halt = 1;
      break;
    case 'C':
      charromfile = strdup(optarg);
      check_file_access(optarg, "char ROM");
      break;
    case 'c':
      colourramfile = strdup(optarg);
      check_file_access(optarg, "colour RAM");
      break;
    case '4':
      do_go64 = 1;
      wait_for_bitstream = 1;
      break;
    case '1':
      comma_eight_comma_one = 1;
      break;
    case 'p':
      pal_mode = 1;
      wait_for_bitstream = 1;
      break;
    case 'n':
      ntsc_mode = 1;
      wait_for_bitstream = 1;
      break;
    case 'F':
      reset_first = 1;
      break;
    case 'r':
      do_run = 1;
      wait_for_bitstream = 1;
      break;
    case 'f':
      jtag_serial = strdup(optarg);
      break;
    case 'l':
      serial_port = strdup(optarg);
      break;
    case 'M':
      mode_report = 1;
      break;
    case 'o':
      osk_enable = 1;
      break;
    case 'd':
      virtual_f011 = 1;
      d81file = strdup(optarg);
      break;
    case 's':
      serial_speed = atoi(optarg);
      switch (serial_speed) {
      case 1000000:
      case 1500000:
      case 4000000:
      case 230400:
      case 2000000:
        break;
      default:
        log_crit("Invalid serial speed %s!", serial_speed);
        exit(-3);
      }
      break;
    case 'S':
      screen_shot = 1;
      if (optarg != NULL)
        screen_shot_file = strdup(optarg);
      break;
    case 'b':
    case 'q':
      bitstream = strdup(optarg);
      if (bitstream[0] == '@')
        download_bitstream();
      check_file_access(bitstream, "bitstream");
      if (opt == 'q')
        bitstream_only = 1;
      break;
    case 'v':
      vivado_bat = strdup(optarg);
      break;
    case 'j':
      jtag_only = 1;
      break;
    case 'J':
      boundary_scan = 1;
      sscanf(optarg, "%[^,],%[^,],%s", boundary_xdc, boundary_bsdl, jtag_sensitivity);
      break;
    case 'V':
      set_vcd_file(optarg);
      break;
    case 'w':
      unittest_logfile = strdup(optarg);
      break;
    case 'k':
      hyppo = strdup(optarg);
      if (hyppo[0] == '@')
        download_hyppo();
      check_file_access(hyppo, "HYPPO");
      break;
    case 't':
    case 'T':
      type_text = strdup(optarg);
      if (opt == 'T')
        type_text_cr = 1;
      break;
    case 'u':
      unit_test_mode = 1;
      if (optarg == NULL)
        unit_test_timeout = UT_TIMEOUT;
      else {
        unit_test_timeout = strtol(optarg, &endp, 10);
        if (*endp != '\0') {
          log_error("-u option requires a numeric argument");
          exit(-1);
        }
        if (unit_test_timeout < UT_TIMEOUT)
          unit_test_timeout = UT_TIMEOUT;
      }
      wait_for_bitstream = 1;
      break;
    case 0x81: // memsave
    {
      char *next;
      if (!optarg)
        usage(-3, "failed to parse memsave address argument");
      if (strchr(optarg, ':') && strchr(optarg, '=')) { // got both range and filename
        if (sscanf(optarg, "%x:%x;", &memsave_start, &memsave_end) != 2)
          usage(-3, "failed to parse memsave address argument");
        next = strchr(optarg, '=') + 1;
        memsave_filename = strdup(next);
      }
      else if (strchr(optarg, ':') || strchr(optarg, '='))
        usage(-3, "failed to parse memsave argument (range without file?)");
      else {
        memsave_start = -1;
        memsave_end = -1;
        memsave_filename = strdup(optarg);
      }
    } break;
    default: // can not happen?
      usage(-3, "Unknown option.");
    }
  }

  if (argv[optind]) {
    filename = strdup(argv[optind]);
    check_file_access(filename, "programme");
    wait_for_bitstream = 1;
  }

  if (argc - optind > 1)
    usage(-3, "Unexpected extra commandline arguments.");

  log_debug("parameter parsing done");

  log_note("%s %s", TOOLNAME, version_string);

  /*
   * Automatically find the serial port
   *
   * This will use various source of information to determine which
   * serial port to use.
   *
   * It needs only be executed if:
   *  - user did not provide a serial_port (lets autodetect one)
   *  - user did request autodiscover (jtag_only)
   *  - user wants to push a bitstream to the device (requires jtag init)
   *
   * If a bitstream is provided, it's fpga_id is extracted and used to select
   * the proper device.
   * If a serial port is given, this will be used to select the device.
   * If a JTAG serial id is given, this is used to select the USB device.
   * If multiple selectors are given, all muss match!
   *
   * This is done by init_fpgajtag, which returns the device as a string.
   */
  if (bitstream || jtag_only || !serial_port) {
    unsigned int fpga_id = get_bitstream_fpgaid(bitstream);

    char *detected_port = init_fpgajtag(jtag_serial, serial_port, fpga_id);

#ifndef WINDOWS
    // this is set by fpgajtag/util.c:fpgausb_init which is called by fpgajtag/fpgajtag.c:init_fpgajtag
    if (fpgajtag_libusb_open_failed) {
      log_warn("May not be able to auto-detect USB port due to insufficient permissions.");
      log_warn("    You may be able to solve this problem via the following:");
      log_warn("        sudo usermod -a -G dialout <your username>");
      log_warn("    and then:");
      log_warn("        echo 'ACTION==\"add\", ATTRS{idVendor}==\"0403\", ATTRS{idProduct}==\"6010\", GROUP=\"dialout\", "
               "MODE=\"0666\"' | "
               "sudo tee /etc/udev/rules.d/40-xilinx.rules");
      log_warn("    and then log out, and log back in again, or failing that, reboot your computer and try again.");
    }
#endif

    // check if init_fpgajtag did totally fail
    if (detected_port == NULL) {
      if (!jtag_only)
        log_note("tip: try using 'm65 --autodiscover --verbose'");
      if (bitstream) {
        log_crit("you supplied a bitstream for a different MEGA65 model than is connected");
        log_crit("please use the correct bitstream for your device");
      }
      else
        log_crit("no matching jtag port found, aborting");
      exit(1);
    }

    // check if a UART was matched to JTAG (detected_port != UNKNOWN)
    if (strcmp(detected_port, "UNKNOWN")) {
      // we did detect a serial device
      if (serial_port) {
        // but the user also gave us --device
        if (strcmp(serial_port, detected_port))
          // serial_port != detected_port, we stick to the commandline but warn the user
          log_warn("autodetected port does not match --device argument: %s != %s", detected_port, serial_port);
        // if it is the same, we can just use keep it
      }
      else // user did not specify a serial_port, use detected one
        serial_port = detected_port;
    }
    // so here detected_port is UNKNOWN and we work with what the user has given
    // the UNKNOWN device was already reported by init_fpgajtag
  }

  if (boundary_scan) {
#ifdef WINDOWS
    log_warn("JTAG boundary scan not implemented on Windows.");
#else
    // Launch boundary scan in a separate thread, so that we can monitor signals while
    // running other operations.
    if (pthread_create(&threads[thread_count++], NULL, run_boundary_scan, NULL))
      log_error("failed to create JTAG boundary scan thread.");
    else
      log_note("JTAG boundary scan launched in separate thread.");
#endif
  }

  // autodiscover/jtag_only mode, abort here before doing anything
  if (jtag_only)
    do_exit(0);

  // -b Load bitstream if file provided
  if (bitstream) {
    load_bitstream(bitstream);
    if (bitstream_only)
      do_exit(0);
    if (wait_for_bitstream) {
      log_note("waiting for the system to settle...");
      sleep(4);
    }
  }

  if (!serial_port) {
    if (!jtag_only)
      log_note("tip: try using 'm65 --autodiscover --verbose'");
    log_crit("serial port not specified, aborting.");
    exit(1);
  }

  log_info("opening serial port %s", serial_port);
  if (open_the_serial_port(serial_port))
    exit(-1);
  xemu_flag = mega65_peek(0xffd360f) & 0x20 ? 0 : 1;

  rxbuff_detect();
  monitor_sync();

  if (debug_load_memory) {
    log_note("Testing load memory function.");
    unsigned char buf[65536];

#if 0
    // Overwrite C65 text screen
    mega65_poke(0xFFD3060,0x800>>0);
    mega65_poke(0xFFD3061,0x800>>8);
    mega65_poke(0xFFD3062,0x800>>16);
    mega65_poke(0xffd3054,0x00);
    mega65_poke(0xffd3031,0x80); // 80 columns
    for(int i=0;i<=256;i++) {
      for(int y=0;y<25;y++) for(int x=0;x<80;x++) buf[y*80+x]=x+i;
      push_ram(0x0800,2000,buf);
    }
#endif

    // Switch to a graphics mode, and do similar with graphics screen and larger transfers
    mega65_poke(0xffd3031, 0x00); // 40 columns
    mega65_poke(0xFFD3060, (char)(0x12000 >> 0));
    mega65_poke(0xFFD3061, (char)(0x12000 >> 8));
    mega65_poke(0xFFD3062, (char)(0x12000 >> 16));
    mega65_poke(0xffd3054, 0x05);

    // Setup screen memory in columns
    for (int y = 0; y < 25; y++)
      for (int x = 0; x < 40; x++) {
        buf[y * 40 * 2 + x * 2 + 0] = (0x1000 + y + x * 25) >> 0;
        buf[y * 40 * 2 + x * 2 + 1] = (0x1000 + y + x * 25) >> 8;
      }
    push_ram(0x12000L, 4000, buf);
    for (int y = 0; y < 25; y++)
      for (int x = 0; x < 40; x++) {
        buf[y * 40 * 2 + x * 2 + 0] = 0x00;
        buf[y * 40 * 2 + x * 2 + 1] = 0x00;
      }
    push_ram(0xff80000L, 4000, buf);

    for (int i = 0; 319; i++) {
      for (int x = 0; x < 320; x++) {
        for (int y = 0; y < 200; y++) {
          buf[y * 8 + (x >> 3) * 64 * 25 + (x & 7)] = i + x;
        }
      }
      push_ram(0x40000L, 320 * 200, buf);
    }

    exit(0);
  }

  if (zap) {
    if (zap_addr < 8) {
      int slot = zap_addr;
      zap_addr = slot * 0x800000;
      log_note("Reconfiguring FPGA using core in slot %d ($%08x)", slot, zap_addr);
    }
    else {
      if (zap_addr % 0x800000)
        log_warn("zap address not aligned to $800000");
      log_note("Reconfiguring FPGA using core at $%08x (slot %d)", zap_addr, zap_addr / 0x800000);
    }
    char cmd[1024];
    monitor_sync();
    // addr needs to be shifted right by 8!
    snprintf(
        cmd, 1024, "sffd36c8 %x %x %x %x\r", (zap_addr >> 8) & 0xff, (zap_addr >> 16) & 0xff, (zap_addr >> 24) & 0xff, 0);
    slow_write(fd, cmd, strlen(cmd));
    monitor_sync();
    mega65_poke(0xffd36cf, 0x42);
    log_note("waiting for the system to settle...");
    // XXX This can take a while, which we should accommodate
    if (wait_for_bitstream) {
      sleep(4);
    }
    monitor_sync();
  }

  // fetch version information
  if (unit_test_mode) {
    get_system_bitstream_version();
    get_system_rom_version();
  }

  // let's save as soon as possible, because first
  // loading an then saving makes no sense, right?
  if (memsave_filename) {
    log_info("detecting C64/C65 mode status");
    detect_mode();
    if (memory_save(memsave_start, memsave_end, memsave_filename))
      do_exit(-1);
  }

  if (virtual_f011)
    log_note("vf011 - remote access to disk image '%s' requested", d81file);

  if (hyppo_report)
    show_hyppo_report();

  // If we have no HYPPO file provided, but need one, then
  // extract one out of the running bitstream.
  if (!hyppo) {
    if (virtual_f011) {
      log_info("extracting HYPPO from running system...");
      unsigned char hyppo_data[0x4000];
      fetch_ram(0xFFF8000, 0x4000, hyppo_data);
#ifdef WINDOWS
      char *temp_name = "HYPPOEXT.M65";
#else
      char *temp_name = "/tmp/HYPPOEXT.M65";
#endif
      FILE *f = fopen(temp_name, "wb");
      if (!f) {
        log_crit("could not create temporary HYPPO file.");
        exit(-1);
      }
      fwrite(hyppo_data, 0x4000, 1, f);
      fclose(f);
      hyppo = strdup(temp_name);
    }
  }

  if (!hyppo) {

    // XXX These two need the CPU to be in hypervisor mode
    if (romfile || charromfile) {
      enter_hypervisor_mode();
      if (romfile) {
        // Un-protect
        log_info("replacing rom");
        mega65_poke(0xffd367d, mega65_peek(0xffd367d) & (0xff - 4));

        load_file(romfile, 0x20000, 0);
        // reenable ROM write protect
        mega65_poke(0xffd367d, mega65_peek(0xffd367d) | 0x04);
      }
      if (charromfile) {
        log_info("replacing colourr ram");
        load_file(charromfile, 0xFF7E000, 0);
      }
      return_from_hypervisor_mode();
    }

    if (colourramfile) {
      log_info("replacing colourram");
      load_file(colourramfile, 0xFF80000, 0);
    }
    if (flashmenufile) {
      log_info("replacing flashmenu");
      load_file(flashmenufile, 0x50000, 0);
    }
  }
  else {
    int patchKS = 0;
    if (romfile && (!flashmenufile))
      patchKS = 1;

    real_stop_cpu();
    if (hyppo) {
      log_info("replacing hyppo...");
      real_stop_cpu();
      load_file(hyppo, 0xfff8000, patchKS);
    }
    if (flashmenufile) {
      log_info("replacing flashmenu");
      load_file(flashmenufile, 0x50000, 0);
    }
    if (romfile) {
      log_info("replacing rom");
      load_file(romfile, 0x20000, 0);
    }
    if (charromfile) {
      log_info("replacing character rom");
      load_file(charromfile, 0xFF7E000, 0);
    }
    if (colourramfile) {
      log_info("replacing colourram");
      load_file(colourramfile, 0xFF80000, 0);
    }
    if (virtual_f011) {
      log_note("virtualising F011 FDC access");

      // Enable FDC virtualisation
      mega65_poke(0xffd3659, 0x01);
      // Enable disk 0 (including for write)
      mega65_poke(0xffd368b, 0x03);
    }
    if (!reset_first)
      start_cpu();
  }

  // -F reset
  if (reset_first) {
    log_note("resetting MEGA65");
    start_cpu();
    slow_write(fd, "\r!\r", 3);
    monitor_sync();
    sleep(2);
  }

  if (no_cart) {
    char cmd[1024];

    for (int i = 0; i < 2; i++) {
      real_stop_cpu();
      mega65_poke(0xffd37d, 0x00); // disable cartridge
      // bank ROM in
      mega65_poke(0x0, 0x37);
      mega65_poke(0x1, 0x37);
      // The sequence below traps if an IRQ happens during the early reset sequence
      snprintf(cmd, 1024, "gfce2\r");
      slow_write(fd, cmd, strlen(cmd));
      snprintf(cmd, 1024, "\r");
      slow_write(fd, cmd, strlen(cmd));
      slow_write(fd, cmd, strlen(cmd));
      slow_write(fd, cmd, strlen(cmd));
      slow_write(fd, cmd, strlen(cmd));
      slow_write(fd, cmd, strlen(cmd));
      slow_write(fd, cmd, strlen(cmd));
      slow_write(fd, cmd, strlen(cmd));
      slow_write(fd, cmd, strlen(cmd));
      snprintf(cmd, 1024, "gfce2\r");
      slow_write(fd, cmd, strlen(cmd));
      start_cpu();
      usleep(50000);
    }
  }

  if (break_point != -1) {
    log_note("setting CPU breakpoint at $%04x", break_point);
    char cmd[1024];
    sprintf(cmd, "b%x\r", break_point);
    do_usleep(20000);
    slow_write(fd, cmd, strlen(cmd));
    do_exit(0);
  }

  if (pal_mode) {
    log_info("switching to PAL mode");
    mega65_poke(0xFFD306fL, 0x00);
    mega65_poke(0xFFD3072L, 0x00);
    mega65_poke(0xFFD3048L, 0x68);
    mega65_poke(0xFFD3049L, 0x0 | (mega65_peek(0xFFD3049L) & 0xf0));
    mega65_poke(0xFFD304AL, 0xF8);
    mega65_poke(0xFFD304BL, 0x1 | (mega65_peek(0xFFD304BL) & 0xf0));
    mega65_poke(0xFFD304EL, 0x68);
    mega65_poke(0xFFD304FL, 0x0 | (mega65_peek(0xFFD304FL) & 0xf0));
    mega65_poke(0xFFD3072L, 0);
    // switch CIA TOD 50/60
    mega65_poke(0xffd3c0el, mega65_peek(0xffd3c0el) | 0x80);
    mega65_poke(0xffd3d0el, mega65_peek(0xffd3d0el) | 0x80);
  }
  else if (ntsc_mode) {
    log_info("switching to NTSC mode");
    mega65_poke(0xFFD306fL, 0x87);
    mega65_poke(0xFFD3072L, 0x18);
    mega65_poke(0xFFD3048L, 0x2A);
    mega65_poke(0xFFD3049L, 0x0 | (mega65_peek(0xFFD3049L) & 0xf0));
    mega65_poke(0xFFD304AL, 0xB9);
    mega65_poke(0xFFD304BL, 0x1 | (mega65_peek(0xFFD304BL) & 0xf0));
    mega65_poke(0xFFD304EL, 0x2A);
    mega65_poke(0xFFD304FL, 0x0 | (mega65_peek(0xFFD304FL) & 0xf0));
    mega65_poke(0xFFD3072L, 24);
    // switch CIA TOD 50/60
    mega65_poke(0xffd3c0el, mega65_peek(0xffd3c0el) & 0x7f);
    mega65_poke(0xffd3d0el, mega65_peek(0xffd3d0el) & 0x7f);
  }

  if (ethernet_video) {
    log_note("enabling ethernet video");
    mega65_poke(0xffd36e1, 0x29);
  }
  else if (ethernet_cpulog) {
    log_note("enabling ethernet cpulog");
    mega65_poke(0xffd36e1, 0x05);
  }

  /*
    Show image if requested.
    We expect a file in the "logo" mode of pngprepare here.
    These files have palette values followed by tiles, without de-duplication.
    This means 640x480 requires ~300KB, plus the 9.6K screen and 9.6K colour data.

  */
  if (load_binary) {

    char filename[1024];
    int load_addr = 0;

    if (sscanf(load_binary, "%[^@]@%x", filename, &load_addr) != 2) {
      log_crit("-@ option format is file@hexaddr");
      usage(-3, "-@ option format is file@hexaddr");
    }

    enter_hypervisor_mode();

    // Un-protect ROM area
    mega65_poke(0xffd367d, mega65_peek(0xffd367d) & (0xff - 4));

    load_file(filename, load_addr, 0);
    log_note("loaded file '%s' @ $%x", filename, load_addr);

    return_from_hypervisor_mode();
  }

  /*
    Show / set audio coefficient values.
  */
  if (set_mixer_args) {
    // Parse HEX=percent, ... where HEX is a two-digit even hex address of the
    // coefficient.
    int ofs = 0;
    while (ofs < strlen(set_mixer_args)) {
      int n, coeff, coeffhi, percent;
      while (set_mixer_args[ofs] == ',')
        ofs++;
      if ((sscanf(&set_mixer_args[ofs], "%x-%x=%d%n", &coeff, &coeffhi, &percent, &n) >= 3)) {
        fprintf(stderr, "ofs=%d, n=%d\n", ofs, n);
        ofs += n;
        if (coeffhi < coeff)
          coeffhi = coeff;
        int val = percent * 655;
        while (coeff <= coeffhi) {
          mega65_poke(0xffd36f4, coeff + 0);
          mega65_poke(0xffd36f5, val & 0xff);
          mega65_poke(0xffd36f4, coeff + 1);
          mega65_poke(0xffd36f5, val >> 8);
          fprintf(stderr, "Setting audio coefficient $%02x to %04x\n", coeff, val);
          coeff += 2;
        }
      }
      else if ((sscanf(&set_mixer_args[ofs], "%x=%d%n", &coeff, &percent, &n) >= 2)) {
        ofs += n;
        int val = percent * 655;
        mega65_poke(0xffd36f4, coeff + 0);
        mega65_poke(0xffd36f5, val & 0xff);
        mega65_poke(0xffd36f4, coeff + 1);
        mega65_poke(0xffd36f5, val >> 8);
        fprintf(stderr, "Setting audio coefficient $%02x to %04x\n", coeff, val);
      }
    }
  }

  if (show_audio_mixer) {
    monitor_sync();
    fprintf(stderr, "Reading audio mixer coefficients (takes several seconds).\n");
    unsigned char mixer_coefficients[256];
    for (int i = 0; i < 256; i++) {
      mega65_poke(0xffd36f4, i);
      //	fprintf(stderr,"poked %d\n",i);
      mixer_coefficients[i] = mega65_peek(0xffd36f5);
      //	fprintf(stderr,"peeked\n");
      fprintf(stderr, ".");
    }
    fprintf(stderr, "\n");
    for (int i = 0; i < 256; i += 32) {
      switch (i) {
      case 0x00:
        fprintf(stderr, "Speaker Left:\n");
        break;
      case 0x20:
        fprintf(stderr, "Speaker Right:\n");
        break;
      case 0x40:
        fprintf(stderr, "Telephone 1:\n");
        break;
      case 0x60:
        fprintf(stderr, "Telephone 2:\n");
        break;
      case 0x80:
        fprintf(stderr, "Bluetooth Left:\n");
        break;
      case 0xA0:
        fprintf(stderr, "Bluetooth Right:\n");
        break;
      case 0xC0:
        fprintf(stderr, "Headphones Right:\n");
        break;
      case 0xE0:
        fprintf(stderr, "Headphones Left:\n");
        break;
      }
      for (int j = 0; j < 32; j += 2) {
        fprintf(stderr, "%02x ", i + j);
        switch (j) {
        case 0:
          fprintf(stderr, "            SID Left: ");
          break;
        case 2:
          fprintf(stderr, "           SID Right: ");
          break;
        case 4:
          fprintf(stderr, "     Telephone 1 Mic: ");
          break;
        case 6:
          fprintf(stderr, "     Telephone 2 Mic: ");
          break;
        case 8:
          fprintf(stderr, "      Bluetooth Left: ");
          break;
        case 10:
          fprintf(stderr, "     Bluetooth Right: ");
          break;
        case 12:
          fprintf(stderr, "        Line-In Left: ");
          break;
        case 14:
          fprintf(stderr, "       Line-In Right: ");
          break;
        case 16:
          fprintf(stderr, "           Digi Left: ");
          break;
        case 18:
          fprintf(stderr, "          Digi Right: ");
          break;
        case 20:
          fprintf(stderr, "   Microphone 0 Left: ");
          break;
        case 22:
          fprintf(stderr, "  Microphone 0 Right: ");
          break;
        case 24:
          fprintf(stderr, "   Microphone 1 Left: ");
          break;
        case 26:
          fprintf(stderr, "  Microphone 1 Right: ");
          break;
        case 28:
          fprintf(stderr, "              OPL FM: ");
          break;
        case 30:
          fprintf(stderr, "       Master Volume: ");
          break;
        }
        fprintf(stderr, " %03d%%\n", (mixer_coefficients[i + j] + (mixer_coefficients[i + j + 1] << 8)) / 655);
      }
    }
  }

  if (filename || do_go64) {
    log_info("detecting C64/C65 mode status");
    detect_mode();
  }

  // -4 Switch to C64 mode
  if ((!saw_c64_mode) && do_go64) {
    switch_to_c64mode();
    // the systems needs a bit to switch over
    usleep(200000);
  }

  if (type_text)
    do_type_text(type_text);

#if 0
  // Increase serial speed, if possible
#ifndef WINDOWS
  if (virtual_f011&&serial_speed==2000000) {
    // Try bumping up to 4mbit
    slow_write(fd,"\r+9\r",4);
    set_serial_speed(fd,4000000);
    serial_speed=4000000;
  }
#endif
#endif

  // OSK enable
  if (osk_enable) {
    mega65_poke(0xffd361f, 0xff);
    log_note("OSK enabled");
  }

  // -S screen shot
  if (screen_shot) {
    real_stop_cpu();
    do_screen_shot(screen_shot_file);
    start_cpu();
    do_exit(0);
  }

  if (filename) {
    unsigned char sid_header[0x7e];
    int is_sid_tune = 0;

    log_note("loading file '%s'", filename);

    // Next we just load the file
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
      log_crit("could not find file '%s'", filename);
      exit(-1);
    }
    else {
      char buffer[40];
      unsigned char SIDmagic1[5] = { 0x50, 0x53, 0x49, 0x44, 0x00 };
      unsigned char SIDmagic2[5] = { 0x52, 0x53, 0x49, 0x44, 0x00 };

      // load full SID header here!
      if (fread(sid_header, 1, 0x7e, f) != 0x7e) {
        log_debug("short read while trying to get SID header, not SID file!");
        memset(sid_header, 0, sizeof(sid_header));
      }
      //	dump_bytes(0,"sid header",sid_header,0x7c);

      // seek back just after load address
      fseek(f, 2, SEEK_SET);

      int load_addr = sid_header[0] | (sid_header[1]<<8);

      // check for magic SID header, last byte needs to be 1-4, those are the valid versions
      if ((memcmp(sid_header, SIDmagic1, 5) && memcmp(sid_header, SIDmagic2, 5)) || (sid_header[5] < 0 || sid_header[5] > 4)) {
        if (!comma_eight_comma_one) {
          if (saw_c64_mode)
            load_addr = 0x0801;
          else
            load_addr = 0x2001;
          log_info("forcing load address to $%04X", load_addr);
        }
        else {
          if (saw_c64_mode && load_addr == 0x2001)
            log_warn("loading BASIC65 PRG in C64 mode!");
          else if (!saw_c64_mode && load_addr == 0x0801)
            log_warn("loading C64 BASIC PRG in C65 mode!");
          log_info("load address is $%04x", load_addr);
        }
        // write user feedback to the screen
        snprintf(buffer, 40, "REM M65 INJECTING AT $%04X:\r", load_addr);
        stuff_keybuffer(buffer);
        usleep(20000);
      }
      else {
        // give some feedback about found magic
        log_note("detected %s v%d tune", sid_header, sid_header[5]);

        // Player is for C64 mode!
        if (!saw_c64_mode) {
          switch_to_c64mode();
          // the systems needs a bit to switch over
          usleep(200000);
        }

        is_sid_tune = 1;
      }

      real_stop_cpu();

      if (is_sid_tune) {
        log_info("loading SID header...");

        // SID header was alread read above!

        // SID version 2+ has 0x7c bytes of header
        // we already did read the first two, so 0x7c reads
        // also the first 2 bytes == load_addr of the data/prg!
        // if it is a SID version 1, header is only 0x76,
        // as specified in the word at 0x6 of the header

        unsigned int end_of_sid_header = (sid_header[0x06] << 8) + sid_header[0x07];
        unsigned int start_addr = (sid_header[0x0a] << 8) + sid_header[0x0b];
        unsigned int play_addr = (sid_header[0x0c] << 8) + sid_header[0x0d];
        //	unsigned int play_speed=sid_header[0x12];

        char *name = (char *)&sid_header[0x16];
        char *author = (char *)&sid_header[0x36];
        char *released = (char *)&sid_header[0x56];

        log_note("found SID tune '%s' by '%s' (%s)", name, author, released);

        // Also show player info on the screen
        // clang-format off
        char player_screen[1000] = { "                                        "
                                     "                                        "
                                     " M65 TOOL CRUSTY SID PLAYER V0.01 ALPHA "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "  Track                                 "
                                     "    0                                   "
                                     "                                        "
                                     "  Name                                  "
                                     "                                        "
                                     "                                        "
                                     "  Artist                                "
                                     "                                        "
                                     "                                        "
                                     "  Copyright                             "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "          0 - 9 = SELECT TRACK          "
                                     "                                        "
                                     "                                        " };
        // clang-format on
        for (int i = 0; i < 32 && name[i]; i++)
          player_screen[40 * 11 + 4 + i] = name[i];
        for (int i = 0; i < 32 && author[i]; i++)
          player_screen[40 * 14 + 4 + i] = author[i];
        for (int i = 0; i < 32 && released[i]; i++)
          player_screen[40 * 17 + 4 + i] = released[i];

        // convert to screen codes
        for (int i=0; i < 920; i++)
          player_screen[i] = wincp1252_to_screen(player_screen[i]) | ((i < 5*40 || i >= 20*40) ? 0x80 : 0x00);

        // hide code on screen (blue on blue)
        memset(player_screen + 920, 6, 80);
        push_ram(0xff802d0L, 80, (unsigned char *)player_screen + 920);
        memset(player_screen + 920, 0xa0, 80);

        push_ram(0x0400, 1000, (unsigned char *)player_screen);

        // fetch load address from the two bytes after the end_of_sid_header
        load_addr = (sid_header[end_of_sid_header + 1] << 8) + sid_header[end_of_sid_header];
        log_debug("SID load address is $%04x", load_addr);

        // Prepare simple play routine
        // XXX For now it is always VIC frame locked
        log_note("uploading SID play routine");

        // clang-format off
        /*
                  SEI
                  LDA #$17
                  STA $d018     // lowercase
                  LDA #$35
                  STA $01       // get rid of rom
                  LDA #$00      // song number
        player_init:
                  JSR $1234     // call SID initAddress
        after_init:
                  LDA #$80
        raster1:
                  CMP $d012
                  BNE raster1   // wait for raster
                  LDA #$01
                  STA $d020     // white border
                  JSR $5678     // call SID playAddress
                  LDA #$00
                  STA $d020     // black border
                  LDA #$80
        raster2:
                  CMP $d012
                  BNE raster2   // wait for raster
                  LDA $d610
                  BEQ after_init
                  STA $d610     // got a keypress
                  AND #$0f      // we only want 0-9 but we don't care about the key
                  ORA #$30      // make it a screencode
                  STA $0944     // and write it to the screen
                  AND #$0f      // back to 0-9
                  BRA player_init  // init player again
                  // this is endless!
        */
        unsigned char player[64] = {
          0x78, 0xa9, 0x17, 0x8d, 0x18, 0xd0, 0xa9, 0x35, 0x85, 0x01, 0xa9, 0x00, 0x20, 0x34, 0x12, 0xa9,
          0x80, 0xcd, 0x12, 0xd0, 0xd0, 0xfb, 0xa9, 0x01, 0x8d, 0x20, 0xd0, 0x20, 0x78, 0x56, 0xa9, 0x00,
          0x8d, 0x20, 0xd0, 0xa9, 0x80, 0xcd, 0x12, 0xd0, 0xd0, 0xfb, 0xad, 0x10, 0xd6, 0xf0, 0xe0, 0x8d,
          0x10, 0xd6, 0x29, 0x0f, 0x09, 0x30, 0x8d, 0x44, 0x05, 0x29, 0x0f, 0x80, 0xcf, 0x00, 0x00, 0x00
        };
        // clang-format on

        // patch all kind of stuff into player code
        player[11] = sid_header[0x11] - 1;

        if (start_addr) {
          player[13] = (start_addr >> 0) & 0xff;
          player[14] = (start_addr >> 8) & 0xff;
        }
        else {
          player[12] = 0xb8; // CLV is safer
          player[13] = 0xb8;
          player[14] = 0xb8;
        }
        if (play_addr) {
          player[28] = (play_addr >> 0) & 0xff;
          player[29] = (play_addr >> 8) & 0xff;
        }
        else {
          player[27] = 0xb8;
          player[28] = 0xb8;
          player[29] = 0xb8;
        }

        // Enable M65 IO for keyboard scanning
        slow_write(fd, "sffd302f 47\n", 12);
        slow_write(fd, "sffd302f 53\n", 12);

        // push to tape buffer
        push_ram(0x06d0, 64, player);

        // seek to start of SID prg (after end_of_sid_header + 2 bytes of load_addr)
        fseek(f, end_of_sid_header + 2, SEEK_SET);
      }

      unsigned char buf[32768];
      int max_bytes = 32768;
      int b = fread(buf, 1, max_bytes, f);
      while (b > 0) {
        log_debug("read block for $%04x -- $%04x (%d bytes)", load_addr, load_addr + b - 1, b);

        if (is_sid_tune) {
          int num_sids = 0;
          int sid_addrs[256];
          int fix_addrs[256];
          int this_sid = 0;
          for (int i = 0; i < b; i++) {
            switch (buf[i]) {
            case 0xD4:
            case 0xD5:
            case 0xD6:
            case 0xDE:
            case 0xDF:
              // Possible SID addresses
              // Check if opcode is an absolute load or store
              // If so, note the SID address, so we can reallocate any
              // that are out of range etc
              if (i >= 2) {
                // Look for absolute store instructions
                switch (buf[i - 2]) {
                case 0x8D: //   STA $nnnn
                case 0x99: //   STA $nnnn,Y
                case 0x9D: //  STA $nnnn,X
                case 0x8E: //  STX $nnnn
                case 0x8C: //  STY $nnnn
                  this_sid = buf[i] << 8;
                  this_sid |= buf[i - 1];
                  this_sid &= 0xffe0;
                  int j = 0;
                  for (j = 0; j < num_sids; j++)
                    if (this_sid == sid_addrs[j])
                      break;
                  if (j == num_sids) {
                    sid_addrs[num_sids++] = this_sid;
                    log_info("tune uses SID at $%04x", this_sid);
                  }
                }
              }
              break;
            }
          }
          log_info("tune uses a total of %d SIDs", num_sids);
          for (int i = 0; i < num_sids; i++) {
            if (sid_addrs[i] >= 0xd600) {
              fix_addrs[i] = 0xd400 + 0x20 * i;
              log_info("relocating SID at $%02x to $%04x", sid_addrs[i], fix_addrs[i]);
            }
            else
              fix_addrs[i] = sid_addrs[i];
          }
          for (int i = 0; i < b; i++) {
            switch (buf[i]) {
            case 0xD4:
            case 0xD5:
            case 0xD6:
            case 0xDE:
            case 0xDF:
              // Possible SID addresses
              // Check if opcode is an absolute load or store
              // If so, note the SID address, so we can reallocate any
              // that are out of range etc
              if (i >= 2) {
                // Look for absolute store instructions
                switch (buf[i - 2]) {
                case 0x8D: //   STA $nnnn
                case 0x99: //   STA $nnnn,Y
                case 0x9D: //  STA $nnnn,X
                case 0x8E: //  STX $nnnn
                case 0x8C: //  STY $nnnn
                  this_sid = buf[i] << 8;
                  this_sid |= buf[i - 1];

                  int j = 0;
                  for (j = 0; j < num_sids; j++)
                    if ((this_sid & 0xffe0) == sid_addrs[j])
                      break;
                  if (fix_addrs[j] != sid_addrs[j]) {
                    log_debug("@ $%04X Patching $%04X to $%04X", i + load_addr, this_sid, fix_addrs[j] | (this_sid & 0x1f));
                    int fixed_addr = fix_addrs[j] | (this_sid & 0x1f);
                    buf[i - 1] = fixed_addr & 0xff;
                    buf[i] = fixed_addr >> 8;
                  }
                }
              }
              break;
            }
          }
        }

        push_ram(load_addr, b, buf);

        load_addr += b;
        b = fread(buf, 1, max_bytes, f);
      }
      fclose(f);
      f = NULL;

      // set end address, clear input buffer, release break point,
      // jump to end of load routine, resume CPU at a CLC, RTS
      monitor_sync();

      // Set end of memory pointer
      if (!comma_eight_comma_one) {
        int top_of_mem_ptr_addr = 0x2d;
        if (saw_c65_mode)
          top_of_mem_ptr_addr = 0x82;
        unsigned char load_addr_bytes[2];
        load_addr_bytes[0] = load_addr;
        load_addr_bytes[1] = load_addr >> 8;
        push_ram(top_of_mem_ptr_addr, 2, load_addr_bytes);
        log_info("storing end of program pointer $%04x at $%02x", load_addr, top_of_mem_ptr_addr);
      }

      // start the simple SID Player
      if (is_sid_tune) {
        slow_write(fd, "g06d0\r", 6);
        monitor_sync();
        start_cpu();
        log_note("SID player started");
        do_exit(0);
      }

      if (!halt) {
        start_cpu();
      }

      // we need to clean up serial here, before we actually start the unit test
      // to get rid of the stuff that was send before
      if (unit_test_mode)
        monitor_sync();

      if (do_run) {
        stuff_keybuffer("RUN:\r");
        log_note("running");
      }
      else
        // loaded ok.
        log_note("loaded");
    }
  }

  if (unit_test_mode)
    enterTestMode();

  // XXX - loop for virtualisation, JTAG boundary scanning etc

  if (virtual_f011) {
    unsigned char buff[8192];
    int b, i;

    log_raiselevel(LOG_NOTE);
#ifndef WINDOWS
    log_note("entering virtualised F011 wait loop... press 'q' plus <RETURN> to exit");
#else
    log_note("entering virtualised F011 wait loop...");
#endif
    log_warn("resetting the system might render your D81 image unusable!");
    while (1) {
#ifndef WINDOWS
      fd_set read_set;
      FD_SET(fd, &read_set);
      FD_SET(STDIN_FILENO, &read_set);
      if (select(fd + 1, &read_set, NULL, NULL, NULL) < 1) {
        log_debug("vF011: select false");
        continue;
      }
      else
        log_debug("vF011: select true");
      if (FD_ISSET(STDIN_FILENO, &read_set) && fgetc(stdin) == 'q') {
        log_crit("exit requested, please power cycle your MEGA65");
        break;
      }
      if (!FD_ISSET(fd, &read_set))
        continue;
#endif

      b = serialport_read(fd, buff, 8192);
      // if (b > 0) dump_bytes(2, "VF011 wait", buff, b);

      /*
       * after reset the system will search for "MEDA65.ROM", so that
       * already might be a boiler plate? put the bytes that get read on
       * reset can be interpreted as write requests by check_for_vf011_requests
       * and might kill the D81 image... so better bail out here!
       */
      if (b > 64 && strstr((char *)buff, "MEGA65 Serial Monitor")) {
        log_crit("reset detected, please power cycle your MEGA65");
        break;
      }

      for (i = 0; i < b; i++) {
        recent_bytes[0] = recent_bytes[1];
        recent_bytes[1] = recent_bytes[2];
        recent_bytes[2] = recent_bytes[3];
        recent_bytes[3] = buff[i];
        check_for_vf011_requests();
      }
      handle_vf011_requests();
    }
    if (fd81 != NULL) {
      log_debug("closing d81 image file");
      fclose(fd81);
    }
    // disable vF011
    mega65_poke(0xffd3659, 0x00);
    mega65_poke(0xffd368b, 0x07);
    do_exit(0);
  }
  do_exit(0);
}

void do_exit(int retval)
{
#ifndef WINDOWS
  if (thread_count) {
    log_crit("background tasks may be running. CONTROL+C to stop...");
    for (int i = 0; i < thread_count; i++)
      pthread_join(threads[i], NULL);
  }
#endif
  close_communication_port();
  exit(retval);
}
