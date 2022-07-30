#include "serial.h"

/** TODO: TEMPORARY stubs for serial **/
bool serialOpen(char *portName)
{
  return true;
}
bool serialClose(void)
{
  return true;
}
void serialWrite(char *string)
{
}
bool serialRead(char *buf, int bufsize)
{
  return true;
}
void serialBaud(bool fastmode)
{
}
void serialFlush(void)
{
}
/** TODO: m65common defines this, I guess: int fd = 0; **/

/** actually from mega65_ftp.c **/
int do_ftp(char *bitstream)
{
  return 0;
}
