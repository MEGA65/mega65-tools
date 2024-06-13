/*
  Common routines for communicating with the MEGA65 via the serial
  monitor interface

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
#ifndef WINDOWS
#include <pthread.h> // msys2-mingw64 doesn't have pthread.h, so had to drop it in here
#ifndef __APPLE__
#include <sys/ioctl.h>
#include <linux/serial.h>
#include <linux/tty_flags.h>
#endif
#endif

#ifdef WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <arpa/inet.h>
#endif

#include <libusb.h>

#include <m65common.h>
#include <logging.h>
#include <fpgajtag.h>

#define SLOW_FACTOR 1
#define SLOW_FACTOR2 1

int debug_serial = 0;
int xemu_flag = 0;

#ifdef WINDOWS
#include <windows.h>
#undef SLOW_FACTOR
#define SLOW_FACTOR 1
#define SLOW_FACTOR2 1
// #define do_usleep usleep
void do_usleep(__int64 usec)
{
  HANDLE timer;
  LARGE_INTEGER ft;

  ft.QuadPart = -(10 * usec); // Convert to 100 nanosecond interval, negative value indicates relative time

  timer = CreateWaitableTimer(NULL, TRUE, NULL);
  SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
  WaitForSingleObject(timer, INFINITE);
  CloseHandle(timer);
}

#else
#include <termios.h>
void do_usleep(unsigned long usec)
{
  //  printf("do_usleep(%ld)\n",usec);
  usleep(usec);
}
#endif

#ifdef WINDOWS

FILE iobs[3];

/* This seems to exist in MSYS2-Mingw64, in "mingw-w64-crt-git/src/mingw-w64/mingw-w64-crt/stdio/acrt_iob_func.c" */
/* I'll comment it out for my install, but will leave it up to you in the PR if you want to comment it out or if it's needed
 * on your side */
/*
FILE *__imp___acrt_iob_func(void)
{
  iobs[0]=*stdin;
  iobs[1]=*stdout;
  iobs[2]=*stderr;
  return iobs;
}*/
#endif

time_t start_time = 0;

int no_rxbuff = 1;

int saw_c64_mode = 0;
int saw_c65_mode = 0;
int saw_openrom = 0;

int serial_speed = 2000000;

int serial_port_is_tcp = 0;

int cpu_stopped = 0;

// 0 = old hard coded monitor, 1= Kenneth's 65C02 based fancy monitor
// Only REALLY old bitstreams don't have the new monitor
int new_monitor = 1;

#ifdef WINDOWS
PORT_TYPE fd = { WINPORT_TYPE_INVALID, INVALID_HANDLE_VALUE, INVALID_SOCKET };
#else
PORT_TYPE fd = -1;
#endif

int do_slow_write(PORT_TYPE fd, char *d, int l, const char *func, const char *file, const int line)
{
  // UART is at 2Mbps, but we need to allow enough time for a whole line of
  // writing. 100 chars x 0.5usec = 500usec. So 1ms between chars should be ok.
  int i;
  if (debug_serial && 0) {
    printf("\nWriting ");
    for (i = 0; i < l; i++) {
      if (d[i] >= ' ')
        printf("%c", d[i]);
      else
        printf("[$%02X]", d[i]);
    }
    printf("\n");
    char line[1024];
    fgets(line, 1024, stdin);
  }

  for (i = 0; i < l; i++) {
    if (no_rxbuff) {
      if (serial_speed == 4000000)
        do_usleep(1000 * SLOW_FACTOR);
      else
        do_usleep(2000 * SLOW_FACTOR);
    }
    int w = do_serial_port_write(fd, (unsigned char *)&d[i], 1, func, file, line);
    while (w < 1) {
      if (no_rxbuff) {
        if (serial_speed == 4000000)
          do_usleep(500 * SLOW_FACTOR);
        else
          do_usleep(1000 * SLOW_FACTOR);
      }
      w = do_serial_port_write(fd, (unsigned char *)&d[i], 1, func, file, line);
    }
  }
  return 0;
}

void do_write(PORT_TYPE localfd, char *str)
{
  int len = strlen(str);
  do_serial_port_write(localfd, (uint8_t *)str, len, NULL, NULL, 0);
}

int do_read(PORT_TYPE localfd, char *str, int max)
{
  return do_serial_port_read(localfd, (uint8_t *)str, max, NULL, NULL, 0);
}

int do_slow_write_safe(PORT_TYPE fd, char *d, int l, const char *func, const char *file, int line)
{
  // There is a bug at the time of writing that causes problems
  // with the CPU's correct operation if various monitor commands
  // are run when the CPU is running.
  // Stopping the CPU before and then resuming it after running a
  // command solves the problem.
  // The only problem then is if we have a breakpoint set (like when
  // getting ready to load a program), because we might accidentally
  // resume the CPU when it should be stopping.
  // (We can work around this by using the fact that the new UART
  // monitor tells us when a breakpoint has been reached.
  if (!cpu_stopped)
    do_slow_write(fd, "t1\r", 3, func, file, line);
  do_slow_write(fd, d, l, func, file, line);
  if (!cpu_stopped) {
    //    printf("Resuming CPU after writing string\n");
    do_slow_write(fd, "t0\r", 3, func, file, line);
  }
  return 0;
}

// From os.c in serval-dna
long long gettime_us()
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

int pending_vf011_read = 0;
int pending_vf011_write = 0;
int pending_vf011_device = 0;
int pending_vf011_track = 0;
int pending_vf011_sector = 0;
int pending_vf011_side = 0;
unsigned char recent_bytes[4] = { 0, 0, 0, 0 };

void check_for_vf011_jobs(unsigned char *read_buff, int b)
{
  // Check for Virtual F011 requests coming through
  for (int i = 3; i < b; i++) {
    if (read_buff[i] == '!') {
      if ((read_buff[i - 1] & read_buff[i - 2] & read_buff[i - 3]) & 0x80) {
        pending_vf011_read = 1;
        pending_vf011_device = 0;
        pending_vf011_track = read_buff[i - 3] & 0x7f;
        pending_vf011_sector = read_buff[i - 2] & 0x7f;
        pending_vf011_side = read_buff[i - 1] & 0x0f;
      }
    }
    if (read_buff[i] == 0x5c) {
      if ((read_buff[i - 1] & read_buff[i - 2] & read_buff[i - 3]) & 0x80) {
        pending_vf011_write = 1;
        pending_vf011_device = 0;
        pending_vf011_track = read_buff[i - 3] & 0x7f;
        pending_vf011_sector = read_buff[i - 2] & 0x7f;
        pending_vf011_side = read_buff[i - 1] & 0x0f;
      }
    }
  }
}

void wait_for_prompt(void)
{
  unsigned char read_buff[8192];
  int b = 1;
  int offset = 0;
  while (1) {
    b = serialport_read(fd, read_buff + offset, 1);
    // if (b > 0) dump_bytes(0, "wait_for_prompt", read_buff, b + offset);
    if (b < 0 || b > 8191)
      continue;
    read_buff[b + offset] = 0;

    check_for_vf011_jobs(read_buff, b);

    if (b > 0)
      offset += b;

    if (strstr((char *)read_buff, "!\r\n")) {
      // Watch or break point triggered.
      // There is a bug in the MEGA65 where it sometimes reports
      // this repeatedly. It is worked around by simply sending a newline.
      printf("WARNING: Break or watchpoint trigger seen.\n");
      serialport_write(fd, (uint8_t *)"\r", 1);
    }
    if (strstr((char *)read_buff, ".")) {
      break;
    }
  }
}

void wait_for_string(char *s)
{
  unsigned char read_buff[8192];
  int b = 1;
  int offset = 0;
  while (1) {
    b = serialport_read(
        fd, read_buff + offset, 1); // let's try read it one byte at a time, to assure we don't read more than necessary
    // if (b > 0) dump_bytes(0, "wait_for_string", read_buff, b + offset);
    if (b < 0 || b > 8191)
      continue;
    read_buff[b + offset] = 0;

    check_for_vf011_jobs(read_buff, b);

    if (b > 0)
      offset += b;

    if (strstr((char *)read_buff, s)) {
      break;
    }
  }
}

void purge_input(void)
{
  unsigned char read_buff[8192];
  int b = 1;
  while (b > 0) {
    b = serialport_read(fd, read_buff, 8191);
  }
}

unsigned long long gettime_ms()
{
  struct timeval nowtv;
  // If gettimeofday() fails or returns an invalid value, all else is lost!
  if (gettimeofday(&nowtv, NULL) == -1)
    perror("gettimeofday");
  return nowtv.tv_sec * 1000LL + nowtv.tv_usec / 1000;
}

void purge_and_check_for_vf011_jobs(int stopP)
{
  char recent[4];
  time_t start = time(0);

  if (!no_rxbuff) {
    unsigned char read_buff[8192];
    int b = 1;
    recent[0] = 0;
    recent[1] = 0;
    recent[2] = 0;
    recent[3] = 0;
    while (1) {
      b = serialport_read(fd, read_buff, 8191);

      check_for_vf011_jobs(read_buff, b);

      for (int i = 0; i < b; i++) {
        recent_bytes[0] = recent_bytes[1];
        recent_bytes[1] = recent_bytes[2];
        recent_bytes[2] = recent_bytes[3];
        recent_bytes[3] = read_buff[i];
      }
      for (int i = 0; i < b; i++) {
        recent[0] = recent[1];
        recent[1] = recent[2];
        recent[2] = read_buff[i];
        if (stopP) {
          //	  dump_bytes(0,"recent looking for t1\r",recent,3);
          if (strstr((char *)recent, "t1\r")) {
            cpu_stopped = 1;
            return;
          }
        }
        else {
          //	  dump_bytes(0,"recent looking for t0\r",recent,3);
          if (strstr((char *)recent, "t0\r")) {
            cpu_stopped = 0;
            return;
          }
        }
      }

      // Safety catch in case first command was missed
      if (time(0) >= (start + 1)) {
        if (stopP)
          slow_write(fd, "t1\r", 3);
        else
          slow_write(fd, "t0\r", 3);
        start = time(0);
      }
    }
  }
}

// NOTE: It was discovered late that this function did not actually stop the cpu
// (due to a call to slow_write_safe() which turns trace-mode off at the end anyway ('t0'))
//
// An initial attempt was made to repair the function to genuinely stop the cpu, but tools
// like mega65_ftp (and perhaps others) were adversely affected by this. They seemed to rely
// on this 'broken' behaviour that did not actually stop the cpu.
//
// Given that, this broken function and its usages have been renamed to 'fake_stop_cpu()'
// in order to highlight the broken nature of the function to other developers.
//
// Developers are advised to consider switching any usages they encounter to use 'real_stop_cpu()'
// instead, and assess the tool for any adverse impact before committing.
int fake_stop_cpu(void)
{
  if (cpu_stopped) {
    //    log_debug("CPU already stopped.");
    return 1;
  }
  // Stop CPU
  //  log_debug("Stopping CPU");
  if (no_rxbuff)
    do_usleep(50000);
  slow_write_safe(fd, "t1\r", 3);
  purge_and_check_for_vf011_jobs(1);
  return 0;
}

// NOTE: This function genuinely stops the cpu. As you switch over from fake_stop_cpu()
// to make use of this function, please be to assure the tool you are working on isn't
// adversely impacted by the genuine stopping of the cpu.
int real_stop_cpu(void)
{
  if (cpu_stopped) {
    //    log_debug("CPU already stopped.");
    return 1;
  }
  // Stop CPU
  // log_debug("Stopping CPU");
  if (no_rxbuff)
    do_usleep(50000);
  cpu_stopped = 1;
  slow_write(fd, "t1\r", 3);
  purge_and_check_for_vf011_jobs(1);
  return 0;
}

int start_cpu(void)
{
  // Stop CPU
  if (cpu_stopped) {
    //    log_debug("Starting CPU\n");
  }
  if (no_rxbuff)
    do_usleep(50000);
  slow_write(fd, "t0\r", 3);
  purge_and_check_for_vf011_jobs(0);
  return 0;
}

int load_file(char *filename, int load_addr, int patchHyppo)
{
  char cmd[1024];

  FILE *f = fopen(filename, "rb");
  if (!f) {
    log_crit("could not open file '%s'", filename);
    exit(-2);
  }

  if (no_rxbuff)
    do_usleep(50000);
  unsigned char buf[65536];
  int max_bytes;
  int byte_limit = 4096;
  max_bytes = 0x10000 - (load_addr & 0xffff);
  if (max_bytes > byte_limit)
    max_bytes = byte_limit;
  int b = fread(buf, 1, max_bytes, f);
  while (b > 0) {
    if (patchHyppo) {
      log_debug("patching hyppo...");
      // Look for BIT $nnnn / BIT $1234, and change to JMP $nnnn to skip
      // all SD card activities
      for (int i = 0; i < (b - 5); i++) {
        if ((buf[i] == 0x2c) && (buf[i + 3] == 0x2c) && (buf[i + 4] == 0x34) && (buf[i + 5] == 0x12)) {
          log_debug("patching Hyppo @ $%04x to skip SD card and ROM checks", 0x8000 + i);
          buf[i] = 0x4c;
        }
      }
    }
    log_debug("read to $%04x (%d bytes)", load_addr, b);
    // load_addr=0x400;
    // XXX - The l command requires the address-1, and doesn't cross 64KB boundaries.
    // Thus writing to $xxx0000 requires adding 64K to fix the actual load address
    int munged_load_addr = load_addr;
    if ((load_addr & 0xffff) == 0x0000) {
      munged_load_addr += 0x10000;
    }

#ifdef WINDOWS_GUS
    // Windows doesn't seem to work with the l fast-load monitor command
    log_debug("asking Gus to write data...");
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
    // The old uart monitor could handle being given a 28-bit address for the end address,
    // but Kenneth's implementation requires it be a 16 bit address.
    // Also, Kenneth's implementation doesn't need the -1, so we need to know which version we
    // are talking to.
    if (new_monitor)
      sprintf(cmd, "l%x %x\r", load_addr, (load_addr + b) & 0xffff);
    else
      sprintf(cmd, "l%x %x\r", munged_load_addr - 1, (munged_load_addr + b - 1) & 0xffff);
    //    printf("  command ='%s'\n  b=%d\n",cmd,b);
    slow_write(fd, cmd, strlen(cmd));
    int n = b;
    unsigned char *p = buf;
    while (n > 0) {
      int w = serialport_write(fd, p, n);
      if (w > 0) {
        p += w;
        n -= w;
      }
      else
        do_usleep(1000);
    }
    wait_for_prompt();
#endif

    load_addr += b;

    max_bytes = 0x10000 - (load_addr & 0xffff);
    if (max_bytes > byte_limit)
      max_bytes = byte_limit;
    b = fread(buf, 1, max_bytes, f);
  }

  fclose(f);
  log_info("file '%s' loaded", filename);
  return 0;
}

int mega65_poke(unsigned int addr, unsigned char value)
{
  return push_ram(addr, 1, &value);
}

unsigned char mega65_peek(unsigned int addr)
{
  unsigned char b;
  fetch_ram(addr, 1, &b);
  return b;
}

int restart_hyppo(void)
{
  // Start executing in new hyppo
  printf("Re-Starting CPU in new HYPPO\n");
  if (!no_rxbuff)
    do_usleep(50000);
  purge_input();
  slow_write(fd, "g8100\r", 6);
  wait_for_prompt();
  slow_write(fd, "t0\r", 3);
  wait_for_prompt();
  cpu_stopped = 0;

  return 0;
}

void print_spaces(FILE *f, int col)
{
  for (int i = 0; i < col; i++)
    fprintf(f, " ");
}

int dump_bytes(int col, char *msg, unsigned char *bytes, int length)
{
  print_spaces(stderr, col);
  fprintf(stderr, "%s:\n", msg);
  for (int i = 0; i < length; i += 16) {
    print_spaces(stderr, col);
    fprintf(stderr, "%04X: ", i);
    for (int j = 0; j < 16; j++)
      if (i + j < length)
        fprintf(stderr, " %02X", bytes[i + j]);
      else
        fprintf(stderr, " | ");
    fprintf(stderr, "  ");
    for (int j = 0; j < 16; j++)
      if (i + j < length)
        fprintf(stderr, "%c", (bytes[i + j] >= ' ' && bytes[i + j] < 0x7c) ? bytes[i + j] : '.');

    fprintf(stderr, "\n");
  }
  return 0;
}

int stuff_keybuffer(char *s)
{
  int buffer_addr = 0x277;
  int buffer_len_addr = 0xc6, i;
  char cmd[1024], *pos;

  if (saw_c65_mode) {
    buffer_addr = 0x2b0;
    buffer_len_addr = 0xd0;
  }

  log_concat(NULL);
  log_concat("injecting string into key buffer at $%04X: ");
  for (int i = 0; s[i]; i++) {
    if (s[i] >= ' ' && s[i] < 0x7c)
      log_concat("%c", s[i]);
    else
      log_concat("[$%02x]", s[i]);
  }
  log_debug(NULL);

  snprintf(cmd, 1024, "s%x 0\r", buffer_len_addr);
  slow_write(fd, cmd, strlen(cmd));
  usleep(25000);

  // only use 9 of the 10 chars in the keybuffer
  // using all 10 makes weird chars on the 1st call
#define MAX_KEYBUFFER_CHARS 9
  pos = s;
  while (*pos) {
    snprintf(cmd, 1024, "s%04x", buffer_addr);
    for (i = 0; i < MAX_KEYBUFFER_CHARS && pos[i]; i++) {
      snprintf(cmd + 5 + i * 3, 6, " %02x", toupper(pos[i]));
    }
    log_debug("stuff_keybuffer: '%s' %d", cmd, i);
    snprintf(cmd + 5 + i * 3, 40, "\rs%x %d\r", buffer_len_addr, i);
    slow_write(fd, cmd, strlen(cmd));
    if (i == MAX_KEYBUFFER_CHARS)
      usleep(25000);
    pos = pos + i;
  }
  return 0;
}

int read_and_print(PORT_TYPE fd)
{
  char buff[8192];
  int r = serialport_read(fd, (unsigned char *)buff, 8192);
  buff[r] = 0;
  printf("%s\n", buff);
  return 0;
}

int rxbuff_detect(void)
{
  /*
    Newer bitstreams finally have buffering on the serial monitor interface.
    If detected, this means that we can send commands a lot faster.
  */
  unsigned char read_buff[8193];

  // If running with xemu, assume no rxbuff available (for now)
  if (xemu_flag)
    return !no_rxbuff;

  monitor_sync();
  // Send two commands one after the other with no delay.
  // If we have RX buffering, both commands will execute.
  // If not, then only the first one will execute
  log_info("checking if MEGA65 has RX buffer");
  serialport_write(fd, (unsigned char *)"\025m0\rm1\r", 7);
  do_usleep(20000); // Give plenty of time for things to settle
  int b = 1;
  while (b > 0) {
    b = serialport_read(fd, read_buff, 8192);
    if (b >= 0)
      read_buff[b] = 0;
    //    dump_bytes(0,"bytes from serial port",read_buff,b);
    if ((strstr((char *)read_buff, ":00000000:")) && (strstr((char *)read_buff, ":00000001:"))) {
      no_rxbuff = 0;
      log_info("RX buffer detected. Latency will be reduced.");
      break;
    }
  }
  return !no_rxbuff;
}

int monitor_sync(void)
{
  /* Synchronise with the monitor interface.
     Send #<token> until we see the token returned to us.
  */

  unsigned char read_buff[8192];
  int b = 1;

  // Begin by sending a null command and purging input
  char cmd[8192];
  cmd[0] = 0x15; // ^U
  cmd[1] = '#';  // prevent instruction stepping
  cmd[2] = 0x0d; // Carriage return
  purge_input();
  if (no_rxbuff)
    do_usleep(20000); // Give plenty of time for things to settle
  slow_write_safe(fd, cmd, 3);
  //  printf("Wrote empty command.\n");
  if (no_rxbuff) {
    do_usleep(20000); // Give plenty of time for things to settle
    purge_input();
  }
  else {
    wait_for_prompt();
    purge_input();
  }

  char recent[16];
  bzero(recent, 16);

  for (int tries = 0; tries < 10; tries++) {
#ifdef WINDOWS
    snprintf(cmd, 1024, "#%08x\r", rand());
#else
    snprintf(cmd, 1024, "#%08lx\r", random());
#endif
    //    printf("Writing token: '%s'\n",cmd);
    slow_write_safe(fd, cmd, strlen(cmd));

    time_t start = time(0);

    while (1) {
      if (no_rxbuff)
        do_usleep(10000 * SLOW_FACTOR);
      b = serialport_read(fd, read_buff, 8192);
      if (b < 0)
        b = 0;
      if (b < 1) {
        do_usleep(100);
        b = serialport_read(fd, read_buff, 8192);
        if (b < 0)
          b = 0;
      }
      if (b < 1) {
        do_usleep(1000);
        b = serialport_read(fd, read_buff, 8192);
        if (b < 0)
          b = 0;
      }
      if (b > 8191)
        b = 8191;
      read_buff[b] = 0;

      if ((time(0) - start) > 1) {
        // Seems to be locked up.
        // It could be that someone had a half-finished s command in progress.
        // So write 64K x 0
        char zeroes[256];
        bzero(zeroes, 256);
        for (int i = 0; i < 256; i++)
          serialport_write(fd, (unsigned char *)zeroes, 256);

        slow_write_safe(fd, cmd, strlen(cmd));

        start = time(0);
      }

      for (int n = 0; n < b; n++) {
        for (int j = 0; j < 15; j++)
          recent[j] = recent[j + 1];
        recent[15] = read_buff[n];

        //	dump_bytes(0,"recent",recent,strlen(cmd));

        // Token present?
        if (!strncmp(cmd, &recent[16 - strlen(cmd)], strlen(cmd)))
          return 0;
      }

      //      if (b>0) dump_bytes(2,"Sync input",read_buff,b);
    }
    if (no_rxbuff)
      do_usleep(10000 * SLOW_FACTOR);
    else
      do_usleep(1000);
  }
  log_warn("failed to synchronise with the monitor");
  return 1;
}

int get_pc(void)
{
  /*
    Get current programme counter value of CPU
  */
  if (!no_rxbuff)
    monitor_sync();

  slow_write_safe(fd, "r\r", 2);
  if (no_rxbuff)
    do_usleep(50000);
  else
    do_usleep(2000);
  unsigned char buff[8192];
  int b = serialport_read(fd, buff, 8192);
  if (b < 0)
    b = 0;
  if (b > 8191)
    b = 8191;
  buff[b] = 0;
  //  if (b>0) dump_bytes(2,"PC read input",buff,b);
  char *s = strstr((char *)buff, "\n,");
  if (s)
    return strtoll(&s[6], NULL, 16);
  else
    return -1;
}

int in_hypervisor(void)
{
  /*
    Get H flag from register output
  */
  if (!no_rxbuff)
    monitor_sync();

  slow_write_safe(fd, "r\r", 2);
  if (no_rxbuff)
    do_usleep(50000);
  else
    do_usleep(2000);
  unsigned char buff[8192];
  int b = serialport_read(fd, buff, 8192);
  if (b < 0)
    b = 0;
  if (b > 8191)
    b = 8191;
  buff[b] = 0;
  //  if (b>0) dump_bytes(2,"H read input",buff,b);
  char *s = strstr((char *)buff, " H ");
  if (s)
    return 1;
  else
    return 0;
}

int breakpoint_pc = -1;
int breakpoint_set(int pc)
{
  char cmd[8192];
  monitor_sync();
  start_cpu();
  snprintf(cmd, 8192, "b%x\r", pc);
  breakpoint_pc = pc;
  slow_write(fd, cmd, strlen(cmd));
  // XXX any t0 or t1 cancels a queued breakpoint,
  // so must be avoided
  return 0;
}

int breakpoint_wait(void)
{
  char read_buff[8192];
  char pattern[16];
  time_t start = time(0);

  snprintf(pattern, 16, "\n,077");

  int match_state = 0;

  // Now read until we see the requested PC
  log_info("waiting for breakpoint at $%04X to trigger", breakpoint_pc);
  while (1) {
    int b = serialport_read(fd, (unsigned char *)read_buff, 8192);

    // Poll for PC value, as sometimes breakpoint doesn't always auto-present it
    if (time(0) != start) {
      slow_write(fd, "r\r", 2);
      start = time(0);
    }

    for (int i = 0; i < b; i++) {
      if (read_buff[i] == pattern[match_state]) {
        if (match_state == 4) {
          log_info("breakpoint @ $%04X triggered", breakpoint_pc);
          slow_write(fd, "t1\r", 3);
          cpu_stopped = 1;
          //	  printf("stopped following breakpoing.\n");
          return 0;
        }
        else
          match_state++;
      }
      else {
        match_state = 0;
      }
    }
    //    if (b>0) dump_bytes(2,"Breakpoint wait input",read_buff,b);
  }
}

int push_ram(unsigned long address, unsigned int count, unsigned char *buffer)
{
  //  fprintf(stderr,"Pushing %d bytes to RAM @ $%07lx\n",count,address);

  int cpu_stopped_state = cpu_stopped;

  // We have to stop the CPU first, so that the serial monitor can keep up with
  // the full 2mbit/sec data rate (as otherwise the CPU can block the serial
  // monitor processor for some number of cycles per character while it finishes
  // instructions.
  if (!cpu_stopped_state)
    real_stop_cpu();

  char cmd[8192];
  for (unsigned int offset = 0; offset < count;) {
    int b = count - offset;
    // Limit to same 64KB slab
    if (b > (0xffff - ((address + offset) & 0xffff)))
      b = (0xffff - ((address + offset) & 0xffff));
    if (b > 4096)
      b = 4096;

    if (count == 1) {
      //	fprintf(stderr,"Writing single byte\n");
      sprintf(cmd, "s%lx %x\r", address, buffer[0]);
      slow_write_safe(fd, cmd, strlen(cmd));
      if (no_rxbuff)
        do_usleep(1000 * SLOW_FACTOR);
    }
    else {
      if (new_monitor)
        sprintf(cmd, "l%lx %lx\r", address + offset, (address + offset + b) & 0xffff);
      else
        sprintf(cmd, "l%lx %lx\r", address + offset - 1, address + offset + b - 1);
      slow_write_safe(fd, cmd, strlen(cmd));
      if (no_rxbuff)
        do_usleep(1000 * SLOW_FACTOR);
      if (xemu_flag)
        do_usleep(50000 * SLOW_FACTOR);
      int n = b;
      unsigned char *p = &buffer[offset];
      while (n > 0) {
        int w = serialport_write(fd, p, n);
        if (w > 0) {
          p += w;
          n -= w;
          if (xemu_flag)
            do_usleep(50000 * SLOW_FACTOR);
        }
        else {
          do_usleep(1000 * SLOW_FACTOR);
        }
      }
    }
    wait_for_string(cmd);
    wait_for_prompt();
    offset += b;
  }
  if (!cpu_stopped_state)
    start_cpu();
  return 0;
}

char parse_byte(const unsigned char *source, unsigned char *target)
{
  unsigned char val = 0;
  if (source[0] >= '0' && source[0] <= '9')
    val = ((source[0] - 48) << 4);
  else if (source[0] >= 'A' && source[0] <= 'F')
    val = ((source[0] - 55) << 4);
  else if (source[0] >= 'a' && source[0] <= 'f')
    val = ((source[0] - 87) << 4);
  else
    return -1;
  if (source[1] >= '0' && source[1] <= '9')
    val |= (source[1] - 48);
  else if (source[1] >= 'A' && source[1] <= 'F')
    val |= (source[1] - 55);
  else if (source[1] >= 'a' && source[1] <= 'f')
    val |= (source[1] - 87);
  else
    return -1;
  *target = val;
  return 0;
}

int fetch_ram(unsigned long address, unsigned int count, unsigned char *buffer)
{
  /* Fetch a block of RAM into the provided buffer.
     This greatly simplifies many tasks.
  */

  unsigned long addr = address;
  unsigned long end_addr;
  char cmd[80], *found;
  unsigned char read_buff[8192];
  char next_addr_str[8192];
  int ofs = 0, s_offset;

  //  fprintf(stderr,"Fetching $%x bytes @ $%lx\n",count,address);

  //  monitor_sync();
  time_t last_rx = 0;
  while (addr < (address + count)) {
    if ((last_rx < time(0)) || (addr == end_addr)) {
      //	printf("no response for 1 sec: Requesting more.\n");
      if ((address + count - addr) < 17) {
        snprintf(cmd, 79, "m%X\r", (unsigned int)addr);
        end_addr = addr + 0x10;
      }
      else {
        snprintf(cmd, 79, "M%X\r", (unsigned int)addr);
        end_addr = addr + 0x100;
      }
      //	printf("Sending '%s'\n",cmd);
      slow_write_safe(fd, cmd, strlen(cmd));
      last_rx = time(0);
    }
    int b = serialport_read(fd, &read_buff[ofs], 8191 - ofs);
    if (b <= 0)
      b = 0;
    //            else
    //	      printf("%s\n", read_buff);
    if ((ofs + b) > 8191)
      b = 8191 - ofs;
    //      if (b) dump_bytes(0,"read data",&read_buff[ofs],b);
    read_buff[ofs + b] = 0;
    ofs += b;
    snprintf(next_addr_str, 8192, "\n:%08X:", (unsigned int)addr);
    found = strstr((char *)read_buff, next_addr_str);
    if (found && (strlen(found) >= 43)) {
      /* debug
      char b = s[43];
      s[43] = 0;
      log_debug("found data for $%08x: %s", (unsigned int)addr, s);
      s[43] = b;
      */
      for (int i = 0; i < 16; i++) {

        // Don't write more bytes than requested
        if ((addr - address + i) >= count)
          break;

        if (parse_byte((unsigned char *)&found[11 + i * 2], &buffer[addr - address + i])) {
          log_debug("fetch_ram: error parsing %s", found);
        }
      }
      addr += 16;

      // Shuffle buffer down
      s_offset = (long long)found - (long long)read_buff + 43;
      bcopy(&read_buff[s_offset], &read_buff[0], 8192 - (ofs - s_offset));
      ofs -= s_offset;
    }
  }
  if (addr >= (address + count)) {
    // log_debug("fetch_ram: read complete at $%08lx\n", addr);
    return 0;
  }
  else {
    // TODO: better return -1 and let caller decide?
    log_crit("could not read requested memory region.");
    exit(-1);
  }
}

unsigned char ram_cache[512 * 1024 + 255];
unsigned char ram_cache_valids[512 * 1024 + 255];
int ram_cache_initialised = 0;

int fetch_ram_invalidate(void)
{
  ram_cache_initialised = 0;
  return 0;
}

int fetch_ram_cacheable(unsigned long address, unsigned int count, unsigned char *buffer)
{
  if (!ram_cache_initialised) {
    ram_cache_initialised = 1;
    bzero(ram_cache_valids, 512 * 1024);
    bzero(ram_cache, 512 * 1024);
  }
  if ((address + count) >= (512 * 1024)) {
    return fetch_ram(address, count, buffer);
  }

  // See if we need to fetch it fresh
  for (int i = 0; i < count; i++) {
    if (!ram_cache_valids[address + i]) {
      // Cache not valid here -- so read some data
      //      printf("Fetching $%08x for cache.\n",address);
      fetch_ram(address, 256, &ram_cache[address]);
      for (int j = 0; j < 256; j++)
        ram_cache_valids[address + j] = 1;

      bcopy(&ram_cache[address], buffer, count);
      return 0;
    }
  }

  // It's valid in the cache
  bcopy(&ram_cache[address], buffer, count);
  return 0;
}

time_t last_settle_msg_time = 0;

int detect_mode(void)
{
  /*
    Set saw_c64_mode or saw_c65_mode according to what we can discover.
    We can look at the C64/C65 charset bit in $D030 for a good clue.
    But we also really want to know that the CPU is in the keyboard
    input loop for either of the modes, if possible. OpenROMs being
    under development makes this tricky.
  */
  saw_c65_mode = 0;
  saw_c64_mode = 0;
  saw_openrom = 0;

  unsigned char mem_buff[8192];

  // Look for OpenROMs
  fetch_ram(0x20010, 16, mem_buff);
  if (mem_buff[0] == 'V' || mem_buff[0] == 'O') {
    mem_buff[9] = 0;
    int date_code = atoi((const char *)&mem_buff[1]);
    if (date_code > 2000000) {
      log_debug("detected OpenROM version %d\n", date_code);
      saw_openrom = 1;
      saw_c64_mode = 1;
      return 0;
    }
  }

  while (in_hypervisor())
    do_usleep(1000);

  while (1) {

    fetch_ram(0xffd3030, 1, mem_buff);
    while (mem_buff[0] & 0x01) {
      if (last_settle_msg_time != time(0)) {
        log_debug("waiting for MEGA65 KERNAL/OS to settle...");
        last_settle_msg_time = time(0);
      }
      if (no_rxbuff)
        do_usleep(200000);
      fetch_ram(0xffd3030, 1, mem_buff);
    }

    // Wait for HYPPO to exit
    int d054 = mega65_peek(0xffd3054);
    while (d054 & 7) {
      if (no_rxbuff)
        do_usleep(5000);
      else
        do_usleep(500);
      d054 = mega65_peek(0xffd3054);
    }

    fetch_ram(0xffd3030, 1, mem_buff);
    if (mem_buff[0] == 0x64) {
      saw_c65_mode = 1;
      log_debug("in C65 mode");
      return 0;
    }

    // Use screen address to guess mode
    fetch_ram(0xffd3060, 3, mem_buff);
    if (mem_buff[1] == 0x04) {
      log_debug("screen is at $0400");
      // check $01 port value
      fetch_ram(0x7770001, 1, mem_buff);
      log_debug("port $01 contains $%02x", mem_buff[0]);
      if ((mem_buff[0] & 0xf) == 0x07) {
        saw_c64_mode = 1;
        log_debug("in C64 mode");
        return 0;
      }
    }
    if (mem_buff[1] == 0x08) {
      saw_c65_mode = 1;
      log_debug("in C65 mode");
      return 0;
    }
  }

#if 0
  //  printf("$D030 = $%02X\n",mem_buff[0]);
  if (mem_buff[0]==0x64) {
    // Probably C65 mode
    int in_range=0;
    // Allow more tries to allow more time for ROM checksum to finish
    // or boot attempt from floppy to finish
    for (int i=0;i<10;i++) {
      int pc=get_pc();
      if (pc>=0xe1ae&&pc<=0xe1b4) in_range++; else {
	// C65 ROM does checksum, so wait a while if it is in that range
	if (pc>=0xb000&&pc<0xc000) sleep(1);
	// Or booting from internal drive is also slow
	if (pc>=0x9c00&&pc<0x9d00) sleep(1);
	// Or something else it does while booting
	if (pc>=0xfeb0&&pc<0xfed0) sleep(1);
	else {
	  //	  log_debug(stderr,"Odd PC=$%04x",pc);
	  do_usleep(5000);
	}
      }
    }
    if (in_range>3) {
      // We are in C65 BASIC main loop, so assume it is C65 mode
      saw_c65_mode=1;
      log_debug("CPU in C65 BASIC 10 main loop.");
      return 0;
    }
  } else if (mem_buff[0]==0x00) {
    // Probably C64 mode
    int in_range=0;
    for (int i=0;i<5;i++) {
      int pc=get_pc();
      // XXX Might not work with OpenROMs?
      if (pc>=0xe5cd&&pc<=0xe5d5) in_range++;
      else {
	log_debug("odd PC=$%04x",pc);
	do_usleep(5000);
      }
    }
    if (in_range>3) {
      // We are in C64 BASIC main loop, so assume it is C65 mode
      saw_c64_mode=1;
      log_debug("CPU in C64 BASIC 2 main loop.");
      return 0;
    }
  }
#endif

  log_debug("could not determine C64/C65/MEGA65 mode");
  return 1;
}

int last_read_count = 0;

#ifdef WINDOWS

unsigned char win_err_use_neutral = 0;
void print_error(const char *context)
{
  DWORD error_code = GetLastError(), size;
  char buffer[256];
  if (!win_err_use_neutral) {
    size = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK, NULL,
        error_code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPSTR)buffer, 256, NULL);
    if (size == 0)
      win_err_use_neutral = 1;
  }
  // fallback to system language if US English is not available
  if (win_err_use_neutral)
    size = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK, NULL,
        error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)buffer, 256, NULL);
  if (size == 0) {
    log_debug("%s: error code %ld", context, error_code);
    return;
  }
  log_debug("%s: %s", context, buffer);
}

// Opens the specified serial port, configures its timeouts, and sets its
// baud rate.  Returns a handle on success, or INVALID_HANDLE_VALUE on failure.
HANDLE open_serial_port(const char *device, uint32_t baud_rate)
{
  // COM10+ need to have \\.\ added to the front
  // (see
  // https://support.microsoft.com/en-us/topic/howto-specify-serial-ports-larger-than-com9-db9078a5-b7b6-bf00-240f-f749ebfd913e
  // and https://github.com/MEGA65/mega65-tools/issues/48)
  char device_with_prefix[8192];
  snprintf(device_with_prefix, 8192, "\\\\.\\%s", device);

  HANDLE port = CreateFileA(
      device_with_prefix, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (port == INVALID_HANDLE_VALUE) {
    print_error(device);
    return INVALID_HANDLE_VALUE;
  }

  // Consider making the serial buffers bigger? They default to 8192?
  // - https://stackoverflow.com/questions/54313240/why-is-my-serial-read-on-windows-com-port-limited-to-8192-bytes
  // That SO thread suggests making use of SetupComm() to specify the buffer-size you want:
  // - https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setupcomm

  SetupComm(port, 131072, 131072);

  // Flush away any bytes previously read or written.
  BOOL success = FlushFileBuffers(port);
  if (!success) {
    print_error("Failed to flush serial port");
    CloseHandle(port);
    return INVALID_HANDLE_VALUE;
  }

  // Configure read and write operations to time out after 1 ms and 100 ms, respectively.
  COMMTIMEOUTS timeouts = { 0 };
  timeouts.ReadIntervalTimeout = 0;
  timeouts.ReadTotalTimeoutConstant = 1;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 100;
  timeouts.WriteTotalTimeoutMultiplier = 0;

  success = SetCommTimeouts(port, &timeouts);
  if (!success) {
    print_error("Failed to set serial timeouts");
    CloseHandle(port);
    return INVALID_HANDLE_VALUE;
  }

  DCB state;
  state.DCBlength = sizeof(DCB);
  success = GetCommState(port, &state);
  if (!success) {
    print_error("Failed to get serial settings");
    CloseHandle(port);
    return INVALID_HANDLE_VALUE;
  }

  state.fBinary = TRUE;
  state.fDtrControl = DTR_CONTROL_ENABLE;
  state.fDsrSensitivity = FALSE;
  state.fTXContinueOnXoff = FALSE;
  state.fOutX = FALSE;
  state.fInX = FALSE;
  state.fErrorChar = FALSE;
  state.fNull = FALSE;
  state.fRtsControl = RTS_CONTROL_ENABLE;
  state.fAbortOnError = FALSE;
  state.fOutxCtsFlow = FALSE;
  state.fOutxDsrFlow = FALSE;
  state.ByteSize = 8;
  state.StopBits = ONESTOPBIT;
  state.Parity = NOPARITY;

  state.BaudRate = baud_rate;

  success = SetCommState(port, &state);
  if (!success) {
    print_error("Failed to set serial settings");
    CloseHandle(port);
    return INVALID_HANDLE_VALUE;
  }

  return port;
}

int win_serial_port_write(HANDLE port, uint8_t *buffer, size_t size, const char *func, const char *file, const int line)
{
  DWORD offset = 0;
  DWORD written;
  BOOL success;
  //  printf("Calling WriteFile(%d)\n",size);

  if (debug_serial && 0) {
    fprintf(stderr, "%s:%d:%s(): ", file, line, func);
    dump_bytes(0, "serial write (windows)", buffer, size);
  }

  while (offset < size) {
    success = WriteFile(port, &buffer[offset], size - offset, &written, NULL);
    //  printf("  WriteFile() returned.\n");
    if (!success) {
      print_error("Failed to write to port");
      return -1;
    }
    if (written > 0)
      offset += written;
    if (offset < size) {
      // Assume buffer is full, so wait a little while
      //      usleep(1000);
    }
  }
  success = FlushFileBuffers(port);
  if (!success)
    print_error("Failed to flush buffers");
  return size;
}

int win_tcp_write(SOCKET sock, uint8_t *buffer, size_t size, const char *func, const char *file, const int line)
{
  if (debug_serial) {
    fprintf(stderr, "%s:%d:%s(): ", file, line, func);
    dump_bytes(0, "tcp write (windows)", buffer, size);
  }

  int iResult = send(sock, (char *)buffer, size, 0);
  if (iResult == SOCKET_ERROR) {
    printf("send failed with error: %d\n", WSAGetLastError());
    closesocket(sock);
    WSACleanup();
    exit(1);
  }
  int count = iResult;
  return count;
}

// Writes bytes to the serial port, returning 0 on success and -1 on failure.
int do_serial_port_write(WINPORT port, uint8_t *buffer, size_t size, const char *func, const char *file, const int line)
{
  if (port.type == WINPORT_TYPE_FILE)
    return win_serial_port_write(port.fdfile, buffer, size, func, file, line);
  else if (port.type == WINPORT_TYPE_SOCK)
    return win_tcp_write(port.fdsock, buffer, size, func, file, line);
  return 0;
}

// Reads bytes from the serial port.
// Returns after all the desired bytes have been read, or if there is a
// timeout or other error.
// Returns the number of bytes successfully read into the buffer, or -1 if
// there was an error reading.
SSIZE_T win_serial_port_read(HANDLE port, uint8_t *buffer, size_t size, const char *func, const char *file, const int line)
{
  DWORD received = 0;
  //  printf("Calling ReadFile(%I64d)\n",size);
  BOOL success = ReadFile(port, buffer, size, &received, NULL);
  if (last_read_count || received) {
    if (debug_serial) {
      fprintf(stderr, "%s:%d:%s():", file, line, func);
      dump_bytes(0, "serial read (linux)", buffer, received);
    }
  }
  last_read_count = received;
  if (!success) {
    print_error("Failed to read from port");
    return -1;
  }
  //  printf("  ReadFile() returned. Received %ld bytes\n",received);
  return received;
}

SSIZE_T win_tcp_read(SOCKET sock, uint8_t *buffer, size_t size, const char *func, const char *file, const int line)
{
  // check if any bytes available yet, if not, exit early
  unsigned long l;
  ioctlsocket(sock, FIONREAD, &l);
  if (l == 0)
    return 0;

  int iResult = recv(sock, (char *)buffer, size, 0);
  if (iResult == 0 && size != 0) {
    printf("recv: Connection closed.\n");
    exit(1);
  }
  else if (iResult < 0) {
    printf("recv failed with error: %d\n", WSAGetLastError());
    exit(1);
  }

  int count = iResult;
  if (last_read_count || count) {
    if (debug_serial) {
      fprintf(stderr, "%s:%d:%s():", file, line, func);
      dump_bytes(0, "tcp read (windows)", buffer, count);
    }
  }
  last_read_count = count;
  return count;
}

SSIZE_T do_serial_port_read(WINPORT port, uint8_t *buffer, size_t size, const char *func, const char *file, const int line)
{
  if (port.type == WINPORT_TYPE_FILE)
    return win_serial_port_read(port.fdfile, buffer, size, func, file, line);
  else if (port.type == WINPORT_TYPE_SOCK)
    return win_tcp_read(port.fdsock, buffer, size, func, file, line);
  return 0;
}

void close_serial_port(void)
{
  CloseHandle(fd.fdfile);
}

#else
int do_serial_port_write(int fd, uint8_t *buffer, size_t size, const char *function, const char *file, const int line)
{

#ifdef __APPLE__
  if (debug_serial) {
    fprintf(stderr, "%s:%d:%s(): ", file, line, function);
    dump_bytes(0, "serial write (osx)", buffer, size);
  }
  return write(fd, buffer, size);
#else
  size_t offset = 0;
  if (debug_serial) {
    fprintf(stderr, "%s:%d:%s(): ", file, line, function);
    dump_bytes(0, "serial write (linux)", buffer, size);
  }
  while (offset < size) {
    int written = write(fd, &buffer[offset], size - offset);
    if (written > 0)
      offset += written;
    if (offset < size) {
      usleep(1000);
      //      printf("Wrote %d bytes\n",written);
    }
  }
#endif
  return size;
}

size_t do_serial_port_read(int fd, uint8_t *buffer, size_t size, const char *function, const char *file, const int line)
{
  int count;

  if (serial_port_is_tcp) {
#ifndef __APPLE__
    count = recv(fd, buffer, size, MSG_DONTWAIT);
#else
    count = recv(fd, buffer, size, 0);
#endif
  }
  else
    count = read(fd, buffer, size);
  if (last_read_count || count) {
    if (debug_serial) {
      fprintf(stderr, "%s:%d:%s():", file, line, function);
      dump_bytes(0, "serial read (linux/osx)", buffer, count);
    }
  }
  last_read_count = count;
  return count;
}

void set_serial_speed(int fd, int serial_speed)
{
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, NULL) | O_NONBLOCK);
  struct termios t;

  if (fd < 0) {
    log_error("set_serial_speed: invalid fd");
    return;
  }

#ifdef __APPLE__
  /*
   * This code is needed because recent versions of MacOS do not allow
   * setting 'strange' baud rates (like 2000000) via tcsetattr().
   */
  speed_t speed = serial_speed;
  log_debug("set_serial_speed: %d bps (OSX)", serial_speed);
  if (ioctl(fd, IOSSIOSPEED, &speed) == -1) {
    log_error("failed to set output baud rate using IOSSIOSPEED");
  }
  if (tcgetattr(fd, &t))
    log_error("failed to get terminal parameters");
  cfmakeraw(&t);
  // TCSASOFT prevents some fields (most importantly the baud rate)
  // from being changed.  MacOS does not support 'strange' baud rates
  // that might be set by the ioctl above.
  if (tcsetattr(fd, TCSANOW | TCSASOFT, &t))
    log_error("failed to set OSX terminal parameters");

  // Serial port will be unresponsive after returning from FREEZER
  // without this.
  tcflush(fd, TCIFLUSH);
#else
  log_debug("set_serial_speed: %d bps (termios)", serial_speed);
  if (serial_speed == 230400) {
    if (cfsetospeed(&t, B230400))
      log_error("failed to set output baud rate");
    if (cfsetispeed(&t, B230400))
      log_error("failed to set input baud rate");
  }
  else if (serial_speed == 2000000) {
    if (cfsetospeed(&t, B2000000))
      log_error("failed to set output baud rate");
    if (cfsetispeed(&t, B2000000))
      log_error("failed to set input baud rate");
  }
  else if (serial_speed == 1000000) {
    if (cfsetospeed(&t, B1000000))
      log_error("failed to set output baud rate");
    if (cfsetispeed(&t, B1000000))
      log_error("failed to set input baud rate");
  }
  else if (serial_speed == 1500000) {
    if (cfsetospeed(&t, B1500000))
      log_error("failed to set output baud rate");
    if (cfsetispeed(&t, B1500000))
      log_error("failed to set input baud rate");
  }
  else {
    if (cfsetospeed(&t, B4000000))
      log_error("failed to set output baud rate");
    if (cfsetispeed(&t, B4000000))
      log_error("failed to set input baud rate");
  }

  t.c_cflag &= ~PARENB;
  t.c_cflag &= ~CSTOPB;
  t.c_cflag &= ~CSIZE;
  t.c_cflag &= ~CRTSCTS;
  t.c_cflag |= CS8 | CLOCAL;
  t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO | ECHOE);
  t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR | INPCK | ISTRIP | IXON | IXOFF | IXANY | PARMRK);
  t.c_oflag &= ~OPOST;
  if (tcsetattr(fd, TCSANOW, &t))
    log_error("failed to set terminal parameters");

  // Also set USB serial port to low latency
  struct serial_struct serial;
  ioctl(fd, TIOCGSERIAL, &serial);
  serial.flags |= ASYNC_LOW_LATENCY;
  ioctl(fd, TIOCSSERIAL, &serial);
#endif
}

void close_serial_port(void)
{
  close(fd);
}

#endif

unsigned char wait_for_serial(const unsigned char what, const unsigned long timeout_sec, const unsigned long timeout_usec)
{
#ifdef WINDOWS
  return 0xff;
#else
  fd_set read_set;
  fd_set write_set;
  struct timeval timeout;
  unsigned char res = 0;

  FD_ZERO(&read_set);
  if (what & WAIT_READ)
    FD_SET(fd, &read_set);
  FD_ZERO(&write_set);
  if (what & WAIT_WRITE)
    FD_SET(fd, &write_set);
  if (timeout_sec > 0 || timeout_usec > 0) {
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = timeout_usec;
  }

  if (select(fd + 1, &read_set, &write_set, NULL, timeout_sec > 0 || timeout_usec > 0 ? &timeout : NULL) < 1) {
    log_debug("wait for serial: nothing there");
    return 0;
  }
  if (FD_ISSET(fd, &read_set))
    res |= WAIT_READ;
  if (FD_ISSET(fd, &write_set))
    res |= WAIT_WRITE;

  log_debug("wait_for_serial(%x): got something %x", what, res);
  return res;
#endif
}

/*
        borrowed from: https://www.binarytides.com/hostname-to-ip-address-c-sockets-linux/
        Get ip from domain name
 */

int hostname_to_ip(char *hostname, char *ip)
{
  struct hostent *he;
  struct in_addr **addr_list;
  int i;

  if ((he = gethostbyname(hostname)) == NULL) {
    // get the host info
#ifndef WINDOWS
    herror("gethostbyname");
#endif
    return 1;
  }

  addr_list = (struct in_addr **)he->h_addr_list;

  for (i = 0; addr_list[i] != NULL; i++) {
    // Return the first one;
    strcpy(ip, inet_ntoa(*addr_list[i]));
    return 0;
  }

  return 1;
}

#ifdef WINDOWS
PORT_TYPE open_tcp_port(char *portname)
{
  PORT_TYPE localfd = { WINPORT_TYPE_INVALID, 0 };
  char hostname[128] = "localhost";
  char port[128] = "4510"; // assume a default port of 4510
  if (portname[3] == '#')  // did user provide a hostname and port number?
  {
    sscanf(&portname[4], "%[^:]:%s", hostname, port);
  }
  else if (portname[3] == '\\' && portname[4] == '#') {
    sscanf(&portname[5], "%[^:]:%s", hostname, port);
  }

  localfd.type = WINPORT_TYPE_SOCK;

  WSADATA wsaData;
  struct addrinfo *result = NULL, *ptr = NULL, hints;
  int iResult;
  iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    printf("WSAStartup failed with error: %d\n", iResult);
    exit(1);
  }
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  iResult = getaddrinfo(hostname, port, &hints, &result);
  if (iResult != 0) {
    printf("getaddrinfo failed with error %d\n", iResult);
    WSACleanup();
    exit(1);
  }

  // attempt to connect to an address until one succeeds
  for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
    localfd.fdsock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if (localfd.fdsock == INVALID_SOCKET) {
      printf("socket failed with error: %d\n", WSAGetLastError());
      WSACleanup();
      exit(1);
    }
    iResult = connect(localfd.fdsock, ptr->ai_addr, (int)ptr->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
      closesocket(localfd.fdsock);
      localfd.fdsock = INVALID_SOCKET;
      continue;
    }
    break;
  }

  freeaddrinfo(result);

  if (localfd.fdsock == INVALID_SOCKET) {
    printf("Unable to connect to server!\n");
    WSACleanup();
    exit(1);
  }

  return localfd;
}

void close_tcp_port(PORT_TYPE localfd)
{
  if (localfd.fdsock != INVALID_SOCKET) {
    closesocket(localfd.fdsock);
    WSACleanup();
  }
}

#else // linux/mac-osx
PORT_TYPE open_tcp_port(char *portname)
{
  int localfd;
  char hostname[128] = "localhost";
  int port = 4510;        // assume a default port of 4510
  if (portname[3] == '#') // did user provide a hostname and port number?
  {
    sscanf(&portname[4], "%[^:]:%d", hostname, &port);
  }
  else if (portname[3] == '\\' && portname[4] == '#') {
    sscanf(&portname[5], "%[^:]:%d", hostname, &port);
  }

  struct sockaddr_in sock_st;

  localfd = socket(AF_INET, SOCK_STREAM, 0);
  if (localfd < 0) {
    log_error("could not create creating tcp/ip socket: %s", strerror(errno));
    return 0;
  }

  char ip[100];

  hostname_to_ip(hostname, ip);
  log_info("%s resolved to %s", hostname, ip);

  sock_st.sin_addr.s_addr = inet_addr(ip);
  sock_st.sin_family = AF_INET;
  sock_st.sin_port = htons(port);

  if (connect(localfd, (struct sockaddr *)&sock_st, sizeof(sock_st)) < 0) {
    log_error("failed to connect to socket %s:%d: %s\n", hostname, port, strerror(errno));
    close(localfd);
    exit(1);
  }

  return localfd;
}

void close_tcp_port(PORT_TYPE localfd)
{
  close(localfd);
}

// provide an implementation of stricmp for linux/mac
int stricmp(const char *a, const char *b)
{
  int ca, cb;
  do {
    ca = (unsigned char)*a++;
    cb = (unsigned char)*b++;
    ca = tolower(toupper(ca));
    cb = tolower(toupper(cb));
  } while (ca == cb && ca != '\0');
  return ca - cb;
}

#endif

void close_default_tcp_port(void)
{
  close_tcp_port(fd);
}

void close_communication_port(void)
{
  if (serial_port_is_tcp)
    close_tcp_port(fd);
  else
    close_serial_port();
}

int open_the_serial_port(char *serial_port)
{
  if (serial_port == NULL) {
    log_error("serial port not set, aborting");
    return -1;
  }

  serial_port_is_tcp = 0;
  if (!strncasecmp(serial_port, "tcp", 3)) {
    fd = open_tcp_port(serial_port);
    serial_port_is_tcp = 1;
    return 0;
  }

#ifdef WINDOWS
  fd.type = WINPORT_TYPE_FILE;
  fd.fdfile = open_serial_port(serial_port, serial_speed);
  if (fd.fdfile == INVALID_HANDLE_VALUE) {
    log_crit("could not open serial port '%s'", serial_port);
    log_error("  (could the port be in use by another application?)");
    log_error("  (could the usb cable be disconnected or faulty?)");
    return -1;
  }

#else /* !WINDOWS */
  errno = 0;
  fd = open(serial_port, O_RDWR);
  if (fd == -1) {
    log_crit("could not open serial port '%s'", serial_port);
    return -1;
  }

  set_serial_speed(fd, serial_speed);

#ifdef __linux__
  // Also try to reduce serial port latency on linux
  char *last_part = serial_port;
  for (int i = 0; serial_port[i]; i++)
    if (serial_port[i] == '/')
      last_part = &serial_port[i + 1];

  char latency_file[1024];
  snprintf(latency_file, 1024, "/sys/bus/usb-serial/devices/%s/latency_timer", last_part);
  FILE *f = fopen(latency_file, "r");
  if (f) {
    char line[1024];
    fread(line, 1024, 1, f);
    int latency = atoi(line);
    fclose(f);
    if (latency != 1) {
      f = fopen(latency_file, "w");
      if (!f) {
        log_warn("cannot write to '%s' to reduce USB port latency. Performance will be reduced.", latency_file);
        log_warn("  You can try something like the following to fix it:");
        log_warn("    echo 1 | sudo tee %s\n", latency_file);
      }
      else {
        fprintf(f, "1\n");
        fclose(f);
        log_info("reduced USB latency from %d ms to 1 ms.", latency);
      }
    }
  }
#endif /* __linux__ */
#endif /* !WINDOWS */

  return 0;
}

int switch_to_c64mode(void)
{
  log_note("trying to switch to C64 mode");
  saw_c65_mode = 0;
  //    do_usleep(100000);
  int retries = 0;
  while (!saw_c64_mode && retries < 10) {
    monitor_sync();
    log_debug("go64 retry %d", retries);
    stuff_keybuffer("\r\rGO64\rY\r");
    do_usleep(50000);
    detect_mode();
    retries++;
  }
  return saw_c64_mode == 0;
}

unsigned int get_bitstream_fpgaid(const char *bitstream)
{
  unsigned int fpga_id = 0xffffffff, found = 0; // wildcard match for the last valid device
  if (bitstream) {
    log_info("scanning bitstream file '%s' for FPGA ID", bitstream);
    FILE *f = fopen(bitstream, "rb");
    if (f) {
      unsigned char buff[8192];
      int len = fread(buff, 1, 8192, f);
      if (len < 8192)
        log_debug("only got %d bytes from bitstream, file to short?", len);
      for (int i = 0; i < len; i++) {
        if ((buff[i + 0] == 0x30) && (buff[i + 1] == 0x01) && (buff[i + 2] == 0x80) && (buff[i + 3] == 0x01)) {
          i += 4;
          fpga_id = buff[i + 0] << 24;
          fpga_id |= buff[i + 1] << 16;
          fpga_id |= buff[i + 2] << 8;
          fpga_id |= buff[i + 3] << 0;

          log_info("detected FPGA ID %08x from bitstream file", fpga_id);
          found = 1;
          break;
        }
      }
      fclose(f);
    }
  }
  if (!found)
    log_info("using default FPGA ID %x", fpga_id);

  return fpga_id;
}

char system_bitstream_version[64] = "VERSION NOT FOUND";
char system_hardware_model_name[64] = "UNKNOWN";
unsigned char system_hardware_model = 0;
int get_system_bitstream_version(void)
{
  char buf[512], *found, *end;
  size_t len = 0;
  time_t timeout;

  // fetch version info via monitor 'h'
  // don' forget to sync console with '\xf#\r'
  log_debug("get_system_bitstream_version: writing reset/help");
#ifdef WINDOWS
  slow_write(fd, "\xf#\rh\r", 5);
#else
  write(fd, "\xf#\rh\r", 5);
#endif
  usleep(20000);
  timeout = time(NULL);
  while (timeout + 2 > time(NULL)) {
    len = serialport_read(fd, (unsigned char *)buf, 512);
    if (len == -1) {
      log_error("get_system_bitstream_version: read error %d", errno);
      return -1;
    }
    if (len > 0)
      break;
  }
  if (len == 0)
    return -1;

  log_debug("get_system_bitstream_version: got help answer (len=%d)", len);
  found = strstr(buf, "build GIT: ");
  if (found != NULL) {
    found += 11; // skip found prefix
    end = strchr(found, '\r');
    if (end != NULL) {
      *end = 0;
      strncpy(system_bitstream_version, found, 63);
      system_bitstream_version[63] = 0;
      log_debug("get_system_bitstream_version: version %s", system_bitstream_version);
    }
  }
  else {
    log_debug("get_system_bitstream_version: version not found");
    return -1;
  }

  log_debug("get_system_bitstream_version: dumping $FFD3629");
  slow_write(fd, "mffd3629 1\r", 11);
  usleep(20000);
  while (!(len = serialport_read(fd, (unsigned char *)buf, 512)))
    ;
  // search version byte and translate
  found = strstr(buf, ":0FFD3629:");
  if (found != NULL) {
    found += 10;
    system_hardware_model = ((unsigned char)found[0] - (found[0] > 57 ? 55 : 48)) * 16 + (unsigned char)found[1]
                          - (found[1] > 57 ? 55 : 48);
    switch (system_hardware_model) {
    case 1:
      strcpy(system_hardware_model_name, "MEGA65 R1");
      break;
    case 2:
      strcpy(system_hardware_model_name, "MEGA65 R2");
      break;
    case 3:
      strcpy(system_hardware_model_name, "MEGA65 R3");
      break;
    case 33:
      strcpy(system_hardware_model_name, "MEGAPHONE R1 PROTOTYPE");
      break;
    case 34:
      strcpy(system_hardware_model_name, "MEGAPHONE R4 PROTOTYPE");
      break;
    case 64:
      strcpy(system_hardware_model_name, "NEXYS 4 PSRAM");
      break;
    case 65:
      strcpy(system_hardware_model_name, "NEXYS 4 DDR (NO WIDGET)");
      break;
    case 66:
      strcpy(system_hardware_model_name, "NEXYS 4 DDR (WIDGET)");
      break;
    case 253:
      strcpy(system_hardware_model_name, "QMTECH WUKONG BOARD");
      break;
    case 254:
      strcpy(system_hardware_model_name, "SIMULATED MEGA65");
      break;
    case 255:
      strcpy(system_hardware_model_name, "HARDWARE NOT SPECIFIED");
      break;
    default:
      snprintf(system_hardware_model_name, 31, "UNKNOWN MODEL $%02X", system_hardware_model);
    }
    log_debug("get_system_bitstream_version: hardware $%02X '%s'", system_hardware_model, system_hardware_model_name);
  }
  else {
    log_debug("get_system_bitstream_version: hardware not detected");
    return -1;
  }

  return 0;
}

char system_rom_version[18] = "UNKNOWN";
char *get_system_rom_version(void)
{
  // Check for C65 ROM via version string
  fetch_ram(0x20016L, 7, (unsigned char *)system_rom_version + 4);
  if ((system_rom_version[4] == 'V') && (system_rom_version[5] == '9')) {
    if (system_rom_version[6] >= '2')
      system_rom_version[0] = 'M';
    else
      system_rom_version[0] = 'C';
    system_rom_version[1] = '6';
    system_rom_version[2] = '5';
    system_rom_version[3] = ' ';
    system_rom_version[11] = 0;
    return system_rom_version;
  }

  // OpenROM - 16 characters "OYYMMDDCC       "
  fetch_ram(0x20010L, 16, (unsigned char *)system_rom_version + 4);
  if ((system_rom_version[4] == 'O') && (system_rom_version[11] == '2') && (system_rom_version[12] == '0')
      && (system_rom_version[13] == ' ')) {
    system_rom_version[0] = 'O';
    system_rom_version[1] = 'P';
    system_rom_version[2] = 'E';
    system_rom_version[3] = 'N';
    system_rom_version[4] = ' ';
    system_rom_version[11] = 0;
    return system_rom_version;
  }

  strcpy(system_rom_version, "NON MEGA65 ROM");
  return system_rom_version;
}

char *find_serial_port(const int serial_speed)
{
#ifndef WINDOWS
  int res, fd;
  char *device;

  log_note("no serial device given, trying brute-force autodetect");

  usbdev_get_next_device(1);
  while ((device = usbdev_get_next_device(0))) {
    // this must use m65serial to open and read!
    log_debug("find_serial_port: trying %s", device);
    if (open_the_serial_port(device))
      continue;

    res = get_system_bitstream_version();

    close(fd);
    fd = -1;

    if (!res) {
      log_note("selecting %s: %s (%s)", device, system_hardware_model_name, system_bitstream_version);
      break;
    }
    else
      log_note("%s: no reply", device);
  }

  return device ? strdup(device) : NULL;
#else
  return NULL;
#endif
}

// very lazy conversion from Windows Codepage 1252 to PETSCII screen codes (used by SID mini player)
unsigned char wincp1252_to_screen(unsigned char ascii)
{
  // control and latin chars - use raster char
  if (ascii < 0x20 || ascii > 0x7b)
    return 0x5e;
  // numbers and interpunctation
  if (ascii < 0x40)
    return ascii;
  // big letters
  if (ascii < 0x5b)
    return ascii;
  if (ascii < 0x5f)
    return 0x94;
  if (ascii == 0x5f)
    return 0x64; // underscore
  // lower letters
  if (ascii < 0x7b)
    return ascii & 0x1f;
  // default is raster
  return 0x5e;
}
