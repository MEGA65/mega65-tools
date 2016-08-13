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

//static int nf;
//static char** files;

char* my_generator(const char* text, int state)
{
    static int len;
    static type_symmap_entry* iter = NULL;

    if( !state )
    {
        len = strlen(text);
	iter = lstSymMap;
    }

    while(iter != NULL)
    {
        if( strncmp(iter->symbol, text, len) == 0 )
	{
	  char *s = strdup(iter->symbol);
	  iter = iter->next;
	  return s;
	}

	iter = iter->next;
    }
    return((char *)NULL);
}

static char** my_completion(const char * text, int start, int end)
{
    char **matches;
    matches = (char **)NULL;
    //if( start == 0 )
    //{
        matches = rl_completion_matches((char*)text, &my_generator);
    //}
    //else
    //  rl_bind_key('\t',rl_insert);
    return( matches );
}

/**
 * main entry point of program
 *
 * argc = number of arguments
 * argv = string array of arguments
 */
int main(int argc, char** argv)
{
  char devSerial[100] = "/dev/ttyS4";

  signal(SIGINT, ctrlc_handler);
  rl_initialize();

  printf("m65dbg - " VERSION "\n");
  printf("======\n");

  // check parameters
  for (int k = 1; k < argc; k++)
  {
    if (strcmp(argv[k], "--help") == 0 ||
	strcmp(argv[k], "-h") == 0)
    {
      printf("--help/-h = display this help\n"
	     "--device/-d </dev/tty*> = select a tty device-name to use as the serial port to communicate with the Nexys hardware\n");
      exit(0);
    }
    if (strcmp(argv[k], "--device") == 0 ||
	strcmp(argv[k], "-d") == 0)
    {
      if (k+1 >= argc)
      {
        printf("Device name for serial port is missing (e.g., /dev/ttyS0)\n");
	exit(0);
      }
      k++;
      strcpy(devSerial, argv[k]);
    }
  }

  // open the serial port
  serialOpen(devSerial);
  printf("- Type 'help' for new commands, '?'/'h' for raw commands.\n");

  listSearch();

  while(1)
  {
    ctrlcflag = false;

    rl_attempted_completion_function = my_completion;

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
