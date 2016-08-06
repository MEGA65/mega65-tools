void cmdHelp(void);
void cmdDisassemble(void);
void cmdNext(void);
void cmdPrintByte(void);
void cmdPrintWord(void);
void cmdPrintDWord(void);

#define BUFSIZE 4096

extern char outbuf[];
extern char inbuf[];
