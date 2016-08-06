#include <stdio.h>
#include <string.h>
#include "commands.h"
#include "serial.h"
#include "gs4510.h"

typedef struct
{
  int pc;
	int a;
	int x;
	int y;
	int z;
	int b;
	int sp;
	int mapl;
	int maph;
} reg_data;

typedef struct
{
  int addr;
  unsigned int b[16];
} mem_data;

char outbuf[BUFSIZE] = { 0 };	// the buffer of what command is output to the remote monitor
char inbuf[BUFSIZE] = { 0 }; // the buffer of what is read in from the remote monitor

reg_data get_regs(void)
{
  reg_data reg;
  char* line;
  serialWrite("r\n");
  serialRead(inbuf, BUFSIZE);
  line = strstr(inbuf+2, "\n") + 1;
  sscanf(line,"%04X %02X %02X %02X %02X %02X %04X %04X %04X",
    &reg.pc, &reg.a, &reg.x, &reg.y, &reg.z, &reg.b, &reg.sp, &reg.mapl, &reg.maph);

  return reg;
}


mem_data get_mem(int addr)
{
  mem_data mem;
  char str[100];
  sprintf(str, "d%04X\n", addr); // use 'd' instead of 'm' (for memory in cpu context)
  serialWrite(str);
  serialRead(inbuf, BUFSIZE);
  sscanf(inbuf, " :%X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
  &mem.addr, &mem.b[0], &mem.b[1], &mem.b[2], &mem.b[3], &mem.b[4], &mem.b[5], &mem.b[6], &mem.b[7], &mem.b[8], &mem.b[9], &mem.b[10], &mem.b[11], &mem.b[12], &mem.b[13], &mem.b[14], &mem.b[15]); 

  return mem;
}


void cmdHelp(void)
{
  printf("m65dbg commands\n"
         "===============\n"
	 "dis = disassemble\n"
	 "n = step to next instruction\n"
	 "[ENTER] = repeat last command\n"
   "q/x/exit = exit the program\n"
   );
}


void cmdDisassemble(void)
{
  char str[128] = { 0 };
  char s[32] = { 0 };
  int last_bytecount = 0;

  // get current register values
  reg_data reg = get_regs();

  // get memory at current pc
  mem_data mem = get_mem(reg.pc);

  // now, try to disassemble it

  // Program counter
  sprintf(str, "$%04X ", reg.pc);

  // Opcode and arguments
  sprintf(s, "%02X ", mem.b[0]);
  strcat(str, s);

  last_bytecount = mode_lut[mem.b[0]] + 1;

  if (last_bytecount == 1)
  {
    strcat(str, "      ");
  }
  if (last_bytecount == 2)
  {
    sprintf(s, "%02X    ", mem.b[1]);
    strcat(str, s);
  }
  if (last_bytecount == 3)
  {
    sprintf(s, "%02X ", mem.b[2]);
    strcat(str, s);
  }

  // Instruction name
  strcat(str, instruction_lut[mem.b[0]]);

  printf("%s\n", str);
}

void cmdNext(void)
{
  // just send an enter command
  serialWrite("\n");
  serialRead(inbuf, BUFSIZE);

  printf(inbuf);

  cmdDisassemble();
}
