/**
 * m65dbg - An enhanced remote serial debugger/monitor for the mega65 project
 **/

#define _BSD_SOURCE _BSD_SOURCE
#include <stdio.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
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
    serialWrite(outbuf);
    serialRead(inbuf, BUFSIZE);
    printf("%s", inbuf);
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

  if (cmdGetContinueMode())
  {
    // just send an enter command
    serialWrite("t1\n");
    serialRead(inbuf, BUFSIZE);

    cmdSetContinueMode(false);
  }
}

//static int nf;
//static char** files;

char* my_generator(const char* text, int state)
{
  static int len;
  static type_symmap_entry* iter = NULL;
  static int cmd_idx = 0;

  if( !state )
  {
    len = strlen(text);
    iter = lstSymMap;
    cmd_idx = 0;
  }

  // check if it is a symbol name
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

  while (cmd_idx < cmdGetCmdCount())
  {
    if (strncmp(cmdGetCmdName(cmd_idx), text, len) == 0 )
    {
      char *s = strdup(cmdGetCmdName(cmd_idx));
      cmd_idx++;
      return s;
    }
    cmd_idx++;
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

/** Look for a "~/.m65dbginit" file.
 *
 * If one exists, load it and run the commands within it.
*/
void run_m65dbg_init_file_commands()
{
  // file exists
  char* HOME = getenv("HOME");
  char filepath[256];
  sprintf(filepath, "%s/.m65dbg_init", HOME);
  if( access( filepath, F_OK ) != -1 )
  {
    printf("Loading \"%s\"...\n", filepath);

    FILE* f = fopen(filepath, "r");
    char* line = NULL;
    size_t len = 0;

    while (getline(&line, &len, f) != -1)
    {
      // remove any newline character at end of line
      line[strcspn(line, "\n")] = 0;

      // ignore empty lines
      if (strlen(line) == 0)
        continue;

      // ignore any lines that start with '#', treat these as comments
      if (strlen(line) > 0 && line[0] == '#')
        continue;

      // execute each line
      strInput = strdup(line);
      parse_command();
    }

    if (line != NULL)
      free(line);
  }
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

  run_m65dbg_init_file_commands();

  while(1)
  {
    ctrlcflag = false;

    rl_attempted_completion_function = my_completion;

    get_command();

    if (!strInput ||
        strcmp(strInput, "exit") == 0 ||
        strcmp(strInput, "x") == 0 ||
        strcmp(strInput, "q") == 0)
      return 0;

    if (strInput && *strInput)
      add_history(strInput);

    parse_command();

  }
}
