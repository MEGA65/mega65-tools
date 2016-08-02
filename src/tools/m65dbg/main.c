/**
 * m65dbg - An enhanced remote serial debugger/monitor for the mega65 project
 **/

#include <stdio.h>
#include <string.h>
#include "serial.h"

#define VERSION "v1.00"
#define BUFSIZE 4096

char outbuf[BUFSIZE];
char inbuf[BUFSIZE];

/**
 * retrieves a command via user input
 */
char* get_command(void)
{
  printf("<dbg> ");
  fgets(outbuf, BUFSIZE, stdin);

  return NULL;
}

void parse_command(char* strInput)
{
  serialWrite(outbuf);
  serialRead(inbuf, BUFSIZE);
  printf(inbuf);
}

/**
 * main entry point of program
 *
 * argc = number of arguments
 * argv = string array of arguments
 */
int main(int argc, char** argv)
{
  printf("m65dbg - " VERSION "\n");
  printf("======\n");

  // open the serial port
  serialOpen("/dev/ttyS4");
  printf("- Type 'h' for help\n");

  while(1)
  {
    char *strInput = get_command();

    if (strcmp(outbuf, "exit\n") == 0)
      return 0;

    if (strlen(outbuf) == 0)
      continue;

    parse_command(strInput);
  }
}
