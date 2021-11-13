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

char* logfile = NULL;
char *bitstream=NULL;

char *serial_port="/dev/ttyUSB0";

extern PORT_TYPE fd;

int main(int argc,char **argv)
{
  printf("Opening %s...\n",serial_port);
  open_the_serial_port(serial_port);
  printf("Setting speed to 1,000,000bps\n");
  set_serial_speed(fd, 1000000);
  
}
