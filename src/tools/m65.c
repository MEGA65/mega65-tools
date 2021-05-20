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

#include "m65common.h"

extern int pending_vf011_read;
extern int pending_vf011_write;
extern int pending_vf011_device;
extern int pending_vf011_track;
extern int pending_vf011_sector;
extern int pending_vf011_side;

extern int debug_serial;

extern unsigned char recent_bytes[4];

int osk_enable = 0;

int not_already_loaded = 1;

int unit_test_mode=0;

int halt = 0;

int usedk = 0;

char* load_binary = NULL;

int viciv_mode_report(unsigned char* viciv_regs);

int do_screen_shot(void);
int fpgajtag_main(char* bitstream, char* serialport);
void init_fpgajtag(const char* serialno, const char* filename, uint32_t file_idcode);
int xilinx_boundaryscan(char* xdc, char* bsdl, char* sensitivity);
void set_vcd_file(char* name);
void do_exit(int retval);

void usage(void)
{
  fprintf(stderr, "MEGA65 cross-development tool.\n");
  fprintf(stderr, "usage: m65 [-l <serial port>] [-s <230400|2000000|4000000>]  [-b <FPGA bitstream> [-v <vivado.bat>] [[-k "
                  "<hickup file>] [-R romfile] [-U flashmenufile] [-C charromfile]] [-c COLOURRAM.BIN] [-B breakpoint] [-a] "
                  "[-A <xx[-yy]=ppp>] [-o] [-d diskimage.d81] [-j] [-J <XDC,BSDL[,sensitivity list]> [-V <vcd file>]] [[-1] "
                  "[<-t|-T> <text>] [-f FPGA serial ID] [filename]] [-H] [-E|-L] [-Z <flash addr>] [-@ file@addr] [-N] [-u]\n");
  fprintf(stderr, "  -@ - Load a binary file at a specific address.\n");
  fprintf(stderr, "  -1 - Load as with ,8,1 taking the load address from the program, instead of assuming $0801\n");
  fprintf(stderr, "  -4 - Switch to C64 mode before exiting.\n");
  fprintf(stderr, "  -A - Set audio coefficient(s) xx (and optionally to yy) to ppp percent of maximum volume.\n");
  fprintf(stderr, "  -a - Read and display audio cross-bar mixer status.\n");
  fprintf(stderr, "  -B - Set a breakpoint on synchronising, and then immediately exit.\n");
  fprintf(stderr, "  -b - Name of bitstream file to load.\n");
  fprintf(stderr, "  -C - Character ROM file to preload.\n");
  fprintf(stderr, "  -c - Colour RAM contents to preload.\n");
  fprintf(stderr, "  -d - Enable virtual D81 access\n");
  fprintf(stderr, "  -E - Enable streaming of video via ethernet.\n");
  fprintf(stderr, "  -F - Force reset on start\n");
  fprintf(stderr, "  -f - Specify which FPGA to reconfigure when calling fpgajtag\n");
  fprintf(stderr, "  -H - Halt CPU after loading ROMs.\n");
  fprintf(stderr, "  -J - Do JTAG boundary scan of attached FPGA, using the provided XDC and BSDL files.\n");
  fprintf(stderr, "       A sensitivity list can also be provided, to restrict the set of signals monitored.\n");
  fprintf(stderr, "       This will likely be required when producing VCD files, as they can only log ~80 signals.\n");
  fprintf(stderr, "  -j   Do JTAG operation(s), and nothing else.\n");
  fprintf(stderr, "  -K - Use DK backend for libUSB, if available\n");
  fprintf(stderr, "  -k - Name of hickup file to forcibly use instead of the HYPPO in the bitstream.\n");
  fprintf(stderr, "       NOTE: You can use bitstream and/or HYPPO from the Jenkins server by using @issue/tag/hardware\n"
                  "             for the bitstream, and @issue/tag for HYPPO.\n");
  fprintf(stderr, "  -L - Enable streaming of CPU instruction log via ethernet.\n");
  fprintf(stderr, "  -l - Name of serial port to use, e.g., /dev/ttyUSB1\n");
  fprintf(stderr, "  -o - Enable on-screen keyboard\n");
  fprintf(stderr, "  -N - Disable a running cartridge, and boot to C64 mode.\n");
  fprintf(stderr, "  -n - Force NTSC video mode\n");
  fprintf(stderr, "  -p - Force PAL video mode\n");
  fprintf(stderr, "  -q - Name of bitstream file to load and then directly quit. Use this for cores other than MEGA65.\n");
  fprintf(stderr, "  -R - ROM file to preload at $20000-$3FFFF.\n");
  fprintf(stderr, "  -r - Automatically RUN programme after loading.\n");
  fprintf(stderr, "  -S - Show the text-mode screen\n");
  fprintf(stderr, "  -s - Speed of serial port in bits per second. This must match what your bitstream uses.\n");
  fprintf(stderr, "       (Older bitstream use 230400, and newer ones 2000000 or 4000000).\n");
  fprintf(stderr, "  -T - As above, but also provide carriage return\n");
  fprintf(stderr, "  -t - Type text via keyboard virtualisation.\n");
  fprintf(stderr, "  -U - Flash menu file to preload at $50000-$57FFF.\n");
  fprintf(stderr, "  -u - Enable unit test mode: m65 does not terminate until it receives a response from a unit test.\n");
  fprintf(stderr, "  -v - The location of the Vivado executable to use for -b on Windows.\n");
  fprintf(stderr, "  -V - Write JTAG change log to VCD file, instead of to stdout.\n");
  fprintf(stderr, "  -X - Show a report of current Hypervisor status.\n");
  fprintf(stderr, "  -Z - Zap (reconfigure) FPGA from specified hex address in flash.\n");
  fprintf(stderr, "  filename - Load and run this file in C64 mode before exiting.\n");
  fprintf(stderr, "\n");
  exit(-3);
}

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
char* set_mixer_args = NULL;
int state = 99;
unsigned int name_len, name_lo, name_hi, name_addr = -1;
int do_go64 = 0;
int do_run = 0;
int comma_eight_comma_one = 0;
int ethernet_video = 0;
int ethernet_cpulog = 0;
int virtual_f011 = 0;
char* d81file = NULL;
char* filename = NULL;
char* romfile = NULL;
char* flashmenufile = NULL;
char* charromfile = NULL;
char* colourramfile = NULL;
FILE* f = NULL;
FILE* fd81 = NULL;
char* search_path = ".";
char* bitstream = NULL;
char* vivado_bat = NULL;
char* hyppo = NULL;
char* fpga_serial = NULL;
char* serial_port = NULL; // XXX do a better job auto-detecting this
char modeline_cmd[1024] = "";
int break_point = -1;
int jtag_only = 0;
int bitstream_only = 0;
uint32_t zap_addr;
int zap = 0;

int hypervisor_paused = 0;

int screen_shot = 0;
int screen_rows_remaining = 0;
int screen_address = 0;
int next_screen_address = 0;
int screen_line_offset = 0;
int screen_line_step = 0;
int screen_width = 0;
unsigned char screen_line_buffer[256];

char* type_text = NULL;
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

      fprintf(stderr, "Could not open D81 file: '%s'\n", d81file);
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

      fprintf(stderr, "Error finding D81 sector %d @ 0x%x\n", result, (track * 20 + physical_sector) * 512);
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
  stop_cpu();
  mega65_poke(0xffd3086, side & 0x7f);
  start_cpu();

  timestamp_msg("");
  vf011_bytes_read += 256;
  fprintf(stderr, "READ  device: %d  track: %d  sector: %d  side: %d @ %3.2fKB/sec\n", device, track, sector, side,
      vf011_bytes_read * 1.0 / (gettime_ms() - vf011_first_read_time));

  return 0;
}

int virtual_f011_write(int device, int track, int sector, int side)
{

  pending_vf011_write = 0;

  long long start = gettime_ms();

  if (!vf011_first_read_time)
    vf011_first_read_time = start - 1;

#if 1
  fprintf(stderr, "T+%lld ms : Servicing hypervisor request for F011 FDC sector write.\n", gettime_ms() - start);
#endif

  if (fd81 == NULL) {

    fd81 = fopen(d81file, "wb+");
    if (!fd81) {

      fprintf(stderr, "Could not open D81 file: '%s'\n", d81file);
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

    fprintf(stderr, "Error finding D81 sector %d @ 0x%x\n", result, (track * 20 + physical_sector) * 512);
    exit(-2);
  }
  else {
    int b = fwrite(buf, 1, 512, fd81);
    //	fprintf(stderr, " bytes read: %d @ 0x%x\n", b,(track*20+physical_sector)*512);

    if (b != 512) {
      fprintf(stderr, "ERROR: Short write of %d bytes\n", b);
    }
  }

  /* signal done/result */
  stop_cpu();
  mega65_poke(0xffd3086, side & 0x0f);
  start_cpu();

  timestamp_msg("");
  vf011_bytes_read += 256;
  fprintf(stderr, "WRITE device: %d  track: %d  sector: %d  side: %d @ %3.2fKB/sec\n", device, track, sector, side,
      vf011_bytes_read * 1.0 / (gettime_ms() - vf011_first_read_time));

  return 0;
}

uint32_t uint32_from_buf(unsigned char* b, int ofs)
{
  uint32_t v = 0;
  v = b[ofs + 0];
  v |= (b[ofs + 1] << 8);
  v |= (b[ofs + 2] << 16);
  v |= (b[ofs + 3] << 24);
  return v;
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

  fetch_ram(0xfffbb00, 0x100, disktable_buffer);
  fetch_ram(0xfffbbc0, 0x40, syspart_buffer);
  fetch_ram(0xfffbc00, 0x400, hyppo_buffer);
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
}

void progress_to_RTI(void)
{
  int bytes = 0;
  int match_state = 0;
  int b = 0;
  unsigned char buff[8192];
  slow_write_safe(fd, "tc\r", 3);
  while (1) {
    b = serialport_read(fd, buff, 8192);
    if (b > 0)
      dump_bytes(2, "RTI search input", buff, b);
    if (b > 0) {
      bytes += b;
      buff[b] = 0;
      for (int i = 0; i < b; i++) {
        if (match_state == 0 && buff[i] == 'R') {
          match_state = 1;
        }
        else if (match_state == 1 && buff[i] == 'T') {
          match_state = 2;
        }
        else if (match_state == 2 && buff[i] == 'I') {
          slow_write_safe(fd, "\r", 1);
          fprintf(stderr, "RTI seen after %d bytes\n", bytes);
          return;
        }
        else
          match_state = 0;
      }
    }
    fflush(stdout);
  }
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
      fprintf(stderr, "NOTE: Switching to ASCII stuffing of buffered UART.\n");
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

void do_type_text(char* type_text)
{
  fprintf(stderr, "Typing text via virtual keyboard...\n");

#ifndef WINDOWS
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
      unsigned char c;

      /* get the terminal settings for stdin */
      tcgetattr(STDIN_FILENO, &old_tio);

      /* we want to keep the old setting to restore them a the end */
      new_tio = old_tio;

      /* disable canonical mode (buffered i/o) and local echo */
      new_tio.c_lflag &= (~ICANON & ~ECHO);

      /* set the new settings immediately */
      tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

      fprintf(stderr, "Reading input from terminal in character mode.\n"
                      "Type CONTROL-Y to end.\n");

      c = getc(stdin);
      while (c != 25) {
        printf("$%02x -> ", c);
        switch (c) {
        case 0x7f:
          c = 0x14;
          break; // DELETE
        case 0x0a:
          c = 0x0d;
          break; // RETURN
        case 0x1b:
          // Escape code
          c = getc(stdin);
          if (c == '[') {
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
        printf("$%02x\n", c);
        if (c) {
          do_type_key(c);
        }
        else
          usleep(1000);
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
    for (i = 0; type_text[i]; i++) {
      if (type_text[i] == '~') {
        unsigned char c1;
        // control sequences
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
        }
        do_type_key(c1);
        i++;
        break;
      }
      else
        do_type_key(type_text[i]);
    }

    // RETURN at end if requested
    if (type_text_cr)
      slow_write(fd, "sffd3615 01 7f 7f \n", 19);
  }
  // Stop pressing keys
  slow_write(fd, "sffd3615 7f 7f 7f \n", 19);
}

char line[1024];
int line_len = 0;

void* run_boundary_scan(void* argp)
{
  xilinx_boundaryscan(boundary_xdc[0] ? boundary_xdc : NULL, boundary_bsdl[0] ? boundary_bsdl : NULL,
      jtag_sensitivity[0] ? jtag_sensitivity : NULL);
  return (void*)NULL;
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
  FILE* nf = fopen(filename, "rb");
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

  FILE* f = fopen("/tmp/monitor_load.folder.txt", "rb");
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
  FILE* nf = fopen(filename, "rb");
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

  FILE* f = fopen("/tmp/monitor_load.folder.txt", "rb");
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

void load_bitstream(char* bitstream)
{
  if (vivado_bat != NULL) {
    /*  For Windows we just call Vivado to do the FPGA programming,
        while we are having horrible USB problems otherwise. */
    unsigned int fpga_id = 0x3631093;

    FILE* f = fopen(bitstream, "r");
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

          timestamp_msg("");
          fprintf(stderr, "Detected FPGA ID %x from bitstream file.\n", fpga_id);
        }
      }
      fclose(f);
    }
    else {
      fprintf(stderr, "WARNING: Could not open bitstream file '%s'\n", bitstream);
    }

    char* part_name = "xc7a100t_0";
    fprintf(stderr, "INFO: Expecting FPGA Part ID %x\n", fpga_id);
    if (fpga_id == 0x3636093)
      part_name = "xc7a200t_0";

    FILE* tclfile = fopen("temp.tcl", "w");
    if (!tclfile) {
      fprintf(stderr, "ERROR: Could not create temp.tcl");
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
    printf("Running %s...\n", cmd);
    system(cmd);
    unlink("temp.tcl");
  }
  else {
    // No Vivado.bat, so try to use internal fpgajtag implementation.
    fprintf(stderr, "INFO: NOT using vivado.bat file\n");
    if (fpga_serial) {
      fpgajtag_main(bitstream, fpga_serial);
    }
    else {
      fpgajtag_main(bitstream, NULL);
    }
  }
  timestamp_msg("Bitstream loaded.\n");
}

void enter_hypervisor_mode(void)
{
  /* Ach! for some things we want to make sure we are in the hypervisor.
     This is a bit annoying, as we have to make sure we save state etc
     properly.
  */
  monitor_sync();
  stop_cpu();
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

int check_file_access(char* file, char* purpose)
{
  FILE* f = fopen(file, "rb");
  if (!f) {
    fprintf(stderr, "ERROR: Cannot access %s file '%s'\n", purpose, file);
    exit(-1);
  }
  else
    fclose(f);

  return 0;
}

extern const char* version_string;

void unit_test_log(unsigned char bytes[4],int argc,char **argv)
{
  switch(bytes[3]) {
  case 0xf0: // Starting a test
    break;
  case 0xf1: // Skipping a test
    break;
  case 0xf2: // Test pass
    break;
  case 0xf3: // Test failure (ie test ran, but detected failure of test condition)
    break;
  case 0xf4: // Error trying to run test
    break;
  }
  
}

int main(int argc, char** argv)
{
  start_time = time(0);

  fprintf(stderr,
      "MEGA65 Cross-Platform tool.\n"
      "version: %s\n",
      version_string);

  timestamp_msg("");
  fprintf(stderr, "Getting started..\n");

  if (argc == 1)
    usage();

  int opt;
  while ((opt = getopt(argc, argv, "@:14aA:B:b:q:c:C:d:DEFHf:jJ:Kk:Ll:MnNoprR:Ss:t:T:uU:v:V:XZ:?")) != -1) {
    switch (opt) {
    case 'D':
      debug_serial = 1;
      break;
    case 'h':
    case '?':
      usage();
    case '@':
      load_binary = optarg;
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
      usedk = 1;
      break;
    case 'Z': {
      // Zap (reconfig) FPGA via MEGA65 reconfig registers
      sscanf(optarg, "%x", &zap_addr);
      fprintf(stderr, "Reconfiguring FPGA using bitstream at $%08x\n", zap_addr);
      zap = 1;
    } break;
    case 'B':
      sscanf(optarg, "%x", &break_point);
      break;
    case 'L':
      if (ethernet_video) {
        usage();
      }
      else {
        ethernet_cpulog = 1;
      }
      break;
    case 'E':
      if (ethernet_cpulog) {
        usage();
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
      break;
    case '1':
      comma_eight_comma_one = 1;
      break;
    case 'p':
      pal_mode = 1;
      break;
    case 'n':
      ntsc_mode = 1;
      break;
    case 'F':
      reset_first = 1;
      break;
    case 'r':
      do_run = 1;
      break;
    case 'f':
      fpga_serial = strdup(optarg);
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
        usage();
      }
      break;
    case 'S':
      screen_shot = 1;
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
      unit_test_mode=1;
      break;
    default: /* '?' */
      usage();
    }
  }

  // Automatically find the serial port on Linux, if one has not been
  // provided
  // Detect only A7100T parts
  // XXX Will require patching for MEGA65 R1 PCBs, as they have an A200T part.
#ifdef __APPLE__
  if (bitstream)
#endif
  {
    fprintf(stderr, "NOTE: Scanning bitstream file '%s' for device ID\n", bitstream);
    unsigned int fpga_id = 0x3631093;
    FILE* f = fopen(bitstream, "rb");
    if (f) {
      unsigned char buff[8192];
      int len = fread(buff, 1, 8192, f);
      fprintf(stderr, "NOTE: Read %d bytes to search\n", len);
      for (int i = 0; i < len; i++) {
        if ((buff[i + 0] == 0x30) && (buff[i + 1] == 0x01) && (buff[i + 2] == 0x80) && (buff[i + 3] == 0x01)) {
          i += 4;
          fpga_id = buff[i + 0] << 24;
          fpga_id |= buff[i + 1] << 16;
          fpga_id |= buff[i + 2] << 8;
          fpga_id |= buff[i + 3] << 0;

          timestamp_msg("");
          fprintf(stderr, "Detected FPGA ID %x from bitstream file.\n", fpga_id);
        }
      }
      fclose(f);
    }
    fprintf(stderr, "INFO: Using fpga_id %x\n", fpga_id);
#ifndef WINDOWS
    init_fpgajtag(NULL, bitstream, fpga_id);
#endif
  }

#ifdef WINDOWS
  if (boundary_scan) {
    fprintf(stderr, "WARNING: JTAG boundary scan not implemented on Windows.\n");
  }
#else
  if (boundary_scan) {
    fprintf(stderr, "ERROR: threading on Windows not implemented.\n");
    exit(-1);
    // Launch boundary scan in a separate thread, so that we can monitor signals while
    // running other operations.
    if (pthread_create(&threads[thread_count++], NULL, run_boundary_scan, NULL))
      perror("Failed to create JTAG boundary scan thread.\n");
    else
      fprintf(stderr, "JTAG boundary scan launched in separate thread.\n");
  }
#endif

  if (jtag_only)
    do_exit(0);

  if (argv[optind]) {
    filename = strdup(argv[optind]);
    check_file_access(filename, "programme");
  }

  if (argc - optind > 1)
    usage();

  // -b Load bitstream if file provided
  if (bitstream) {
    load_bitstream(bitstream);
    if (bitstream_only)
      do_exit(0);
  }

  if (virtual_f011) {
    char msg[1024];
    snprintf(msg, 1024, "Remote access to disk image '%s' requested.\n", d81file);
    timestamp_msg(msg);
  }

  open_the_serial_port(serial_port);

  rxbuff_detect();
  monitor_sync();

  if (zap) {
    char cmd[1024];
    monitor_sync();
    snprintf(cmd, 1024, "sffd36c8 %x %x %x %x\r", (zap_addr >> 0) & 0xff, (zap_addr >> 8) & 0xff, (zap_addr >> 16) & 0xff,
        (zap_addr >> 24) & 0xff);
    slow_write(fd, cmd, strlen(cmd));
    monitor_sync();
    mega65_poke(0xffd36cf, 0x42);
    fprintf(stderr, "FPGA reconfigure command issued.\n");
    // XXX This can take a while, which we should accommodate
    monitor_sync();
  }

  if (hyppo_report)
    show_hyppo_report();

  // If we have no HYPPO file provided, but need one, then
  // extract one out of the running bitstream.
  if (!hyppo) {
    if (virtual_f011) {
      timestamp_msg("Extracting HYPPO from running system...\n");
      unsigned char hyppo_data[0x4000];
      fetch_ram(0xFFF8000, 0x4000, hyppo_data);
#ifdef WINDOWS
      char* temp_name = "HYPPOEXT.M65";
#else
      char* temp_name = "/tmp/HYPPOEXT.M65";
#endif
      FILE* f = fopen(temp_name, "wb");
      if (!f) {
        perror("Could not create temporary HYPPO file.");
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
        mega65_poke(0xffd367d, mega65_peek(0xffd367d) & (0xff - 4));

        load_file(romfile, 0x20000, 0);
        // reenable ROM write protect
        mega65_poke(0xffd367d, mega65_peek(0xffd367d) | 0x04);
      }
      if (charromfile)
        load_file(charromfile, 0xFF7E000, 0);
      return_from_hypervisor_mode();
    }

    if (colourramfile)
      load_file(colourramfile, 0xFF80000, 0);
    if (flashmenufile) {
      load_file(flashmenufile, 0x50000, 0);
    }
  }
  else {
    int patchKS = 0;
    if (romfile && (!flashmenufile))
      patchKS = 1;

    timestamp_msg("Replacing HYPPO...\n");

    stop_cpu();
    if (hyppo) {
      stop_cpu();
      load_file(hyppo, 0xfff8000, patchKS);
    }
    if (flashmenufile) {
      load_file(flashmenufile, 0x50000, 0);
    }
    if (romfile) {
      load_file(romfile, 0x20000, 0);
    }
    if (charromfile)
      load_file(charromfile, 0xFF7E000, 0);
    if (colourramfile)
      load_file(colourramfile, 0xFF80000, 0);
    if (virtual_f011) {
      timestamp_msg("Virtualising F011 FDC access.\n");

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
    start_cpu();
    slow_write(fd, "\r!\r", 3);
    monitor_sync();
    sleep(2);
  }

  if (no_cart) {
    char cmd[1024];

    for (int i = 0; i < 2; i++) {
      stop_cpu();
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
    fprintf(stderr, "Setting CPU breakpoint at $%04x\n", break_point);
    char cmd[1024];
    sprintf(cmd, "b%x\r", break_point);
    do_usleep(20000);
    slow_write(fd, cmd, strlen(cmd));
    do_exit(0);
  }

  if (pal_mode) {
    mega65_poke(0xffd306f, 0);
  }
  if (ntsc_mode) {
    mega65_poke(0xffd306f, 0x80);
  }
  if (ethernet_video) {
    mega65_poke(0xffd36e1, 0x29);
  }
  if (ethernet_cpulog) {
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
      fprintf(stderr, "ERROR: -@ option format is file@hexaddr\n");
      usage();
    }

    enter_hypervisor_mode();

    // Un-protect ROM area
    mega65_poke(0xffd367d, mega65_peek(0xffd367d) & (0xff - 4));

    load_file(filename, load_addr, 0);
    fprintf(stderr, "Loaded file '%s' @ $%x\n", filename, load_addr);

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
    timestamp_msg("");
    fprintf(stderr, "Detecting C64/C65 mode status.\n");
    detect_mode();
  }

  // -4 Switch to C64 mode
  if ((!saw_c64_mode) && do_go64) {
    switch_to_c64mode();
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
    printf("OSK Enabled\n");
  }

  // -S screen shot
  if (screen_shot) {
    stop_cpu();
    do_screen_shot();
    start_cpu();
    do_exit(0);
  }

  if (filename) {
    timestamp_msg("");
    fprintf(stderr, "Loading file '%s'\n", filename);

    unsigned int load_routine_addr = 0xf664;

    int filename_matches = 0;
    int first_time = 1;

    // We REALLY need to know which mode we are in for LOAD
    while (do_go64 && (!saw_c64_mode)) {
      detect_mode();
      if (!saw_c64_mode) {
        fprintf(stderr, "ERROR: In C65 mode, but expected C64 mode\n");
        exit(-1);
      }
    }
    while ((!do_go64) && (!saw_c65_mode)) {
      detect_mode();
      if (!saw_c65_mode) {
        fprintf(stderr, "ERROR: Should be in C65 mode, but don't seem to be.\n");
        exit(-1);
      }
    }

    while (!filename_matches) {

      if (saw_c64_mode) {
        // Assume LOAD vector in C64 mode is fixed
        load_routine_addr = 0xf4a5;
        fprintf(stderr, "NOTE: Assuming LOAD routine at $F4A5 for C64 mode\n");
      }
      else {
        unsigned char vectorbuff[2];
        fetch_ram(0x3FFD6, 2, vectorbuff);
        load_routine_addr = vectorbuff[0] + (vectorbuff[1] << 8);
        fprintf(stderr, "NOTE: LOAD vector from ROM is $%04x\n", load_routine_addr);
      }
      // Type LOAD command and set breakpoint to catch the ROM routine
      // when it executes.
      breakpoint_set(load_routine_addr);
      if (first_time) {
        if (saw_c64_mode) {
          // What we stuff in the keyboard buffer here is actually
          // not important for ,1 loading.  That gets handled in the loading
          // logic.  But we reflect it here, so that it doesn't confuse people.
          if (comma_eight_comma_one)
            stuff_keybuffer("Lo\"!\",8,1\r");
          else
            stuff_keybuffer("LOAD\"!\",8\r");
        }
        else {
          // Really wait for C65 to get to READY prompt
          stuff_keybuffer("DLo\"!\r");
        }
      }
      first_time = 0;
      breakpoint_wait();

      int filename_addr = 1;
      unsigned char filename_len = mega65_peek(0xb7);
      if (saw_c64_mode)
        filename_addr = mega65_peek(0xbb) + mega65_peek(0xbc) * 256;
      else {
        filename_addr = mega65_peek(0xbb) + mega65_peek(0xbc) * 256 + mega65_peek(0xbe) * 65536;
      }
      char requested_name[256];
      fetch_ram(filename_addr, filename_len, (unsigned char*)requested_name);
      requested_name[filename_len] = 0;
      timestamp_msg("");
      fprintf(stderr, "Requested file is '%s' (len=%d)\n", requested_name, filename_len);
      // If we caught the boot load request, then feed the DLOAD command again
      if (!strcmp(requested_name, "0:AUTOBOOT.C65*"))
        first_time = 1;

      if (!strcmp(requested_name, "!"))
        break;
      if (!strcmp(requested_name, "0:!"))
        break;

      start_cpu();
    }

    // We can ignore the filename.
    // Next we just load the file

    int is_sid_tune = 0;

    FILE* f = fopen(filename, "rb");
    if (f == NULL) {
      fprintf(stderr, "Could not find file '%s'\n", filename);
      exit(-1);
    }
    else {
      char cmd[1024];
      int load_addr = fgetc(f);
      load_addr |= fgetc(f) << 8;
      if ((load_addr == 0x5350) || (load_addr == 0x5352)) {
        // It's probably a SID file

        timestamp_msg("Examining SID file...\n");

        // Read header
        unsigned char sid_header[0x7c];
        fread(sid_header, 0x7c, 1, f);

        unsigned int start_addr = (sid_header[0x0a - 0x02] << 8) + sid_header[0x0b - 0x02];
        unsigned int play_addr = (sid_header[0x0c - 0x02] << 8) + sid_header[0x0d - 0x02];
        //	unsigned int play_speed=sid_header[0x12-0x02];

        char* name = (char*)&sid_header[0x16 - 0x02];
        char* author = (char*)&sid_header[0x36 - 0x02];
        char* released = (char*)&sid_header[0x56 - 0x02];

        timestamp_msg("");
        fprintf(stderr, "SID tune '%s' by '%s' (%s)\n", name, author, released);

        // Also show player info on the screen
        char player_screen[1000] = { "                                        "
                                     "                                        "
                                     "                                        "
                                     "M65 TOOL CRUSTY SID PLAYER V00.00 ALPHA "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "0 - 9 = SELECT TRACK                    "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        "
                                     "                                        " };
        for (int i = 0; name[i]; i++)
          player_screen[40 * 6 + i] = name[i];
        for (int i = 0; author[i]; i++)
          player_screen[40 * 8 + i] = author[i];
        for (int i = 0; released[i]; i++)
          player_screen[40 * 10 + i] = released[i];

        for (int i = 0; i < 1000; i++) {
          if (player_screen[i] >= '@' && player_screen[i] <= 'Z')
            player_screen[i] &= 0x1f;
          if (player_screen[i] >= 'a' && player_screen[i] <= 'z')
            player_screen[i] &= 0x1f;
        }

        push_ram(0x0400, 1000, (unsigned char*)player_screen);

        // Patch load address
        load_addr = (sid_header[0x7d - 0x02] << 8) + sid_header[0x7c - 0x02];
        timestamp_msg("");
        fprintf(stderr, "SID load address is $%04x\n", load_addr);
        //	dump_bytes(0,"sid header",sid_header,0x7c);

        // Prepare simple play routine
        // XXX For now it is always VIC frame locked
        timestamp_msg("Uploading play routine\n");
        int b = 56;
        unsigned char player[56] = { 0x78, 0xa9, 0x35, 0x85, 0x01, 0xa9, 0x01, 0x20, 0x34, 0x12, 0xa9, 0x80, 0xcd, 0x12,
          0xd0, 0xd0, 0xfb, 0xa9, 0x01, 0x8d, 0x20, 0xd0, 0x20, 0x78, 0x56, 0xa9, 0x00, 0x8d, 0x20, 0xd0, 0xa9, 0x80, 0xcd,
          0x12, 0xd0, 0xf0, 0xfb,

          0xad, 0x10, 0xd6, 0xf0, 0x0b, 0x8d, 0x10, 0xd6, 0x29, 0x0f, 0x8d, 0x21, 0xd0, 0x4c, 0x07, 0x04,

          0x4c, 0x0A, 0x04 };

        player[6 + 0] = sid_header[0x11 - 0x02] - 1;

        if (start_addr) {
          player[8 + 0] = (start_addr >> 0) & 0xff;
          player[8 + 1] = (start_addr >> 8) & 0xff;
        }
        else {
          player[7 + 0] = 0xea;
          player[7 + 1] = 0xea;
          player[7 + 2] = 0xea;
        }
        if (play_addr) {
          player[23 + 0] = (play_addr >> 0) & 0xff;
          player[23 + 1] = (play_addr >> 8) & 0xff;
        }
        else {
          player[22 + 0] = 0xea;
          player[22 + 1] = 0xea;
          player[22 + 2] = 0xea;
        }

        // Enable M65 IO for keyboard scanning
        slow_write(fd, "sffd302f 47\n", 12);
        slow_write(fd, "sffd302f 53\n", 12);

        push_ram(0x0400, b, player);

        is_sid_tune = 1;
      }
      else if (!comma_eight_comma_one) {
        if (saw_c64_mode)
          load_addr = 0x0801;
        else
          load_addr = 0x2001;
        timestamp_msg("");
        fprintf(stderr, "Forcing load address to $%04X\n", load_addr);
      }
      else
        printf("Load address is $%04x\n", load_addr);

      unsigned char buf[32768];
      int max_bytes = 32768;
      int b = fread(buf, 1, max_bytes, f);
      while (b > 0) {
        timestamp_msg("");
        fprintf(stderr, "Read block for $%04x -- $%04x (%d bytes)\n", load_addr, load_addr + b - 1, b);

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
                    fprintf(stderr, "Tune uses SID at $%04x\n", this_sid);
                  }
                }
              }
              break;
            }
          }
          fprintf(stderr, "Tune uses a total of %d SIDs.\n", num_sids);
          for (int i = 0; i < num_sids; i++) {
            if (sid_addrs[i] >= 0xd600) {
              fix_addrs[i] = 0xd400 + 0x20 * i;
              fprintf(stderr, "Relocating SID at $%02x to $%04x\n", sid_addrs[i], fix_addrs[i]);
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
                    fprintf(stderr, "@ $%04X Patching $%04X to $%04X\n", i + load_addr, this_sid,
                        fix_addrs[j] | (this_sid & 0x1f));
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

#ifdef WINDOWS_GUS
        // Windows doesn't seem to work with the l fast-load monitor command
        printf("Asking Gus to write data...\n");
        for (int i = 0; i < b; i += 16) {
          int ofs = 0;
          sprintf(cmd, "s%x", load_addr + i);
          ofs = strlen(cmd);
          for (int j = 0; (j < 16) && (i + j) < b; j++) {
            sprintf(&cmd[ofs], " %x", buf[i + j]);
            ofs = strlen(cmd);
          }
          sprintf(&cmd[ofs], "\r");
          ofs = strlen(cmd);
          slow_write(fd, cmd, strlen(cmd));
        }
#else
        // load_addr=0x400;
        push_ram(load_addr, b, buf);
#endif
        load_addr += b;
        b = fread(buf, 1, max_bytes, f);
      }
      fclose(f);
      f = NULL;

      // set end address, clear input buffer, release break point,
      // jump to end of load routine, resume CPU at a CLC, RTS
      monitor_sync();

      // Clear keyboard input buffer
      if (saw_c64_mode)
        sprintf(cmd, "sc6 0\r");
      else
        sprintf(cmd, "sd0 0\r");
      slow_write(fd, cmd, strlen(cmd));
      monitor_sync();

      // Remove breakpoint
      sprintf(cmd, "b\r");
      slow_write(fd, cmd, strlen(cmd));
      monitor_sync();

      // Set end of memory pointer
      {
        int top_of_mem_ptr_addr = 0x2d;
        if (saw_c65_mode)
          top_of_mem_ptr_addr = 0x82;
        unsigned char load_addr_bytes[2];
        load_addr_bytes[0] = load_addr;
        load_addr_bytes[1] = load_addr >> 8;
        push_ram(top_of_mem_ptr_addr, 2, load_addr_bytes);
        fprintf(stderr, "NOTE: Storing end of program pointer at $%x\n", top_of_mem_ptr_addr);
      }

      // We need to set X and Y to load address before
      // returning: LDX #$ll / LDY #$yy / CLC / RTS
      sprintf(cmd, "s380 a2 %x a0 %x 18 60\r", load_addr & 0xff, (load_addr >> 8) & 0xff);
      timestamp_msg("");
      fprintf(stderr, "Returning top of load address = $%04X\n", load_addr);
      slow_write(fd, cmd, strlen(cmd));
      monitor_sync();

      if ((!is_sid_tune) || (!do_run)) {
        sprintf(cmd, "g0380\r");
      }
      else
        sprintf(cmd, "g0400\r");

#if 1
      slow_write(fd, cmd, strlen(cmd));
      //      monitor_sync();

      if (!halt) {
        start_cpu();
      }

      if (do_run) {
        stuff_keybuffer("RUN:\r");
        timestamp_msg("RUNning.\n");
      }
#endif

      // loaded ok.
      timestamp_msg("");
      fprintf(stderr, "LOADED.\n");
    }
  }

  // XXX - loop for virtualisation, JTAG boundary scanning etc

  unsigned char recent_bytes[4] = { 0, 0, 0, 0 };
  
  if (virtual_f011||unit_test_mode) {
    if (virtual_f011)
      fprintf(stderr, "Entering virtualised F011 wait loop...\n");
    if (unit_test_mode)
      fprintf(stderr, "Entering unit test mode. Waiting for test results.\n");
    while (1) {
      unsigned char buff[8192];

      int b = serialport_read(fd, buff, 8192);
      if (b > 0)
        dump_bytes(2, "VF011 wait", buff, b);
      for (int i = 0; i < b; i++) {
        recent_bytes[0] = recent_bytes[1];
        recent_bytes[1] = recent_bytes[2];
        recent_bytes[2] = recent_bytes[3];
        recent_bytes[3] = buff[i];

	if (recent_bytes[3] >= 0xf0) {
	  // Unit test start
	  unit_test_log(recent_bytes,argc,argv);
	}
	
	if (recent_bytes[3] == '!') {
	  // Handle request
	  recent_bytes[3] = 0;
	  pending_vf011_read = 1;
	  pending_vf011_device = 0;
	  pending_vf011_track = recent_bytes[0] & 0x7f;
	  pending_vf011_sector = recent_bytes[1] & 0x7f;
	  pending_vf011_side = recent_bytes[2] & 0x0f;
	}
	if (recent_bytes[3] == 0x5c) {
	  // Handle request
	  recent_bytes[3] = 0;
	  pending_vf011_write = 1;
	  pending_vf011_device = 0;
	  pending_vf011_track = recent_bytes[0] & 0x7f;
	  pending_vf011_sector = recent_bytes[1] & 0x7f;
	  pending_vf011_side = recent_bytes[2] & 0x0f;
	}
      }
      while (pending_vf011_read || pending_vf011_write) {
        if (pending_vf011_read)
          virtual_f011_read(pending_vf011_device, pending_vf011_track, pending_vf011_sector, pending_vf011_side);
        if (pending_vf011_write)
          virtual_f011_write(pending_vf011_device, pending_vf011_track, pending_vf011_sector, pending_vf011_side);
      }

      continue;
    }
  }
  do_exit(0);
}

void do_exit(int retval)
{
#ifndef WINDOWS
  if (thread_count) {
    timestamp_msg("");
    fprintf(stderr, "Background tasks may be running running. CONTROL+C to stop...\n");
    for (int i = 0; i < thread_count; i++)
      pthread_join(threads[i], NULL);
  }
#endif
  close_tcp_port();

  exit(retval);
}
