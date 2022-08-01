#include <stdbool.h>
#include <string.h>
#include "m65common.h"
#include "serial.h"

void serialWrite(char *string)
{
  serialFlush();

  int i = strlen(string);
  char *out = malloc(i+2);
  strlcpy(out, string, i+2);
  if (string[i-1] != '\n')
  {
    out[i] = '\n';
    out[i+1] = '\0';
  }
  slow_write(fd, out, i+1);
  free(out);

  // Xemu needs an extra delay.
  if (xemu_flag) usleep(10000);
}

// Timeout with exponential backoff
// Total timeout = 1000 + 2000 + 4000 + 8000 = 15000 us
#define TIMEOUT_START_US 1000
#define TIMEOUT_MAX_US 8000

bool serialRead(char *buf, int bufsize)
{
  char* secondline = NULL;
  bool foundLF = false;

  // Read repeatedly until dot prompt, with timeout
  uint8_t* start = (uint8_t *)buf;
  int rest_size = bufsize;
  int bytes_read;
  bool found_dot = false;
  int timeout_us = TIMEOUT_START_US;
  while (start < (uint8_t *)buf+bufsize-1) {
    bytes_read = serialport_read(fd, start, rest_size);
    if (bytes_read < 0)
      bytes_read = 0;
    start += bytes_read;
    rest_size -= bytes_read;

    found_dot = ((uint8_t *)buf-start > 2) && start[-2] == '\n' && start[-1] == '.';
    if (found_dot) {
      break;
    } else if (bytes_read < 1) {
      if (timeout_us > TIMEOUT_MAX_US)
        break;
      usleep(timeout_us);
      timeout_us *= 2;
    } else {
      timeout_us = TIMEOUT_START_US;
    }
  }
  *start = '\0';

  // Truncate first line, check for and truncate dot prompt
  for (int k = 0; k < bufsize; k++)
  {
    if ( *(buf+k) == '\n' )
    {
      foundLF = true;
      if (!secondline)
        secondline = buf+k+1;
    }
    else if (foundLF && *(buf+k) == '.')
    {
      *(buf+k) = '\0';

      int len = strlen(secondline) + 1;
      for (int z = 0; z < len; z++)
        *(buf+z) = *(secondline+z);
      return true;
    }
    else
      foundLF = false;
  }

  // Did not see dot prompt
  return false;
}

void serialBaud(bool fastmode)
{
  set_serial_speed(fd, fastmode ? 4000000 : 2000000);
}

void serialFlush(void)
{
  int bytes_available = 0;
  static char tmp[16384];
#ifdef FIONREAD
  ioctl(fd, FIONREAD, &bytes_available);
#else
  ioctl(fd, TIOCINQ, &bytes_available);
#endif
  if (bytes_available > 0)
    read(fd, tmp, bytes_available);
}
