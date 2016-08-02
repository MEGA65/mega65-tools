/**
 * m65dbg - An enhanced remote serial debugger/monitor for the mega65 project
 **/

#include <stdio.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdlib.h>
#include "serial.h"

#define VERSION "v1.00"
#define BUFSIZE 4096

char* outbuf = NULL;	// the buffer of what command is output to the remote monitor
char inbuf[BUFSIZE] = { 0 }; // the buffer of what is read in from the remote monitor

/**
 * retrieves a command via user input
 */
char* get_command(void)
{
  outbuf = readline("<dbg>");

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
  rl_initialize();

  printf("m65dbg - " VERSION "\n");
  printf("======\n");

  // open the serial port
  serialOpen("/dev/ttyS4");
  printf("- Type 'h' for help\n");

  while(1)
  {
    char *strInput = get_command();

    if (strcmp(outbuf, "exit") == 0)
      return 0;

    if (strlen(outbuf) == 0)
      continue;

    if (outbuf && *outbuf)
      add_history(outbuf);

    parse_command(strInput);

    if (outbuf != NULL)
    {
      free(outbuf);
      outbuf = NULL;
    }
  }
}
