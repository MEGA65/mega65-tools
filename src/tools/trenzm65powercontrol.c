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

#include <m65common.h>

char *logfile = NULL;
char *bitstream = NULL;

char *serial_port = "/dev/ttyUSB0";

extern PORT_TYPE fd;

int main(int argc, char **argv)
{
  printf("Opening %s...\n", serial_port);
  serial_speed = 1000000;
  if (open_the_serial_port(serial_port))
    exit(-1);

  int len = 2;
  uint8_t str[2] = { 0x01, 0x02 };

  while (1) {
    do_serial_port_write(fd, (uint8_t *)str, len, NULL, NULL, 0);
  }
}
