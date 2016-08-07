#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
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

char* get_extension(char* fname)
{
  return strrchr(fname, '.');
}

typedef struct tfl
{
	int addr;
  char* file;
	int lineno;
	struct tfl *next;
} type_fileloc;

type_fileloc* lstFileLoc = NULL;

void add_to_list(type_fileloc fl)
{
	type_fileloc* iter = lstFileLoc;

  // first entry in list?
  if (lstFileLoc == NULL)
	{
		lstFileLoc = malloc(sizeof(type_fileloc));
		lstFileLoc->addr = fl.addr;
		lstFileLoc->file = strdup(fl.file);
		lstFileLoc->lineno = fl.lineno;
		lstFileLoc->next = NULL;
		return;
	}

  while (iter != NULL)
	{
	  // replace existing?
	  if (iter->addr == fl.addr)
		{
			iter->file = strdup(fl.file);
			iter->lineno = fl.lineno;
			return;
		}
		// insert entry?
		if (iter->addr > fl.addr)
		{
		  type_fileloc* flcpy = malloc(sizeof(type_fileloc));
			flcpy->addr = iter->addr;
			flcpy->file = iter->file;
			flcpy->lineno = iter->lineno;
			flcpy->next = iter->next;

			iter->addr = fl.addr;
			iter->file = strdup(fl.file);
			iter->lineno = fl.lineno;
			iter->next = flcpy;
			return;
		}
		// add to end?
		if (iter->next == NULL)
		{
		  type_fileloc* flnew = malloc(sizeof(type_fileloc));
			flnew->addr = fl.addr;
			flnew->file = strdup(fl.file);
			flnew->lineno = fl.lineno;
			flnew->next = NULL;

			iter->next = flnew;
			return;
		}

		iter = iter->next;
	}
}

type_fileloc* find_in_list(int addr)
{
	type_fileloc* iter = lstFileLoc;

	while (iter != NULL)
	{
	  if (iter->addr == addr)
			return iter;

		iter = iter->next;
	}

	return NULL;
}

void load_list(char* fname)
{
  FILE* f = fopen(fname, "rt");
	char line[1024];

  while (!feof(f))
	{
	  fgets(line, 1024, f);

		if (strlen(line) == 0)
		  continue;

		char *s = strrchr(line, '|');
		if (s != NULL && *s != '\0')
		{
			s++;
			if (strlen(s) < 5)
			  continue;

			int addr;
      char file[1024];
			int lineno;
			strcpy(file, &strtok(s, ":")[1]);
			sscanf(strtok(NULL, ":"), "%d", &lineno);
			sscanf(line, " %X", &addr);

			//printf("%04X : %s:%d\n", addr, file, lineno);
			type_fileloc fl;
			fl.addr = addr;
			fl.file = file;
			fl.lineno = lineno;
			add_to_list(fl);
		}
	}
}


void show_location(type_fileloc* fl)
{
  FILE* f = fopen(fl->file, "rt");
	char line[1024];
	int cnt = 1;

	while (!feof(f))
	{
		fgets(line, 1024, f);
		if (cnt == fl->lineno)
		{
			printf("> %s", line);
			break;
		}
		cnt++;
	}
	fclose(f);
}

// search the current directory for *.list files
void listSearch(void)
{
  DIR           *d;
  struct dirent *dir;
  d = opendir(".");
  if (d)
  {
    while ((dir = readdir(d)) != NULL)
    {
		  char* ext = get_extension(dir->d_name);
		  if (ext != NULL && strcmp(ext, ".list") == 0)
			{
				printf("Loading \"%s\"...\n", dir->d_name);
				load_list(dir->d_name);
			}
    }

    closedir(d);
  }
}

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
   "pb <addr> = print byte\n"
   "pw <addr> = print word\n"
   "pd <addr> = print dword\n"
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

  type_opcode_mode mode = opcode_mode[mode_lut[mem.b[0]]];
  sprintf(s, " %10s:%d ", mode.name, mode.val);
  strcat(str, s);

  // Opcode and arguments
  sprintf(s, "%02X ", mem.b[0]);
  strcat(str, s);

  last_bytecount = mode.val + 1;

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
    sprintf(s, "%02X %02X ", mem.b[1], mem.b[2]);
    strcat(str, s);
  }

  // Instruction name
  strcat(str, instruction_lut[mem.b[0]]);

  switch(mode_lut[mem.b[0]])
  {
    case M_impl: break;
    case M_InnX:
      sprintf(s, " ($%02X,X)", mem.b[1]);
      strcat(str, s);
      break;
    case M_nn:
      sprintf(s, " $%02X", mem.b[1]);
      strcat(str, s);
      break;
    case M_immnn:
      sprintf(s, " #$%02X", mem.b[1]);
      strcat(str, s);
      break;
    case M_A: break;
    case M_nnnn:
      sprintf(s, " $%02X%02X", mem.b[2], mem.b[1]);
      strcat(str, s);
      break;
    case M_nnrr:
      sprintf(s, " $%02X,$%04X", mem.b[1], (reg.pc + 3 + mem.b[2]) );
      strcat(str, s);
      break;
    case M_rr:
      if (mem.b[1] & 0x80)
        sprintf(s, " $%04X", (reg.pc + 2 - 256 + mem.b[1]) );
      else
        sprintf(s, " $%04X", (reg.pc + 2 + mem.b[1]) );
      strcat(str, s);
      break;
    case M_InnY:
      sprintf(s, " ($%02X),Y", mem.b[1]);
      strcat(str, s);
      break;
    case M_InnZ:
      sprintf(s, " ($%02X),Z", mem.b[1]);
      strcat(str, s);
      break;
    case M_rrrr:
      sprintf(s, " $%04X", (reg.pc + 2 + (mem.b[2] << 8) + mem.b[1]) & 0xffff );
      strcat(str, s);
      break;
    case M_nnX:
      sprintf(s, " $%02X,X", mem.b[1]);
      strcat(str, s);
      break;
    case M_nnnnY:
      sprintf(s, " $%02X%02X,Y", mem.b[2], mem.b[1]);
      strcat(str, s);
      break;
    case M_nnnnX:
      sprintf(s, " $%02X%02X,X", mem.b[2], mem.b[1]);
      strcat(str, s);
      break;
    case M_Innnn:
      sprintf(s, " ($%02X%02X)", mem.b[2], mem.b[1]);
      strcat(str, s);
      break;
    case M_InnnnX:
      sprintf(s, " ($%02X%02X,X)", mem.b[2], mem.b[1]);
      strcat(str, s);
      break;
    case M_InnSPY:
      sprintf(s, " ($%02X,SP),Y", mem.b[1]);
      strcat(str, s);
      break;
    case M_nnY:
      sprintf(s, " $%02X,Y", mem.b[1]);
      strcat(str, s);
      break;
    case M_immnnnn:
      sprintf(s, " #$%02X%02X", mem.b[2], mem.b[1]);
      strcat(str, s);
      break;
  }

  type_fileloc *found = find_in_list(reg.pc);
	if (found)
	{
	  printf("> %s:%d\n", found->file, found->lineno);
		show_location(found);
	}
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

void cmdPrintByte(void)
{
  char* token = strtok(NULL, " ");
  
  if (token != NULL)
  {
    int val;
    sscanf(token, "%X", &val);

    mem_data mem = get_mem(val);

    printf("- %02X\n", mem.b[0]);
  }
}

void cmdPrintWord(void)
{
  char* token = strtok(NULL, " ");
  
  if (token != NULL)
  {
    int val;
    sscanf(token, "%X", &val);

    mem_data mem = get_mem(val);

    printf("- %02X%02X\n", mem.b[1], mem.b[0]);
  }
}

void cmdPrintDWord(void)
{
  char* token = strtok(NULL, " ");
  
  if (token != NULL)
  {
    int val;
    sscanf(token, "%X", &val);

    mem_data mem = get_mem(val);

    printf("- %02X%02X%02X%02X\n", mem.b[3], mem.b[2], mem.b[1], mem.b[0]);
  }
}
