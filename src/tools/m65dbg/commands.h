#include <stdbool.h>

void listSearch(void);
void cmdHelp(void);
void cmdDump(void);
void cmdMDump(void);
void cmdDisassemble(void);
void cmdStep(void);
void cmdNext(void);
void cmdFinish(void);
void cmdPrintByte(void);
void cmdPrintWord(void);
void cmdPrintDWord(void);
void cmdPrintString(void);
void cmdClearScreen(void);
void cmdAutoClearScreen(void);

#define BUFSIZE 4096

extern char outbuf[];
extern char inbuf[];
extern bool ctrlcflag;

typedef struct
{
  char* name;
  void (*func)(void);
  char* params;
  char* help;
} type_command_details;

extern type_command_details command_details[];
