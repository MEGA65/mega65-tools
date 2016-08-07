/**
 * m65dbg - An enhanced remote serial debugger/monitor for the mega65 project
 **/

#include <stdio.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <signal.h>
#include <stdlib.h>
#include "serial.h"
#include "commands.h"

#define VERSION "v1.00"

char *strInput = NULL;

/**
 * retrieves a command via user input and places it in global strInput
 */
void get_command(void)
{
  strInput = readline("<dbg>");
}


void parse_command(void)
{
  char* token;
  bool handled = false;

  // if command is empty, then repeat last command
  if (strlen(strInput) == 0)
  {
    free(strInput);
    strInput = (char*)malloc(strlen(outbuf)+1);
    strcpy(strInput, outbuf);
  }

  // ignore no command
  if (strlen(strInput) == 0)
    return;

  // preserve a copy of original command
  strcpy(outbuf, strInput);

  // tokenise command
  token = strtok(strInput, " ");

  // test for special commands provided by the m65dbg app
  for (int k = 0; command_details[k].name != NULL; k++)
  {
    if (strcmp(token, command_details[k].name) == 0)
    {
      command_details[k].func();
      handled = true;
      break;
    }
  }

  // if command is not handled by m65dbg, then just pass across raw command
  if (!handled)
  {
    serialWrite(strInput);
    serialRead(inbuf, BUFSIZE);
    printf(inbuf);
  }
  
  if (strInput != NULL)
  {
    free(strInput);
    strInput = NULL;
  }

}

// use ctrl-c to break out of any commands that loop (eg, finish/next)
void ctrlc_handler(int s)
{
  ctrlcflag = true;
}

/**
 * main entry point of program
 *
 * argc = number of arguments
 * argv = string array of arguments
 */
int main(int argc, char** argv)
{
  signal(SIGINT, ctrlc_handler);
  rl_initialize();

  printf("m65dbg - " VERSION "\n");
  printf("======\n");

  // open the serial port
  serialOpen("/dev/ttyS4");
  printf("- Type 'h' for help\n");

  listSearch();

  while(1)
  {
    ctrlcflag = false;
    get_command();

    if (strcmp(strInput, "exit") == 0 ||
        strcmp(strInput, "x") == 0 ||
        strcmp(strInput, "q") == 0)
      return 0;

    if (strInput && *strInput)
      add_history(strInput);

    parse_command();

  }
}
