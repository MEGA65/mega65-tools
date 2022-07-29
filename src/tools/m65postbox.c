/*

  A simple server to listen to eleven attempting to post file content.
  It aims to save this file content to a local file on your pc.

  Gurce Isikyildiz 2022

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
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>

#include "m65common.h"
#include "dirtymock.h"

#define BOOL int
#define TRUE 1
#define FALSE 0

#define DE_ATTRIB_DIR  0x10
#define DE_ATTRIB_FILE 0x20

#define RET_FAIL -1
#define RET_NOT_FOUND 0
#define RET_FOUND 1

#ifdef WINDOWS
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <readline/readline.h>
#include <readline/history.h>
#endif

char serial_port[1024] = "/dev/ttyUSB1";
char device_name[1024] = "";

extern const char* version_string;

void usage(void)
{
  fprintf(stderr, "MEGA65 postbox server (file receiver)\n");
  fprintf(stderr, "version: %s\n\n", version_string);
  fprintf(stderr, "usage: m65postbox [-l <serial port>|-d <device name>] [-s <230400|2000000|4000000>]\n");
  fprintf(stderr, "  -l - Name of serial port to use, e.g., /dev/ttyUSB1\n");
  fprintf(stderr, "  -s - Speed of serial port in bits per second. This must match what your bitstream uses.\n");
  fprintf(stderr, "       (Almost always 2000000 is the correct answer).\n");
  fprintf(stderr, "\n");
  exit(-3);
}

enum FILE_STATE { FS_WAIT, FS_WRITE };

void parse_packet(char* pkt) {
  static enum FILE_STATE file_state = FS_WAIT;
  static char fname[256] = "";
  static FILE* f = NULL;

  printf("parsing packet: \n%s\n", pkt);

  switch(file_state) {
    case FS_WAIT:
      if (strncmp(pkt, "/FILE:", 6) == 0) {
        printf("got here...\n");
        strcpy(fname, pkt+6);
        // fname[strlen(fname)-1] = '\0';
        printf("Writing to file \"%s\"...\n", fname);
        f = fopen(fname, "wb");
        file_state = FS_WRITE;
      }
      break;

    case FS_WRITE:
      if (strcmp(pkt, "/") == 0) {
        fclose(f);
        printf("Closing file \"%s\"...\n", fname);
        file_state = FS_WAIT;
      }
      else {
        unsigned int len = strlen(pkt);
        fwrite(pkt, 1, len, f);
        printf("..writing %d bytes..\n", len);
      }
      break;
  }
}

enum PARSE_STATE { PS_HDR1, PS_HDR2, PS_HDR3, PS_HDR4, PS_LEN, PS_STR };

void parse_bytes(unsigned char* bytes, int numbytes)
{
  static enum PARSE_STATE state = PS_HDR1;
  static unsigned int len = 0;
  static unsigned int cnt = 0;
  static char str[8192] = "";

  for (int k = 0; k < numbytes; k++) {
    unsigned char ch = bytes[k];
    switch(state) {
      case PS_HDR1:
        state = (ch == 0x00 ? PS_HDR2 : PS_HDR1); break;
      case PS_HDR2:
        state = (ch == 0x00 ? PS_HDR3 : PS_HDR1); break;
      case PS_HDR3:
        state = (ch == 0x00 ? PS_HDR4 : PS_HDR1); break;
      case PS_HDR4:
        state = (ch == 0xfd ? PS_LEN : PS_HDR1); break;
      case PS_LEN:
        len = (unsigned int)ch;
        cnt = 0;
        state = PS_STR;
        break;
      case PS_STR:
        str[cnt] = ch;
        cnt++;
        if (cnt == len) {
          cnt--;  // remove trailing '\\' character
          str[cnt] = '\0';
          parse_packet(str);
          state = PS_HDR1;
        }
        break;
    } // end switch
    printf("state = %d\n", state);
  } // end for
}

int DIRTYMOCK(main)(int argc, char** argv)
{
#ifdef WINDOWS
  // working around mingw64-stdout line buffering issue with advice suggested here:
  // https://stackoverflow.com/questions/13035075/printf-not-printing-on-console
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
#endif

  int opt;
  while ((opt = getopt(argc, argv, "b:s:l:c:u:p:n")) != -1) {
    switch (opt) {
    case 'l':
      strcpy(serial_port, optarg);
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

  if (argc - optind == 1)
    usage();

  errno = 0;

  open_the_serial_port(serial_port);
  xemu_flag = mega65_peek(0xffd360f) & 0x20 ? 0 : 1;

  rxbuff_detect();

  unsigned char read_buff[8192];

  while(1) {
    int b = serialport_read(fd, read_buff, 8192);
    if (b > 0) {
      read_buff[b] = '\0';
       printf("Received: %s ", read_buff);
      for (int k = 0; k < b; k++)
        printf("[%02X] ", read_buff[k]);
      printf("\n");
      parse_bytes(read_buff, b);
    }
  }

  return 0;
}

