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
void cmdSetBreakpoint(void);

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

typedef struct tse
{
  int addr;
	char* symbol;
  struct tse* next;
} type_symmap_entry;

extern type_command_details command_details[];
extern type_symmap_entry* lstSymMap;
