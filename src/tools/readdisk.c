/*
  Read some or all of the contents of a real floppy in a MEGA65
  via the serial monitor interface

  Copyright (C) 2014-2021 Paul Gardner-Stephen
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

#include "m65common.h"

#define UT_TIMEOUT 10

#define UT_RES_TIMEOUT 127

extern int pending_vf011_read;
extern int pending_vf011_write;
extern int pending_vf011_device;
extern int pending_vf011_track;
extern int pending_vf011_sector;
extern int pending_vf011_side;

extern int debug_serial;
int debug_load_memory = 0;

extern unsigned char recent_bytes[4];

int full_read=0;

int osk_enable = 0;

int not_already_loaded = 1;

int unit_test_mode = 0;
int unit_test_timeout = UT_TIMEOUT;

int halt = 0;

int usedk = 0;

char* load_binary = NULL;

int viciv_mode_report(unsigned char* viciv_regs);

int fpgajtag_main(char* bitstream, char* serialport);
char* init_fpgajtag(const char* serialno, const char* serialport, uint32_t file_idcode);
int xilinx_boundaryscan(char* xdc, char* bsdl, char* sensitivity);
void set_vcd_file(char* name);
void do_exit(int retval);
void get_video_state(void);

void usage(void)
{
  fprintf(stderr, "MEGA65 remote disk reading tool.\n");
  fprintf(stderr,
      "usage: m65 [-l <serial port>] [-s <230400|2000000|4000000>] [-f] out.d81\n");

  fprintf(stderr, "  -f - Do full copy (instead of only copying sectors likely to contain data.\n"
                  "  -l - Name of serial port to use, e.g., /dev/ttyUSB1\n"
                  "  -s - Speed of serial port in bits per second. This must match what your bitstream uses.\n"
                  "       (Typically 2000000 or 4000000).\n"
                  "\n");
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
char* logfile = NULL;
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
  real_stop_cpu();
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
  real_stop_cpu();
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
          break;
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

void check_for_vf011_requests()
{

  if (!virtual_f011) {
    return;
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

extern const char* version_string;

char* test_states[16] = { "START", " SKIP", " PASS", " FAIL", "ERROR", "C#$05", "C#$06", "C#$07", "C#$08", "C#$09", "C#$0A",
  "C#$0B", "C#$0C", "  LOG", " NAME", " DONE" };

char msgbuf[160], *endp;
char testname[160];
unsigned char inbuf[8192];
unsigned int failcount;
FILE* logPtr;

void unit_test_log(unsigned char bytes[4])
{
  int test_issue = bytes[0] + (bytes[1] << 8);
  int test_sub = bytes[2];
  char outstring[255];
  char temp[255];

  struct timeval currentTime;

  // dump_bytes(0, "bytes", bytes, 4);

  gettimeofday(&currentTime, NULL);
  strftime(outstring, 255, "%Y-%m-%dT%H:%M:%S", gmtime(&(currentTime.tv_sec)));

  sprintf(temp, ".%03dZ %s (Issue#%04d, Test #%03d", (unsigned int)currentTime.tv_usec/1000, test_states[bytes[3] - 0xf0], test_issue, test_sub);
  strcat(outstring, temp);

  // append current test name if we have one
  if (testname[0]) {
    sprintf(temp, " - %s)", testname);
  }
  else {
    sprintf(temp, ")");
  }
  strcat(outstring, temp);

  fprintf(stderr, "%s\n", outstring);
  if (logPtr) {
    fprintf(logPtr, "%s\n", outstring);
    fflush(logPtr);
  }

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
    fprintf(stderr, ">>> Terminating after completion of last test.\n");
    if (logPtr) {
      fprintf(logPtr, ">>> Terminating after completion of last test.\n");
      fclose(logPtr);
    }
    do_exit(failcount);
    break;
  }
}

void enterTestMode()
{

  unsigned char receiveString;
  int currentMessagePos;
  time_t currentTime;
  char* ts;

  fprintf(stderr, "Entering unit test mode. Waiting for test results.\n");
  testname[0] = 0; // initialize test name with empty string
  receiveString = 0;
  failcount = 0;
  logPtr = NULL;

  currentTime = time(NULL);

  if (logfile) {

    logPtr = fopen(logfile, "a");
    if (!logPtr) {
      fprintf(stderr, "could not open logfile %s for appending. aborting\n", logfile);
      exit(127);
    }

    ts = asctime(localtime(&currentTime));
    ts[strlen(ts) - 1] = 0;

    fprintf(stderr, "logging test results in %s\n", logfile);
    fprintf(logPtr, "\n>>> begin testing %s; FILE: %s\n", ts, filename);
  }

  while (time(NULL) - currentTime < unit_test_timeout) {

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
          if (recent_bytes[3] == 0xfd) { // log message to console
            fprintf(stderr, "%s\n", msgbuf);
            if (logPtr) {
              fprintf(logPtr, "%s\n", msgbuf);
            }
          }
          else if (recent_bytes[3] == 0xfe) { // set current test name
            strncpy(testname, msgbuf, 160);
          }
          bzero(recent_bytes, 4);
        }

        currentMessagePos++;
      }
      else {
        recent_bytes[0] = recent_bytes[1];
        recent_bytes[1] = recent_bytes[2];
        recent_bytes[2] = recent_bytes[3];
        recent_bytes[3] = inbuf[i];
        check_for_vf011_requests();
      }
      handle_vf011_requests();

      // check if we should receive a string
      if (!receiveString) {
        if (recent_bytes[3] == 0xfe || recent_bytes[3] == 0xfd) {
          // receive message
          receiveString = 1;
          currentMessagePos = 0;
        }
      }

      // not receiving a string? handle unit test token if needed
      if (!receiveString) {
        if (recent_bytes[3] >= 0xf0) {
          // handle unit test token and update time
          currentTime = time(NULL);
          unit_test_log(recent_bytes);
        }
      }
    }
  }

  if (testname[0]) {
    fprintf(stderr, "%s: ", testname);
    if (logPtr) {
      fprintf(logPtr, "%s: ", testname);
    }
  }
  fprintf(stderr, "timeout encountered while running tests. aborting.\n");
  if (logPtr) {
    fprintf(logPtr, "timeout encountered while running tests. aborting.\n");
    fclose(logPtr);
  }
  do_exit(UT_RES_TIMEOUT);
}

unsigned char checkUSBPermissions()
{
#ifndef WINDOWS
  libusb_device_handle* usbhandle = NULL;
  struct libusb_context* usb_context;
  libusb_device** device_list;
  libusb_device* dev;
  unsigned int i = 0;

  if (libusb_init(&usb_context) < 0 || libusb_get_device_list(usb_context, &device_list) < 0) {
    printf("libusb_init failed\n");
    exit(-1);
  }
  while ((dev = device_list[i++])) {
    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(dev, &desc) < 0)
      continue;
    int open_result = libusb_open(dev, &usbhandle);
    if (open_result < 0) {
      return 0;
    }
  }
#endif

  return 1;
}

unsigned char read_a_sector(unsigned char track_number, unsigned char side, unsigned char sector)
{
  // Disable auto-seek, or we can't force seeking to track 0    
  mega65_poke(0xffD3696,0x00);

  // Floppy motor on, and select side
  mega65_poke(0xffD3080, 0x68);
  if (side) mega65_poke(0xffD3080,0x60);
  
  // Map FDC sector buffer, not SD sector buffer
  mega65_poke(0xffD3689, mega65_peek(0xffD3689) & 0x7f);
  
  // Disable matching on any sector, use real drive
  mega65_poke(0xffd36A1, 0x01);

  // Wait until busy flag clears
  while (mega65_peek(0xffD3082) & 0x80) {
    continue;
  }

#if 0
  // Seek to track 0
  goto_track0();
  
  // Seek to the requested track
  while(PEEK(0xD082)&0x80) continue;
  for(i=0;i<track_number;i++) {
    while(PEEK(0xD082)&0x80) continue;
    POKE(0xD081,0x18);
    while(PEEK(0xD082)&0x80) continue;
    }
#endif

  // Now select the side, and try to read the sector
  mega65_poke(0xffD3084, track_number);
  mega65_poke(0xffD3085, sector);
  mega65_poke(0xffD3086, side ? 1 : 0);

  // Issue read command
  mega65_poke(0xffD3081, 0x01); // but first reset buffers
  mega65_poke(0xffD3081, 0x40);

  // Wait for busy flag to clear
  while (mega65_peek(0xffD3082) & 0x80) {
   
  }

  if (mega65_peek(0xffD3082) & 0x18) {
    // Read failed
    fprintf(stderr,"ERROR: Failed to read T:%02x, S:%02x, H:%02x\n",
	    track_number,sector,side);
    return 1;
  }
  else {
    // Read succeeded
    return 0;
  }
}

void goto_track0(void)
{
  // Do some slow seeks first, in case head is stuck at end of disk
  mega65_poke(0xffd3081, 0x10);
  usleep(20000);
  mega65_poke(0xffd3081, 0x10);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  usleep(30000);
  mega65_poke(0xffd3081, 0x10);
  usleep(40000);

  while (!(mega65_peek(0xffd3082) & 0x01)) {
    while (mega65_peek(0xffd3082) & 0x80)
      continue;
    //    mega65_poke(0xffd3020,1);
    mega65_poke(0xffd3081, 0x10);
    while (mega65_peek(0xffd3082) & 0x80)
      continue;
    mega65_poke(0xffd3020, mega65_peek(0xffd3020) + 1);
  }
}


int main(int argc, char** argv)
{
  start_time = time(0);

  fprintf(stderr,
      "MEGA65 Remote Disk Reading tool.\n"
      "version: %s\n\n",
      version_string);

  timestamp_msg("");
  fprintf(stderr, "Getting started..\n");

  if (argc == 1)
    usage();

  int opt;
  while ((opt = getopt(argc, argv, "fhl:s:?")) != -1) {
    switch (opt) {
    case 'h':
    case '?':
      usage();
    case 'f': full_read=1; break;
      break;
    case 'l':
      serial_port = strdup(optarg);
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
    default: /* '?' */
      usage();
    }
  }

  // Automatically find the serial port on Linux, if one has not been
  // provided
  // Detect only A7100T parts
  // XXX Will require patching for MEGA65 R1 PCBs, as they have an A200T part.
#if defined(__APPLE__) || defined(WINDOWS)
  if (bitstream && !serial_port)
#else
  if (!serial_port)
#endif
  {
    fprintf(stderr, "NOTE: Scanning bitstream file '%s' for device ID\n", bitstream);
    unsigned int fpga_id = 0xffffffff;
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
          fprintf(stderr, "Detected FPGA ID %08x from bitstream file.\n", fpga_id);
          break;
        }
      }
      fclose(f);
    }
    fprintf(stderr, "INFO: Using fpga_id %08x\n", fpga_id);
    if (!checkUSBPermissions()) {
      fprintf(stderr, "WARNING: May not be able to auto-detect USB port due to insufficient permissions.\n");
      fprintf(stderr,
          "         You may be able to solve this problem via the following:\n"
          "           sudo usermod -a -G dialout <your username>\n"
          "         and then:\n"
          "           echo 'ACTION==\"add\", ATTRS{idVendor}==\"0403\", ATTRS{idProduct}==\"6010\", GROUP=\"dialout\"' | "
          "sudo tee /etc/udev/rules.d/40-xilinx.rules\n"
          "         and then log out, and log back in again, or failing that, reboot your computer and try again.\n"
          "\n");
    }
    char* res = init_fpgajtag(NULL, NULL, fpga_id);
    if (res != NULL)
      serial_port = res;
  }

  open_the_serial_port(serial_port);
  xemu_flag = mega65_peek(0xffd360f) & 0x20 ? 0 : 1;

  rxbuff_detect();
  monitor_sync();

  real_stop_cpu();

  // Seek to track 0, then to track 37
  goto_track0();
  for(int i=0;i<35;i++) {
    mega65_poke(0xffD3081, 0x18);
    while (mega65_peek(0xffd3082) & 0x80)
      continue;
  }
  
  for(int track=38;track<41;track++) {

    // Step one track
    mega65_poke(0xffD3081, 0x18);
    while (mega65_peek(0xffd3082) & 0x80)
      continue;    

    usleep(50000);
    printf("Current track is T:%02x, H:%02x\n",mega65_peek(0xffd36a3),mega65_peek(0xffd36a5));
    
    for(int side=0;side<2;side++) {
      // Allow a little bit of time for a sector to pass under the head,
      // so that we can check the track and head, and step if we need      
      
      for(int sector=1;sector<=10;sector++) {
	fprintf(stdout,"Reading T:%02x, S:%02x, H:%02x\n",
		track,sector,side);
	int r=read_a_sector(track,side,sector);
      }
    }
  }
  // Floppy motor off
  mega65_poke(0xffD3080, 0x00);

  
  start_cpu();
  do_exit(0);
}

void do_exit(int retval)
{
#ifndef WINDOWS
  if (thread_count) {
    timestamp_msg("");
    fprintf(stderr, "Background tasks may be running. CONTROL+C to stop...\n");
    for (int i = 0; i < thread_count; i++)
      pthread_join(threads[i], NULL);
  }
#endif
  close_default_tcp_port();
  exit(retval);
}
