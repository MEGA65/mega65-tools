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

#include "m65common.h"

#define SLOW_FACTOR 1
#define SLOW_FACTOR2 1

int debug_serial = 0;

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

int serial_speed = 2000000;

int cpu_stopped = 0;

// 0 = old hard coded monitor, 1= Kenneth's 65C02 based fancy monitor
// Only REALLY old bitstreams don't have the new monitor
int new_monitor = 1;

#ifdef WINDOWS
PORT_TYPE fd = { WINPORT_TYPE_INVALID, INVALID_HANDLE_VALUE, INVALID_SOCKET };
#else
PORT_TYPE fd = -1;
#endif

void timestamp_msg(char* msg)
{
  if (!start_time)
    start_time = time(0);
  fprintf(stderr, "[T+%lldsec] %s", (long long)time(0) - start_time, msg);

  return;
}

int do_slow_write(PORT_TYPE fd, char* d, int l, const char* func, const char* file, const int line)
{
  // UART is at 2Mbps, but we need to allow enough time for a whole line of
  // writing. 100 chars x 0.5usec = 500usec. So 1ms between chars should be ok.
  int i;
#if 0
  printf("\nWriting ");
  for(i=0;i<l;i++)
    {
      if (d[i]>=' ') printf("%c",d[i]); else printf("[$%02X]",d[i]);
    }
  printf("\n");
  char line[1024];
  fgets(line,1024,stdin);
#endif

  for (i = 0; i < l; i++) {
    if (no_rxbuff) {
      if (serial_speed == 4000000)
        do_usleep(1000 * SLOW_FACTOR);
      else
        do_usleep(2000 * SLOW_FACTOR);
    }
    int w = do_serial_port_write(fd, (unsigned char*)&d[i], 1, func, file, line);
    while (w < 1) {
      if (no_rxbuff) {
        if (serial_speed == 4000000)
          do_usleep(500 * SLOW_FACTOR);
        else
          do_usleep(1000 * SLOW_FACTOR);
      }
      w = do_serial_port_write(fd, (unsigned char*)&d[i], 1, func, file, line);
    }
  }
  return 0;
}

int do_slow_write_safe(PORT_TYPE fd, char* d, int l, const char* func, const char* file, int line)
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

void check_for_vf011_jobs(unsigned char* read_buff, int b)
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
  while (1) {
    b = serialport_read(fd, read_buff, 8191);
    //    if (b>0) dump_bytes(0,"wait_for_prompt",read_buff,b);
    if (b < 0 || b > 8191)
      continue;
    read_buff[b] = 0;

    check_for_vf011_jobs(read_buff, b);

    if (strstr((char*)read_buff, ".")) {
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
          if (strstr((char*)recent, "t1\r")) {
            cpu_stopped = 1;
            return;
          }
        }
        else {
          //	  dump_bytes(0,"recent looking for t0\r",recent,3);
          if (strstr((char*)recent, "t0\r")) {
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

int stop_cpu(void)
{
  if (cpu_stopped) {
    //    printf("CPU already stopped.\n");
    return 1;
  }
  // Stop CPU
  timestamp_msg("");
  fprintf(stderr, "Stopping CPU\n");
  if (no_rxbuff)
    do_usleep(50000);
  cpu_stopped = 1;
  slow_write_safe(fd, "t1\r", 3);
  purge_and_check_for_vf011_jobs(1);
  return 0;
}

int start_cpu(void)
{
  // Stop CPU
  if (cpu_stopped) {
    timestamp_msg("");
    fprintf(stderr, "Starting CPU\n");
  }
  if (no_rxbuff)
    do_usleep(50000);
  slow_write(fd, "t0\r", 3);
  purge_and_check_for_vf011_jobs(0);
  return 0;
}

int load_file(char* filename, int load_addr, int patchHyppo)
{
  char cmd[1024];

  FILE* f = fopen(filename, "rb");
  if (!f) {
    fprintf(stderr, "Could not open file '%s'\n", filename);
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
      printf("patching...\n");
      // Look for BIT $nnnn / BIT $1234, and change to JMP $nnnn to skip
      // all SD card activities
      for (int i = 0; i < (b - 5); i++) {
        if ((buf[i] == 0x2c) && (buf[i + 3] == 0x2c) && (buf[i + 4] == 0x34) && (buf[i + 5] == 0x12)) {
          fprintf(stderr, "Patching Hyppo @ $%04x to skip SD card and ROM checks.\n", 0x8000 + i);
          buf[i] = 0x4c;
        }
      }
    }
    printf("Read to $%04x (%d bytes)\n", load_addr, b);
    fflush(stdout);
    // load_addr=0x400;
    // XXX - The l command requires the address-1, and doesn't cross 64KB boundaries.
    // Thus writing to $xxx0000 requires adding 64K to fix the actual load address
    int munged_load_addr = load_addr;
    if ((load_addr & 0xffff) == 0x0000) {
      munged_load_addr += 0x10000;
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
    unsigned char* p = buf;
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
  char msg[1024];
  snprintf(msg, 1024, "File '%s' loaded.\n", filename);
  timestamp_msg(msg);
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

void print_spaces(FILE* f, int col)
{
  for (int i = 0; i < col; i++)
    fprintf(f, " ");
}

int dump_bytes(int col, char* msg, unsigned char* bytes, int length)
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
        fprintf(stderr, "   ");
    fprintf(stderr, "  ");
    for (int j = 0; j < 16; j++)
      if (i + j < length)
        fprintf(stderr, "%c", (bytes[i + j] >= ' ' && bytes[i + j] < 0x7c) ? bytes[i + j] : '.');

    fprintf(stderr, "\n");
  }
  return 0;
}

int stuff_keybuffer(char* s)
{
  int buffer_addr = 0x277;
  int buffer_len_addr = 0xc6;

  if (saw_c65_mode) {
    buffer_addr = 0x2b0;
    buffer_len_addr = 0xd0;
  }

  timestamp_msg("Injecting string into key buffer at ");
  fprintf(stderr, "$%04X : ", buffer_addr);
  for (int i = 0; s[i]; i++) {
    if (s[i] >= ' ' && s[i] < 0x7c)
      fprintf(stderr, "%c", s[i]);
    else
      fprintf(stderr, "[$%02x]", s[i]);
  }
  fprintf(stderr, "\n");

  char cmd[1024];
  snprintf(cmd, 1024, "s%x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\rs%x %d\r", buffer_addr, s[0], s[1], s[2], s[3],
      s[4], s[5], s[6], s[7], s[8], s[9], buffer_len_addr, (int)strlen(s));
  return slow_write(fd, cmd, strlen(cmd));
}

int read_and_print(PORT_TYPE fd)
{
  char buff[8192];
  int r = serialport_read(fd, (unsigned char*)buff, 8192);
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
  unsigned char read_buff[8192];

  monitor_sync();
  // Send two commands one after the other with no delay.
  // If we have RX buffering, both commands will execute.
  // If not, then only the first one will execute
  printf("Checking if MEGA65 has RX buffer\n");
  serialport_write(fd, (unsigned char*)"\025m0\rm1\r", 7);
  do_usleep(20000); // Give plenty of time for things to settle
  int b = 1;
  while (b > 0) {
    b = serialport_read(fd, read_buff, 8192);
    if (b >= 0)
      read_buff[b] = 0;
    //    dump_bytes(0,"bytes from serial port",read_buff,b);
    if ((strstr((char*)read_buff, ":00000000:")) && (strstr((char*)read_buff, ":00000001:"))) {
      no_rxbuff = 0;
      timestamp_msg("RX buffer detected.  Latency will be reduced.\n");
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
          serialport_write(fd, (unsigned char*)zeroes, 256);

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
  printf("Failed to synchronise with the monitor.\n");
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
  char* s = strstr((char*)buff, "\n,");
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
  char* s = strstr((char*)buff, " H ");
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

  snprintf(pattern, 16, "\n,077");

  int match_state = 0;

  // Now read until we see the requested PC
  timestamp_msg("");
  fprintf(stderr, "Waiting for breakpoint at $%04X to trigger.\n", breakpoint_pc);
  while (1) {
    int b = serialport_read(fd, (unsigned char*)read_buff, 8192);

    for (int i = 0; i < b; i++) {
      if (read_buff[i] == pattern[match_state]) {
        if (match_state == 4) {
          timestamp_msg("");
          fprintf(stderr, "Breakpoint @ $%04X triggered.\n", breakpoint_pc);
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

int push_ram(unsigned long address, unsigned int count, unsigned char* buffer)
{
  //  fprintf(stderr,"Pushing %d bytes to RAM @ $%07lx\n",count,address);

  int cpu_stopped_state = cpu_stopped;

  // We have to stop the CPU first, so that the serial monitor can keep up with
  // the full 2mbit/sec data rate (as otherwise the CPU can block the serial
  // monitor processor for some number of cycles per character while it finishes
  // instructions.
  if (!cpu_stopped_state)
    stop_cpu();

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
      int n = b;
      unsigned char* p = &buffer[offset];
      while (n > 0) {
        int w = serialport_write(fd, p, n);
        if (w > 0) {
          p += w;
          n -= w;
        }
        else
          do_usleep(1000 * SLOW_FACTOR);
      }
    }
    wait_for_prompt();
    offset += b;
  }
  if (!cpu_stopped_state)
    start_cpu();
  return 0;
}

int fetch_ram(unsigned long address, unsigned int count, unsigned char* buffer)
{
  /* Fetch a block of RAM into the provided buffer.
     This greatly simplifies many tasks.
  */

  unsigned long addr = address;
  unsigned long end_addr;
  char cmd[8192];
  unsigned char read_buff[8192];
  char next_addr_str[8192];
  int ofs = 0;

  //  fprintf(stderr,"Fetching $%x bytes @ $%lx\n",count,address);

  //  monitor_sync();
  while (addr < (address + count)) {
    if ((address + count - addr) < 17) {
      snprintf(cmd, 8192, "m%X\r", (unsigned int)addr);
      end_addr = addr + 0x10;
    }
    else {
      snprintf(cmd, 8192, "M%X\r", (unsigned int)addr);
      end_addr = addr + 0x100;
    }
    //    printf("Sending '%s'\n",cmd);
    slow_write_safe(fd, cmd, strlen(cmd));
    while (addr != end_addr) {
      snprintf(next_addr_str, 8192, "\n:%08X:", (unsigned int)addr);
      int b = serialport_read(fd, &read_buff[ofs], 8192 - ofs);
      if (b < 0)
        b = 0;
      if ((ofs + b) > 8191)
        b = 8191 - ofs;
      //      if (b) dump_bytes(0,"read data",&read_buff[ofs],b);
      read_buff[ofs + b] = 0;
      ofs += b;
      char* s = strstr((char*)read_buff, next_addr_str);
      if (s && (strlen(s) >= 42)) {
        char b = s[42];
        s[42] = 0;
        if (0) {
          printf("Found data for $%08x:\n%s\n", (unsigned int)addr, s);
        }
        s[42] = b;
        for (int i = 0; i < 16; i++) {

          // Don't write more bytes than requested
          if ((addr - address + i) >= count)
            break;

          char hex[3];
          hex[0] = s[1 + 10 + i * 2 + 0];
          hex[1] = s[1 + 10 + i * 2 + 1];
          hex[2] = 0;
          buffer[addr - address + i] = strtol(hex, NULL, 16);
        }
        addr += 16;

        // Shuffle buffer down
        int s_offset = (long long)s - (long long)read_buff + 42;
        bcopy(&read_buff[s_offset], &read_buff[0], 8192 - (ofs - s_offset));
        ofs -= s_offset;
      }
    }
  }
  if (addr >= (address + count)) {
    //    fprintf(stderr,"Memory read complete at $%lx\n",addr);
    return 0;
  }
  else {
    fprintf(stderr, "ERROR: Could not read requested memory region.\n");
    exit(-1);
    return 1;
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

int fetch_ram_cacheable(unsigned long address, unsigned int count, unsigned char* buffer)
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
      printf(".");
      fflush(stdout);
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

  unsigned char mem_buff[8192];

  // Look for OpenROMs
  fetch_ram(0x20010, 16, mem_buff);
  if (mem_buff[0] == 'V' || mem_buff[0] == 'O') {
    mem_buff[9] = 0;
    int date_code = atoi((const char*)&mem_buff[1]);
    if (date_code > 2000000) {
      fprintf(stderr, "Detected OpenROM version %d\n", date_code);
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
        timestamp_msg("Waiting for MEGA65 KERNAL/OS to settle...\n");
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
      timestamp_msg("");
      fprintf(stderr, "In C65 Mode.\n");
      return 0;
    }

    // Use screen address to guess mode
    fetch_ram(0xffd3060, 3, mem_buff);
    if (mem_buff[1] == 0x04) {
      printf("Screen is at $0400\n");
      // check $01 port value
      fetch_ram(0x7770001, 1, mem_buff);
      printf("Port $01 contains $%02x\n", mem_buff[0]);
      if ((mem_buff[0] & 0xf) == 0x07) {
        saw_c64_mode = 1;
        timestamp_msg("");
        fprintf(stderr, "In C64 Mode.\n");
        return 0;
      }
    }
    if (mem_buff[1] == 0x08) {
      saw_c65_mode = 1;
      timestamp_msg("");
      fprintf(stderr, "In C65 Mode.\n");
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
	  //	  fprintf(stderr,"Odd PC=$%04x\n",pc);
	  do_usleep(5000);
	}
      }
    }
    if (in_range>3) {
      // We are in C65 BASIC main loop, so assume it is C65 mode
      saw_c65_mode=1;
      timestamp_msg("");
      fprintf(stderr,"CPU in C65 BASIC 10 main loop.\n");
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
	printf("Odd PC=$%04x\n",pc);
	do_usleep(5000);
      }
    }
    if (in_range>3) {
      // We are in C64 BASIC main loop, so assume it is C65 mode
      saw_c64_mode=1;
      timestamp_msg("");
      fprintf(stderr,"CPU in C64 BASIC 2 main loop.\n");
      return 0;
    }
  }
#endif

  timestamp_msg("Could not determine C64/C65/MEGA65 mode.\n");
  return 1;
}

int last_read_count = 0;

#ifdef WINDOWS

void print_error(const char* context)
{
  DWORD error_code = GetLastError();
  char buffer[256];
  DWORD size = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK, NULL, error_code,
      MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), buffer, sizeof(buffer), NULL);
  if (size == 0) {
    buffer[0] = 0;
  }
  fprintf(stderr, "%s: %s\n", context, buffer);
}

// Opens the specified serial port, configures its timeouts, and sets its
// baud rate.  Returns a handle on success, or INVALID_HANDLE_VALUE on failure.
HANDLE open_serial_port(const char* device, uint32_t baud_rate)
{
  HANDLE port = CreateFileA(device, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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

int win_serial_port_write(HANDLE port, uint8_t* buffer, size_t size, const char* func, const char* file, const int line)
{
  DWORD offset = 0;
  DWORD written;
  BOOL success;
  //  printf("Calling WriteFile(%d)\n",size);

  if (debug_serial) {
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

int win_tcp_write(SOCKET sock, uint8_t* buffer, size_t size, const char* func, const char* file, const int line)
{
  if (debug_serial) {
    fprintf(stderr, "%s:%d:%s(): ", file, line, func);
    dump_bytes(0, "tcp write (windows)", buffer, size);
  }

  int iResult = send(sock, (char*)buffer, size, 0);
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
int do_serial_port_write(WINPORT port, uint8_t* buffer, size_t size, const char* func, const char* file, const int line)
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
SSIZE_T win_serial_port_read(HANDLE port, uint8_t* buffer, size_t size, const char* func, const char* file, const int line)
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

SSIZE_T win_tcp_read(SOCKET sock, uint8_t* buffer, size_t size, const char* func, const char* file, const int line)
{
  // check if any bytes available yet, if not, exit early
  unsigned long l;
  ioctlsocket(sock, FIONREAD, &l);
  if (l == 0)
    return 0;

  int iResult = recv(sock, (char*)buffer, size, 0);
  if (iResult == 0) {
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

SSIZE_T do_serial_port_read(WINPORT port, uint8_t* buffer, size_t size, const char* func, const char* file, const int line)
{
  if (port.type == WINPORT_TYPE_FILE)
    return win_serial_port_read(port.fdfile, buffer, size, func, file, line);
  else if (port.type == WINPORT_TYPE_SOCK)
    return win_tcp_read(port.fdsock, buffer, size, func, file, line);
  return 0;
}

#else
int do_serial_port_write(int fd, uint8_t* buffer, size_t size, const char* function, const char* file, const int line)
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

size_t do_serial_port_read(int fd, uint8_t* buffer, size_t size, const char* function, const char* file, const int line)
{
  int count = read(fd, buffer, size);
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
    fprintf(stderr, "WARNING: serial port file descriptor is -1\n");
  }

#ifdef __APPLE__
  speed_t speed = serial_speed;
  fprintf(stderr, "Setting serial speed to %d bps using OSX method.\n", (int)speed);
  if (ioctl(fd, IOSSIOSPEED, &speed) == -1) {
    perror("Failed to set output baud rate using IOSSIOSPEED");
  }
  if (tcgetattr(fd, &t))
    perror("Failed to get terminal parameters");
  cfmakeraw(&t);
  if (tcsetattr(fd, TCSANOW, &t))
    perror("Failed to set OSX terminal parameters");
#else
  if (serial_speed == 230400) {
    if (cfsetospeed(&t, B230400))
      perror("Failed to set output baud rate");
    if (cfsetispeed(&t, B230400))
      perror("Failed to set input baud rate");
  }
  else if (serial_speed == 2000000) {
    if (cfsetospeed(&t, B2000000))
      perror("Failed to set output baud rate");
    if (cfsetispeed(&t, B2000000))
      perror("Failed to set input baud rate");
  }
  else if (serial_speed == 1000000) {
    if (cfsetospeed(&t, B1000000))
      perror("Failed to set output baud rate");
    if (cfsetispeed(&t, B1000000))
      perror("Failed to set input baud rate");
  }
  else if (serial_speed == 1500000) {
    if (cfsetospeed(&t, B1500000))
      perror("Failed to set output baud rate");
    if (cfsetispeed(&t, B1500000))
      perror("Failed to set input baud rate");
  }
  else {
    if (cfsetospeed(&t, B4000000))
      perror("Failed to set output baud rate");
    if (cfsetispeed(&t, B4000000))
      perror("Failed to set input baud rate");
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
    perror("Failed to set terminal parameters");

  // Also set USB serial port to low latency
  struct serial_struct serial;
  ioctl(fd, TIOCGSERIAL, &serial);
  serial.flags |= ASYNC_LOW_LATENCY;
  ioctl(fd, TIOCSSERIAL, &serial);
#endif
}
#endif

/*
        borrowed from: https://www.binarytides.com/hostname-to-ip-address-c-sockets-linux/
        Get ip from domain name
 */

int hostname_to_ip(char* hostname, char* ip)
{
  struct hostent* he;
  struct in_addr** addr_list;
  int i;

  if ((he = gethostbyname(hostname)) == NULL) {
    // get the host info
#ifndef WINDOWS
    herror("gethostbyname");
#endif
    return 1;
  }

  addr_list = (struct in_addr**)he->h_addr_list;

  for (i = 0; addr_list[i] != NULL; i++) {
    // Return the first one;
    strcpy(ip, inet_ntoa(*addr_list[i]));
    return 0;
  }

  return 1;
}

#ifdef WINDOWS
int open_tcp_port(char* portname)
{
  char hostname[128] = "localhost";
  char port[128] = "4510"; // assume a default port of 4510
  if (portname[3] == '#')  // did user provide a hostname and port number?
  {
    sscanf(&portname[4], "%s:%s", hostname, port);
  }

  fd.type = WINPORT_TYPE_SOCK;

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
    fd.fdsock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if (fd.fdsock == INVALID_SOCKET) {
      printf("socket failed with error: %d\n", WSAGetLastError());
      WSACleanup();
      return 1;
    }
    iResult = connect(fd.fdsock, ptr->ai_addr, (int)ptr->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
      closesocket(fd.fdsock);
      fd.fdsock = INVALID_SOCKET;
      continue;
    }
    break;
  }

  freeaddrinfo(result);

  if (fd.fdsock == INVALID_SOCKET) {
    printf("Unable to connect to server!\n");
    WSACleanup();
    exit(1);
  }

  return 1;
}

void close_tcp_port(void)
{
  if (fd.fdsock != INVALID_SOCKET) {
    closesocket(fd.fdsock);
    WSACleanup();
  }
}

#else // linux/mac-osx
int open_tcp_port(char* portname)
{
  char hostname[128] = "localhost";
  int port = 4510;        // assume a default port of 4510
  if (portname[3] == '#') // did user provide a hostname and port number?
  {
    sscanf(&portname[4], "%s:%d", hostname, &port);
  }

  struct sockaddr_in sock_st;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    printf("error %d creating tcp/ip socket: %s\n", errno, strerror(errno));
    return 0;
  }

  char ip[100];

  hostname_to_ip(hostname, ip);
  printf("%s resolved to %s", hostname, ip);

  sock_st.sin_addr.s_addr = inet_addr(ip);
  sock_st.sin_family = AF_INET;
  sock_st.sin_port = htons(port);

  if (connect(fd, (struct sockaddr*)&sock_st, sizeof(sock_st)) < 0) {
    printf("error %d connecting to tcp/ip socket %s:%d: %s\n", errno, hostname, port, strerror(errno));
    close(fd);
    return 0;
  }

  return 1;
}

void close_tcp_port(void)
{
  // TODO: do I need to do any nice closing of the socket in linux too?
}

#endif

void open_the_serial_port(char* serial_port)
{
  if (!strncasecmp(serial_port, "tcp", 3)) {
    open_tcp_port(serial_port);
    return;
  }
#ifdef WINDOWS
  fd.type = WINPORT_TYPE_FILE;
  fd.fdfile = open_serial_port(serial_port, 2000000);
  if (fd.fdfile == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "Could not open serial port '%s'\n", serial_port);
    exit(-1);
  }

#else
  errno = 0;
  fd = open(serial_port, O_RDWR);
  if (fd == -1) {
    fprintf(stderr, "Could not open serial port '%s'\n", serial_port);
    perror("open");
    exit(-1);
  }

  set_serial_speed(fd, serial_speed);

  // Also try to reduce serial port latency
  char* last_part = serial_port;
  for (int i = 0; serial_port[i]; i++)
    if (serial_port[i] == '/')
      last_part = &serial_port[i + 1];
  char latency_file[1024];
  snprintf(latency_file, 1024, "/sys/bus/usb-serial/devices/%s/latency_timer", last_part);
  FILE* f = fopen(latency_file, "r");
  if (f) {
    char line[1024];
    fread(line, 1024, 1, f);
    int latency = atoi(line);
    fclose(f);
    if (latency != 1) {
      f = fopen(latency_file, "w");
      if (!f) {
        fprintf(stderr, "WARNING: Cannot write to '%s' to reduce USB port latency.  Performance will be reduced.\n",
            latency_file);
        fprintf(stderr,
            "         You can try something like the following to fix it:\n"
            "         echo 1 | sudo tee %s\n",
            latency_file);
      }
      else {
        fprintf(f, "1\n");
        fclose(f);
        fprintf(stderr, "Reduced USB latency from %d ms to 1 ms.\n", latency);
      }
    }
  }
#endif
}

int switch_to_c64mode(void)
{
  printf("Trying to switch to C64 mode...\n");
  monitor_sync();
  stuff_keybuffer("GO64\rY\r");
  saw_c65_mode = 0;
  //    do_usleep(100000);
  detect_mode();
  int count = 0;
  while (!saw_c64_mode) {
    fprintf(stderr, "WARNING: Failed to switch to C64 mode.\n");
    monitor_sync();
    count++;
    fprintf(stderr, "count=%d\n", count);
    if (count > 0) {
      fprintf(stderr, "Retyping GO64\n");
      stuff_keybuffer("\r\rGO64\rY\r");
      do_usleep(50000);
      count = 0;
    }
    detect_mode();
  }
  return 0;
}
