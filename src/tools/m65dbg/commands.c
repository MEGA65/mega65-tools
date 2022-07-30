/* vim: set expandtab shiftwidth=2 tabstop=2: */

#define _BSD_SOURCE _BSD_SOURCE
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include "m65common.h"
#include "commands.h"
#include "serial.h"
#include "gs4510.h"
#include "screen_shot.h"

char pathBitstream[256] = "";
char devSerial[100] = "/dev/ttyUSB1";

int get_sym_value(char *token);

typedef struct {
  int pc;
  int a;
  int x;
  int y;
  int z;
  int b;
  int sp;
  int mapl;
  int maph;
  int lastop;
  int odd1; // what is this?
  int odd2; // what is this?
  char flags[16];
} reg_data;

typedef struct {
  int addr;
  unsigned int b[16];
} mem_data;

bool outputFlag = true;
bool continue_mode = false;

char outbuf[BUFSIZE] = { 0 }; // the buffer of what command is output to the remote monitor
char inbuf[BUFSIZE] = { 0 };  // the buffer of what is read in from the remote monitor

char *type_names[] = { "BYTE  ", "WORD  ", "DWORD ", "STRING", "DUMP  ", "MDUMP " };

bool autocls = false;   // auto-clearscreen flag
bool autowatch = false; // auto-watch flag
bool petscii = false;   // show chars in dumps based on petscii
bool fastmode = false;  // switch for slow (2,000,000bps) and fast (4,000,000bps) modes
bool ctrlcflag = false; // a flag to keep track of whether ctrl-c was caught
int traceframe = 0;     // tracks which frame within the backtrace

int dis_offs = 0;
int dis_scope = 10;

int softbrkaddr = 0;
unsigned char softbrkmem[3] = { 0 };

type_command_details command_details[] = { { "?", cmdRawHelp, NULL,
                                               "Shows help information for raw/native monitor commands" },
  { "help", cmdHelp, NULL, "Shows help information for m65dbg commands" },
  { "dump", cmdDump, "<addr16> [<count>]",
      "Dumps memory (CPU context) at given address (with character representation in right-column)" },
  { "mdump", cmdMDump, "<addr28> [<count>]",
      "Dumps memory (28-bit addresses) at given address (with character representation in right-column)" },
  { "a", cmdAssemble, "<addr28>", "Assembles instructions at the given <addr28> location." },
  { "dis", cmdDisassemble, "[<addr16> [<count>]]",
      "Disassembles the instruction at <addr> or at PC. If <count> exists, it will dissembly that many instructions "
      "onwards" },
  { "mdis", cmdMDisassemble, "[<addr28> [<count>]]",
      "Disassembles the instruction at <addr> or at PC. If <count> exists, it will dissembly that many instructions "
      "onwards" },
  { "c", cmdContinue, "[<addr>]", "continue (until optional <addr>) (equivalent to t0, but more m65dbg-friendly)" },
  { "sc", cmdSoftContinue, "[<addr>]",
      "soft continue (until optional <addr>) (equivalent to t0, but more m65dbg-friendly)" },
  { "step", cmdStep, "[<count>]",
      "Step into next instruction. If <count> is specified, perform that many steps" }, // equate to pressing 'enter' in raw
                                                                                        // monitor
  { "n", cmdNext, "[<count>]",
      "Step over to next instruction (software-based, slow). If <count> is specified, perform that many steps" },
  { "next", cmdHardNext, "[<count>]",
      "Step over to next instruction (hardware-based, fast, xemu-only, for now). If <count> is specified, perform that many "
      "steps" },
  { "finish", cmdFinish, NULL, "Continue running until function returns (ie, step-out-from)" },
  { "pb", cmdPrintByte, "<addr>", "Prints the byte-value of the given address" },
  { "pw", cmdPrintWord, "<addr>", "Prints the word-value of the given address" },
  { "pd", cmdPrintDWord, "<addr>", "Prints the dword-value of the given address" },
  { "ps", cmdPrintString, "<addr>", "Prints the null-terminated string-value found at the given address" },
  { "cls", cmdClearScreen, NULL, "Clears the screen" },
  { "autocls", cmdAutoClearScreen, "0/1", "If set to 1, clears the screen prior to every step/next command" },
  { "break", cmdSetBreakpoint, "<addr>", "Sets the hardware breakpoint to the desired address" },
  { "sbreak", cmdSetSoftwareBreakpoint, "<addr>", "Sets the software breakpoint to the desired address" },
  { "wb", cmdWatchByte, "<addr>", "Watches the byte-value of the given address" },
  { "ww", cmdWatchWord, "<addr>", "Watches the word-value of the given address" },
  { "wd", cmdWatchDWord, "<addr>", "Watches the dword-value of the given address" },
  { "ws", cmdWatchString, "<addr>", "Watches the null-terminated string-value found at the given address" },
  { "wdump", cmdWatchDump, "<addr> [<count>]", "Watches a dump of bytes at the given address" },
  { "wmdump", cmdWatchMDump, "<addr28> [<count>]", "Watches an mdump of bytes at the given 28-bit address" },
  { "watches", cmdWatches, NULL, "Lists all watches and their present values" },
  { "wdel", cmdDeleteWatch, "<watch#>/all",
      "Deletes the watch number specified (use 'watches' command to get a list of existing watch numbers)" },
  { "autowatch", cmdAutoWatch, "0/1", "If set to 1, shows all watches prior to every step/next/dis command" },
  { "symbol", cmdSymbolValue, "<symbol>", "retrieves the value of the symbol from the .map file" },
  { "save", cmdSave, "<binfile> <addr28> <count>",
      "saves out a memory dump to <binfile> starting from <addr28> and for <count> bytes" },
  { "load", cmdLoad, "<binfile> <addr28>", "loads in <binfile> to <addr28>" },
  { "back", cmdBackTrace, NULL, "produces a rough backtrace from the current contents of the stack" },
  { "up", cmdUpFrame, NULL, "The 'dis' disassembly command will disassemble one stack-level up from the current frame" },
  { "down", cmdDownFrame, NULL,
      "The 'dis' disassembly command will disassemble one stack-level down from the current frame" },
  { "se", cmdSearch, "<addr28> <len> <values>",
      "Searches the range you specify for the given values (either a list of hex bytes or a \"string\"" },
  { "ss", cmdScreenshot, NULL, "Takes an ascii screenshot of the mega65's screen" },
  { "ty", cmdType, "[<string>]",
      "Remote keyboard mode (if optional string provided, acts as one-shot message with carriage-return)" },
  { "ftp", cmdFtp, NULL, "FTP access to SD-card" },
  { "petscii", cmdPetscii, "0/1", "In dump commands, respect petscii screen codes" },
  { "fastmode", cmdFastMode, "0/1",
      "Used to quickly switch between 2,000,000bps (slow-mode: default) or 4,000,000bps (fast-mode: used in ftp-mode)" },
  { "scope", cmdScope, "<int>", "the scope-size of the listing to show alongside the disassembly" },
  { "offs", cmdOffs, "<int>", "the offset of the listing to show alongside the disassembly" },
  { "val", cmdPrintValue, "<$hex/dec/\%bin/>", "print the given value in hex, decimal and binary" },
  { "=", cmdForwardDis, "[<count>]", "move forward in disassembly of pc history from 'z' command" },
  { "-", cmdBackwardDis, "[<count>]", "move backward in disassembly of pc history from 'z' command" },
  { "mcopy", cmdMCopy, "<src_addr> <dest_addr> <count>",
      "copy data from source location to destination (28-bit addresses)" },
  { NULL, NULL, NULL, NULL } };

char *get_extension(char *fname)
{
  return strrchr(fname, '.');
}

typedef struct tfl {
  int addr;
  char *file;
  char *module;
  int lineno;
  struct tfl *next;
} type_fileloc;

type_fileloc *lstFileLoc = NULL;

type_fileloc *cur_file_loc = NULL;

type_symmap_entry *lstSymMap = NULL;

type_offsets segmentOffsets = { { 0 } };

type_offsets *lstModuleOffsets = NULL;

type_watch_entry *lstWatches = NULL;

void clearSoftBreak(void);
int isCpuStopped(void);
void setSoftBreakpoint(int addr);
void step(void);

void add_to_offsets_list(type_offsets mo)
{
  type_offsets *iter = lstModuleOffsets;
  mo.enabled = 1;

  if (iter == NULL) {
    lstModuleOffsets = malloc(sizeof(type_offsets));
    memcpy(lstModuleOffsets, &mo, sizeof(type_offsets));
    lstModuleOffsets->next = NULL;
    return;
  }

  while (iter != NULL) {
    // printf("iterating %s\n", iter->modulename);
    //  add to end?
    if (iter->next == NULL) {
      type_offsets *mo_new = malloc(sizeof(type_offsets));
      memcpy(mo_new, &mo, sizeof(type_offsets));
      mo_new->next = NULL;
      // printf("adding %s\n\n", mo_new->modulename);

      iter->next = mo_new;
      return;
    }
    iter = iter->next;
  }
}

void add_to_list(type_fileloc fl)
{
  type_fileloc *iter = lstFileLoc;

  // first entry in list?
  if (lstFileLoc == NULL) {
    lstFileLoc = malloc(sizeof(type_fileloc));
    lstFileLoc->addr = fl.addr;
    lstFileLoc->file = strdup(fl.file);
    lstFileLoc->lineno = fl.lineno;
    lstFileLoc->module = fl.module;
    lstFileLoc->next = NULL;
    return;
  }

  while (iter != NULL) {
    // replace existing?
    if (iter->addr == fl.addr) {
      iter->file = strdup(fl.file);
      iter->lineno = fl.lineno;
      return;
    }
    // insert entry?
    if (iter->addr > fl.addr) {
      type_fileloc *flcpy = malloc(sizeof(type_fileloc));
      flcpy->addr = iter->addr;
      flcpy->file = iter->file;
      flcpy->lineno = iter->lineno;
      flcpy->module = iter->module;
      flcpy->next = iter->next;

      iter->addr = fl.addr;
      iter->file = strdup(fl.file);
      iter->lineno = fl.lineno;
      iter->module = fl.module;
      iter->next = flcpy;
      return;
    }
    // add to end?
    if (iter->next == NULL) {
      type_fileloc *flnew = malloc(sizeof(type_fileloc));
      flnew->addr = fl.addr;
      flnew->file = strdup(fl.file);
      flnew->lineno = fl.lineno;
      flnew->module = fl.module;
      flnew->next = NULL;

      iter->next = flnew;
      return;
    }

    iter = iter->next;
  }
}

void add_to_symmap(type_symmap_entry sme)
{
  type_symmap_entry *iter = lstSymMap;

  // first entry in list?
  if (lstSymMap == NULL) {
    lstSymMap = malloc(sizeof(type_symmap_entry));
    lstSymMap->addr = sme.addr;
    lstSymMap->sval = strdup(sme.sval);
    lstSymMap->symbol = strdup(sme.symbol);
    lstSymMap->next = NULL;
    return;
  }

  while (iter != NULL) {
    // insert entry?
    if (iter->addr >= sme.addr) {
      type_symmap_entry *smecpy = malloc(sizeof(type_symmap_entry));
      smecpy->addr = iter->addr;
      smecpy->sval = iter->sval;
      smecpy->symbol = iter->symbol;
      smecpy->next = iter->next;

      iter->addr = sme.addr;
      iter->sval = strdup(sme.sval);
      iter->symbol = strdup(sme.symbol);
      iter->next = smecpy;
      return;
    }
    // add to end?
    if (iter->next == NULL) {
      type_symmap_entry *smenew = malloc(sizeof(type_symmap_entry));
      smenew->addr = sme.addr;
      smenew->sval = strdup(sme.sval);
      smenew->symbol = strdup(sme.symbol);
      smenew->next = NULL;

      iter->next = smenew;
      return;
    }

    iter = iter->next;
  }
}

void copy_watch(type_watch_entry *dest, type_watch_entry *src)
{
  dest->type = src->type;
  dest->name = strdup(src->name);
  dest->param1 = src->param1 ? strdup(src->param1) : NULL;
  dest->next = NULL;
}

void add_to_watchlist(type_watch_entry we)
{
  type_watch_entry *iter = lstWatches;

  // first entry in list?
  if (lstWatches == NULL) {
    lstWatches = malloc(sizeof(type_watch_entry));
    copy_watch(lstWatches, &we);
    return;
  }

  while (iter != NULL) {
    // add to end?
    if (iter->next == NULL) {
      type_watch_entry *wenew = malloc(sizeof(type_watch_entry));
      copy_watch(wenew, &we);
      iter->next = wenew;
      return;
    }

    iter = iter->next;
  }
}

type_fileloc *find_in_list(int addr)
{
  type_fileloc *iter = lstFileLoc;

  while (iter != NULL) {
    // printf("%s : addr=$%04X : line=%d\n", iter->file, iter->addr, iter->lineno);
    if (iter->addr == addr)
      return iter;

    iter = iter->next;
  }

  return NULL;
}

int find_addr_in_list(char *file, int line)
{
  type_fileloc *iter = lstFileLoc;

  while (iter != NULL) {
    if (!strcmp(file, iter->file) && iter->lineno == line)
      return iter->addr;

    iter = iter->next;
  }

  return -1;
}

type_fileloc *find_lineno_in_list(int lineno)
{
  type_fileloc *iter = lstFileLoc;

  if (!cur_file_loc)
    return NULL;

  while (iter != NULL) {
    if (strcmp(cur_file_loc->file, iter->file) == 0 && iter->lineno == lineno)
      return iter;

    iter = iter->next;
  }

  return NULL;
}

type_symmap_entry *find_in_symmap(char *sym)
{
  type_symmap_entry *iter = lstSymMap;

  while (iter != NULL) {
    if (strcmp(sym, iter->symbol) == 0)
      return iter;

    iter = iter->next;
  }

  return NULL;
}

type_watch_entry *find_in_watchlist(type_watch type, char *name)
{
  type_watch_entry *iter = lstWatches;

  while (iter != NULL) {
    if (strcmp(iter->name, name) == 0 && type == iter->type)
      return iter;

    iter = iter->next;
  }

  return NULL;
}

void free_watch(type_watch_entry *iter, int wnum)
{
  free(iter->name);
  if (iter->param1)
    free(iter->param1);
  free(iter);
  if (outputFlag)
    printf("watch#%d deleted!\n", wnum);
}

bool delete_from_watchlist(int wnum)
{
  int cnt = 0;

  type_watch_entry *iter = lstWatches;
  type_watch_entry *prev = NULL;

  while (iter != NULL) {
    cnt++;

    // we found the item to delete?
    if (cnt == wnum) {
      // first entry of list?
      if (prev == NULL) {
        lstWatches = iter->next;
        free_watch(iter, wnum);
        return true;
      }
      else {
        prev->next = iter->next;
        free_watch(iter, wnum);
        return true;
      }
    }

    prev = iter;
    iter = iter->next;
  }

  return false;
}

char *get_string_token(char *p, char *name);

char *get_nth_token(char *p, int n)
{
  static char token[128];

  for (int k = 0; k <= n; k++) {
    p = get_string_token(p, token);
    if (p == 0)
      return NULL;
  }

  return token;
}

char *get_string_token(char *p, char *name)
{
  int found_start = 0;
  int idx = 0;

  while (1) {
    if (!found_start) {
      if (*p == 0 || *p == '\r' || *p == '\n')
        return 0;

      if (*p != ' ' && *p != '\t') {
        found_start = 1;
        continue;
      }

      p++;
    }

    if (found_start) // found start of token, now look for end;
    {
      if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        name[idx] = 0;
        return ++p;
      }

      name[idx] = *p;
      p++;
      idx++;
    }
  }
}

bool starts_with(const char *str, const char *pre)
{
  return strncmp(pre, str, strlen(pre)) == 0;
}

void parse_ca65_segments(FILE *f, char *line)
{
  char name[128];
  char sval[128];
  int val;
  char *p;

  memset(&segmentOffsets, 0, sizeof(segmentOffsets));

  while (!feof(f)) {
    fgets(line, 1024, f);

    if (line[0] == '\0' || line[0] == '\r' || line[0] == '\n') {
      return;
    }

    p = line;
    if (!(p = get_string_token(p, name)))
      return;

    if (!(p = get_string_token(p, sval)))
      return;

    val = strtol(sval, NULL, 16);
    strcpy(segmentOffsets.segments[segmentOffsets.seg_cnt].name, name);
    segmentOffsets.segments[segmentOffsets.seg_cnt].offset = val;
    segmentOffsets.seg_cnt++;
  }
}

void parse_ca65_modules(FILE *f, char *line)
{
  char name[128];
  char sval[128];
  int val;
  int state = 0;
  char *p;
  type_offsets mo = { { 0 } };

  while (!feof(f)) {
    fgets(line, 1024, f);

    if (line[0] == '\0' || line[0] == '\r' || line[0] == '\n') {
      if (state == 1)
        add_to_offsets_list(mo);
      return;
    }

    if (strchr(line, ':') != NULL) {
      line[(int)(strchr(line, ':') - line)] = '\0';
      if (state == 1) {
        add_to_offsets_list(mo);
        memset(&mo, 0, sizeof(mo));
      }
      state = 0;
    }

    switch (state) {
    case 0: // get module name
      strcpy(mo.modulename, line);
      state = 1;
      break;

    case 1: // get segment offsets
      p = line;
      if (!(p = get_string_token(p, name)))
        return;

      if (!(p = get_string_token(p, sval)))
        return;

      if (!starts_with(sval, "Offs="))
        return;

      p = sval + 5;
      val = strtol(p, NULL, 16);
      strcpy(mo.segments[mo.seg_cnt].name, name);
      mo.segments[mo.seg_cnt].offset = val;
      mo.seg_cnt++;
      // printf("ca65 module: %s : %s - offs = $%04X\n", mo.modulename, name, val);
    }
  }
}

void parse_ca65_symbols(FILE *f, char *line)
{
  char name[128];
  char sval[128];
  int val;
  char str[64];

  while (!feof(f)) {
    fgets(line, 1024, f);

    // if (starts_with(line, "zerobss"))
    //   printf(line);

    char *p = line;
    for (int k = 0; k < 2; k++) {
      if (!(p = get_string_token(p, name)))
        return;

      p = get_string_token(p, sval);
      val = strtol(sval, NULL, 16);

      p = get_string_token(p, str); // ignore this 3rd one...

      type_symmap_entry sme;
      sme.addr = val;
      sme.sval = sval;
      sme.symbol = name;
      add_to_symmap(sme);
    }
  }
}

void load_ca65_map(FILE *f)
{
  char line[1024];
  rewind(f);
  while (!feof(f)) {
    fgets(line, 1024, f);

    if (starts_with(line, "Modules list:")) {
      fgets(line, 1024, f); // ignore following "----" line
      parse_ca65_modules(f, line);
      continue;
    }
    if (starts_with(line, "Segment list:")) {
      fgets(line, 1024, f); // ignore following "----" line
      fgets(line, 1024, f); // ignore following "Name" line
      fgets(line, 1024, f); // ignore following "----" line
      parse_ca65_segments(f, line);
    }
    if (starts_with(line, "Exports list by name:")) {
      fgets(line, 1024, f); // ignore following "----" line
      parse_ca65_symbols(f, line);
      continue;
    }
  }
}

void load_lbl(const char *fname)
{
  // load the map file
  FILE *f = fopen(fname, "rt");

  while (!feof(f)) {
    char line[1024];
    fgets(line, 1024, f);

    char saddr[128];
    char sym[1024];
    sscanf(line, "al %s %s", saddr, sym);

    type_symmap_entry sme;
    sme.addr = get_sym_value(saddr);
    sme.sval = saddr;
    sme.symbol = sym;
    add_to_symmap(sme);
    // printf("%s : %s\n", sme.sval, sym);
  }
  fclose(f);
}

// loads the *.map file corresponding to the provided *.list file (if one exists)
void load_map(const char *fname)
{
  char strMapFile[200];
  strcpy(strMapFile, fname);
  char *sdot = strrchr(strMapFile, '.');
  *sdot = '\0';
  strcat(strMapFile, ".map");

  // check if file exists
  if (access(strMapFile, F_OK) != -1) {
    printf("Loading \"%s\"...\n", strMapFile);

    // load the map file
    FILE *f = fopen(strMapFile, "rt");
    int first_line = 1;

    while (!feof(f)) {
      char line[1024];
      char sval[256];
      fgets(line, 1024, f);

      if (first_line) {
        first_line = 0;
        if (starts_with(line, "Modules list:")) {
          load_ca65_map(f);
          break;
        }
      }

      int addr;
      char sym[1024];
      sscanf(line, "$%04X | %s", &addr, sym);
      sscanf(line, "| %s", sval);

      // printf("%s : %04X\n", sym, addr);
      type_symmap_entry sme;
      sme.addr = addr;
      sme.sval = sval;
      sme.symbol = sym;
      add_to_symmap(sme);
    }
    fclose(f);
  }
}

int get_segment_offset(const char *current_segment)
{
  for (int k = 0; k < segmentOffsets.seg_cnt; k++) {
    if (strcmp(current_segment, segmentOffsets.segments[k].name) == 0) {
      return segmentOffsets.segments[k].offset;
    }
  }
  return 0;
}

int get_module_offset(const char *current_module, const char *current_segment)
{
  type_offsets *iter = lstModuleOffsets;

  while (iter != NULL) {
    if (strcmp(current_module, iter->modulename) == 0) {
      for (int k = 0; k < iter->seg_cnt; k++) {
        if (strcmp(current_segment, iter->segments[k].name) == 0) {
          return iter->segments[k].offset;
        }
      }
    }
    iter = iter->next;
  }
  return 0;
}

char *get_module_string(const char *current_module)
{
  type_offsets *iter = lstModuleOffsets;

  while (iter != NULL) {
    if (strcmp(current_module, iter->modulename) == 0) {
      return iter->modulename;
    }
    iter = iter->next;
  }
  return 0;
}

void load_ca65_list(const char *fname, FILE *f)
{
  static char list_file_name[256];
  strcpy(list_file_name, fname); // preserve a copy of this eternally

  load_map(fname); // load the ca65 map file first, as it contains details that will help us parse the list file

  char line[1024];
  char current_module[256] = { 0 };
  char current_segment[256] = { 0 };
  int lineno = 1;
  char *cmod = NULL;

  while (!feof(f)) {
    lineno++;
    fgets(line, 1024, f);

    if (starts_with(line, "Current file:")) {
      // Retrieve the current file/module that was assembled
      if (strchr(line, '/') != NULL) // truncate a relative location like 'src/utilities/remotesd.s'?
      {
        strcpy(current_module, strrchr(line, '/') + 1);
      }
      else
        strcpy(current_module, strchr(line, ':') + 2);
      current_module[strlen(current_module) - 1] = '\0';
      current_module[strlen(current_module) - 1] = 'o';
      current_segment[0] = '\0';
      // printf("current_module=%s\n", current_module);
      cmod = get_module_string(current_module);
    }

    if (line[0] == '\0' || line[0] == '\r' || line[0] == '\n')
      continue;

    // new .segment specified in code?
    char *p = get_nth_token(line, 2);
    if (p != NULL && strcasecmp(p, ".segment") == 0) {
      char *p = get_nth_token(line, 3);
      strncpy(current_segment, p + 1, strlen(p + 1) - 1);
      current_segment[strlen(p + 1) - 1] = '\0';
      // if (strcmp(current_module, "fdisk_fat32.o") == 0)
      // printf("line: %s\ncurrent_segment=%s\n", line, current_segment);
      // if (strcmp(current_segment, "CODET") == 0)
      // exit(0);
    }

    // did we find a line with a relocatable address at the start of it
    if (line[0] != ' ' && line[1] != ' ' && line[2] != ' ' && line[3] != ' ' && line[4] != ' ' && line[5] != ' '
        && (line[6] == 'r' || line[6] == ' ') && line[7] == ' ' && line[8] != ' ') {
      char saddr[8];
      int addr;
      strncpy(saddr, line, 6);
      saddr[7] = '\0';
      addr = strtol(saddr, NULL, 16);

      // convert relocatable address into absolute address
      if (line[6] == 'r') {
        addr += get_segment_offset(current_segment);
        addr += get_module_offset(current_module, current_segment);
      }

      // if (strcmp(current_module, "fdisk_fat32.o") == 0 /*&& strcmp(current_segment, "CODE") == 0 */ && addr <= 0x1000)
      // printf("mod=%s:seg=%s : %08X : %s", current_module, current_segment, addr, line);
      type_fileloc fl = { 0 };
      fl.addr = addr;
      fl.file = list_file_name;
      fl.module = cmod;
      fl.lineno = lineno;
      add_to_list(fl);
    }
  }
}

// loads the given *.list file
void load_list(char *fname)
{
  FILE *f = fopen(fname, "rt");
  char line[1024];
  int first_line = 1;

  while (!feof(f)) {
    fgets(line, 1024, f);

    if (first_line) {
      first_line = 0;
      if (starts_with(line, "ca65")) {
        load_ca65_list(fname, f);
        fclose(f);
        return;
      }
    }

    if (strlen(line) == 0)
      continue;

    char *s = strrchr(line, '|');
    if (s != NULL && *s != '\0') {
      s++;
      if (strlen(s) < 5)
        continue;

      int addr;
      char file[1024];
      int lineno;
      strcpy(file, &strtok(s, ":")[1]);
      if (strrchr(line, '/'))
        strcpy(file, strrchr(file, '/') + 1);
      sscanf(strtok(NULL, ":"), "%d", &lineno);
      sscanf(line, " %X", &addr);

      // printf("%04X : %s:%d\n", addr, file, lineno);
      type_fileloc fl = { 0 };
      fl.addr = addr;
      fl.file = file;
      fl.lineno = lineno;
      fl.module = NULL;
      add_to_list(fl);
    }
  }
  fclose(f);

  load_map(fname);
}

int is_hexc(char c)
{
  if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
    return true;

  return false;
}

void load_bsa_list(char *fname)
{
  FILE *f = fopen(fname, "rt");
  char line[1024];
  int lineno = 0;

  while (!feof(f)) {
    fgets(line, 1024, f);
    lineno++;

    if (strlen(line) < 8)
      continue;

    if (is_hexc(line[0]) && is_hexc(line[1]) && is_hexc(line[2]) && is_hexc(line[3]) && line[4] == ' ') {
      if (is_hexc(line[5]) && is_hexc(line[6])) {
        type_fileloc fl = { 0 };
        int addr;
        sscanf(line, "%X", &addr);
        fl.addr = addr;
        fl.file = fname;
        fl.lineno = lineno;
        fl.module = NULL;
        add_to_list(fl);
      }
      else {
        char tok[256];
        sscanf(line + 5, "%s", tok);
        if (tok[0] != '*') {
          // add to map?
          type_symmap_entry sme;
          int addr;
          char sval[256];
          sscanf(line, "%X", &addr);
          sprintf(sval, "$%04X", addr);
          sme.addr = addr;
          sme.sval = sval;
          sme.symbol = tok;
          add_to_symmap(sme);
          // printf("%s : %s ($%04X) : %s\n", line, sval, addr, sme.symbol);
        }
      }
    }
  }
}

void load_acme_map(const char *fname)
{
  char strMapFile[200];
  strcpy(strMapFile, fname);
  char *sdot = strrchr(strMapFile, '.');
  *sdot = '\0';
  strcat(strMapFile, ".sym");

  // check if file exists
  if (access(strMapFile, F_OK) != -1) {
    printf("Loading \"%s\"...\n", strMapFile);

    // load the map file
    FILE *f = fopen(strMapFile, "rt");

    while (!feof(f)) {
      char line[1024];
      char sval[256];
      int addr;
      char sym[1024];
      fgets(line, 1024, f);
      sscanf(line, "%s = %s", sym, sval);
      sscanf(sval, "$%04X", &addr);

      // printf("%s : %04X\n", sym, addr);
      type_symmap_entry sme;
      sme.addr = addr;
      sme.sval = sval;
      sme.symbol = sym;
      add_to_symmap(sme);
    }
    fclose(f);
  }
}

bool is_hex(const char *str)
{
  char c;
  for (int k = 0; k < strlen(str); k++) {
    c = str[k];
    if ((c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'))
      continue;
    else
      return false;
  }

  return true;
}

void load_acme_list(char *fname)
{
  FILE *f = fopen(fname, "rt");
  char line[1024];
  char curfile[256] = "";
  int lineno, memaddr;
  char val1[256];
  char val2[256];

  while (!feof(f)) {
    fgets(line, 1024, f);

    if (starts_with(line, "; ****") && strstr(line, "Source:")) {
      char *asmname = strstr(line, "Source: ") + strlen("Source: ");
      if (strrchr(asmname, '/'))
        asmname = strrchr(asmname, '/') + 1;
      sscanf(asmname, "%s", curfile);
    }
    else {
      if (sscanf(line, "%d %s %s", &lineno, val1, val2) == 3) {
        if (is_hex(val1) && is_hex(val2)) {
          sscanf(val1, "%04X", &memaddr);
          type_fileloc fl = { 0 };
          fl.addr = memaddr;
          fl.file = curfile;
          fl.lineno = lineno;
          add_to_list(fl);
        }
      }
    }
  }
  fclose(f);

  load_acme_map(fname);
}

#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"
#define KINV "\x1B[7m"
#define KCLEAR "\x1B[2J"
#define KPOS0_0 "\x1B[1;1H"

void show_location(type_fileloc *fl)
{
  FILE *f = fopen(fl->file, "rt");
  if (f == NULL)
    return;
  char line[1024];
  int cnt = 1;

  while (!feof(f)) {
    fgets(line, 1024, f);
    if (cnt >= (fl->lineno - dis_scope + dis_offs) && cnt <= (fl->lineno + dis_scope + dis_offs)) {
      int addr = find_addr_in_list(fl->file, cnt);
      char saddr[16] = "       ";
      if (addr != -1)
        sprintf(saddr, "[$%04X]", addr);

      if (cnt == fl->lineno) {
        printf("%s> L%d: %s %s%s", KINV, cnt, saddr, line, KNRM);
      }
      else
        printf("> L%d: %s %s", cnt, saddr, line);
      // break;
    }
    cnt++;
  }
  fclose(f);
}

// search the current directory for *.list files
void listSearch(void)
{
  DIR *d;
  struct dirent *dir;
  d = opendir(".");
  if (d) {
    while ((dir = readdir(d)) != NULL) {
      char *ext = get_extension(dir->d_name);
      // VICE label file?
      if (ext != NULL && strcmp(ext, ".lbl") == 0) {
        printf("Loading \"%s\"...\n", dir->d_name);
        load_lbl(dir->d_name);
      }
      // .lst = BSA Compiler for MEGA65 ROM
      if (ext != NULL && strcmp(ext, ".lst") == 0) {
        printf("Loading \"%s\"...\n", dir->d_name);
        load_bsa_list(dir->d_name);
      }
      // .list = Ophis or CA65?
      if (ext != NULL && strcmp(ext, ".list") == 0) {
        printf("Loading \"%s\"...\n", dir->d_name);
        load_list(dir->d_name);
      }
      // .rep = ACME (report file, equivalent to .list)
      else if (ext != NULL && strcmp(ext, ".rep") == 0) {
        printf("Loading \"%s\"...\n", dir->d_name);
        load_acme_list(dir->d_name);
      }
    }

    closedir(d);
  }
}

reg_data get_regs(void)
{
  reg_data reg = { 0 };
  char *line;
  while (1) {
    serialWrite("r\n");
    serialRead(inbuf, BUFSIZE);
    line = strstr(inbuf + 2, "\n");
    if (!line) // did we hit a null+1? try again
      continue;
    line++;
    sscanf(line, "%04X %02X %02X %02X %02X %02X %04X %04X %04X %02X %02X %02X %s", &reg.pc, &reg.a, &reg.x, &reg.y, &reg.z,
        &reg.b, &reg.sp, &reg.maph, &reg.mapl, &reg.lastop, &reg.odd1, &reg.odd2, reg.flags);
    break;
  }

  return reg;
}

void show_regs(reg_data *reg)
{
  printf("PC   A  X  Y  Z  B  SP   MAPH MAPL LAST-OP     P  P-FLAGS   RGP uS IO\n");
  printf("%04X %02X %02X %02X %02X %02X %04X %04X %04X %02X       %02X %02X %s\n", reg->pc, reg->a, reg->x, reg->y, reg->z,
      reg->b, reg->sp, reg->maph, reg->mapl, reg->lastop, reg->odd1, reg->odd2, reg->flags);
}

mem_data get_mem(int addr, bool useAddr28)
{
  mem_data mem = { 0 };
  char str[100];
  if (useAddr28)
    sprintf(str, "m%07X\n", addr); // use 'm' (for 28-bit memory addresses)
  else
    sprintf(str, "m777%04X\n", addr); // set upper 12-bis to $777xxxx (for memory in cpu context)

  serialWrite(str);
  serialRead(inbuf, BUFSIZE);
  sscanf(inbuf, ":%X:%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", &mem.addr, &mem.b[0], &mem.b[1],
      &mem.b[2], &mem.b[3], &mem.b[4], &mem.b[5], &mem.b[6], &mem.b[7], &mem.b[8], &mem.b[9], &mem.b[10], &mem.b[11],
      &mem.b[12], &mem.b[13], &mem.b[14], &mem.b[15]);

  return mem;
}

int peek(unsigned int address)
{
  mem_data mem = get_mem(address, true);
  return mem.b[0];
}

// read all 16 at once (to hopefully speed things up for saving memory dumps)
mem_data *get_mem28array(int addr)
{
  static mem_data multimem[32];
  mem_data *mem;
  char str[100];
  sprintf(str, "M%04X\n", addr);
  serialWrite(str);
  serialRead(inbuf, BUFSIZE);
  char *strLine = strtok(inbuf, "\n");
  for (int k = 0; k < 16; k++) {
    mem = &multimem[k];
    sscanf(strLine, ":%X:%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", &mem->addr, &mem->b[0],
        &mem->b[1], &mem->b[2], &mem->b[3], &mem->b[4], &mem->b[5], &mem->b[6], &mem->b[7], &mem->b[8], &mem->b[9],
        &mem->b[10], &mem->b[11], &mem->b[12], &mem->b[13], &mem->b[14], &mem->b[15]);
    strLine = strtok(NULL, "\n");
  }

  return multimem;
}

// write buffer to client ram
void put_mem28array(int addr, unsigned char *data, int size)
{
  char str[10];
  sprintf(outbuf, "s%08X", addr);

  int i = 0;
  while (i < size) {
    sprintf(str, " %02X", data[i]);
    strcat(outbuf, str);
    i++;
  }
  strcat(outbuf, "\n");

  serialWrite(outbuf);
  serialRead(inbuf, BUFSIZE);
}

void cmdRawHelp(void)
{
  serialWrite("?\n");
  serialRead(inbuf, BUFSIZE);
  printf("%s", inbuf);

  printf(
      "! - reset machine\n"
      "f<low> <high> <byte> - Fill memory\n"
      "g<addr> - Set PC\n"
      "m<addr28> - Dump 16-bytes of memory (28bit addresses - use $777xxxx for CPU context)\n"
      "M<addr28> - Dump 512-bytes of memory (28bit addresses - use $777xxxx for CPU context)\n"
      "d<addr> - Disassemble one instruction (28bit addresses - use $777xxxx for CPU context)\n"
      "D<addr> - Disassemble 16 instructions (28bit addresses - use $777xxxx for CPU context)\n"
      "r - display CPU registers and last instruction executed\n"
      "s<addr28> <value> ... - Set memory (28bit addresses)\n"
      "S<addr> <value> ... - Set memory (CPU memory context)\n"
      "b[<addr>] - Set or clear CPU breakpoint\n"
      "t<0|1> - Enable/disable tracing\n"
      "tc - Traced execution until keypress\n"
      "t|BLANK LINE - Step one cpu cycle if in trace mode\n"
      "w<addr> - Sets a watchpoint to trigger when specified address is modified\n"
      "w - clear 'w' watchpoint\n"
      "e - set a breakpoint to occur based on CPU flags\n"
      "z - history command - show every memory access on the bus for the last hundred cycles\n"
      "+ - set bitrate (+9 = 4,000,000bps,  +13 = 2,000,000bps)\n"
      "i - irq command - (for masking interrupts)\n"
      "# - trap command - trigger a trap?\n"
      "E - flag break command - allows breaking on particular CPU flag settings\n"
      "l<addr28> <addr16> - load mem command (28-bit start address, lower 16-bits of end address, then feed in raw bytes)\n"
      "N - step over (xemu only)\n");
}

void cmdHelp(void)
{
  printf("m65dbg commands\n"
         "===============\n");

  for (int k = 0; command_details[k].name != NULL; k++) {
    type_command_details cd = command_details[k];

    if (cd.params == NULL)
      printf("%s = %s\n", cd.name, cd.help);
    else
      printf("%s %s = %s\n", cd.name, cd.params, cd.help);
  }

  printf("[ENTER] = repeat last command\n"
         "q/x/exit = exit the program\n");
}

void print_char(int c)
{
  if (petscii) {
    print_screencode(c, 1);
  }
  else {
    if (isprint(c) && c > 31)
      printf("%c", c);
    else
      printf(".");
  }
}

void dump(int addr, int total)
{
  int cnt = 0;
  while (cnt < total) {
    // get memory at current pc
    mem_data mem = get_mem(addr + cnt, false);

    printf(" :%07X ", mem.addr);
    for (int k = 0; k < 16; k++) {
      if (k == 8) // add extra space prior to 8th byte
        printf(" ");

      printf("%02X ", mem.b[k]);
    }

    printf(" | ");

    for (int k = 0; k < 16; k++) {
      int c = mem.b[k];
      print_char(c);
    }
    printf("\n");
    cnt += 16;

    if (ctrlcflag)
      break;
  }
}

void cmdDump(void)
{
  char *strAddr = strtok(NULL, " ");

  if (strAddr == NULL) {
    printf("Missing <addr> parameter!\n");
    return;
  }

  int addr = get_sym_value(strAddr);

  int total = 16;
  char *strTotal = strtok(NULL, " ");

  if (strTotal != NULL) {
    sscanf(strTotal, "%X", &total);
  }

  dump(addr, total);
}

void mdump(int addr, int total)
{
  int cnt = 0;
  while (cnt < total) {
    // get memory at current pc
    mem_data mem = get_mem(addr + cnt, true);

    printf(" :%07X ", mem.addr);
    for (int k = 0; k < 16; k++) {
      if (k == 8) // add extra space prior to 8th byte
        printf(" ");

      printf("%02X ", mem.b[k]);
    }

    printf(" | ");

    for (int k = 0; k < 16; k++) {
      int c = mem.b[k];
      print_char(c);
    }
    printf("\n");
    cnt += 16;

    if (ctrlcflag)
      break;
  }
}

void cmdMDump(void)
{
  char *strAddr = strtok(NULL, " ");

  if (strAddr == NULL) {
    printf("Missing <addr> parameter!\n");
    return;
  }

  int addr = get_sym_value(strAddr);

  int total = 16;
  char *strTotal = strtok(NULL, " ");

  if (strTotal != NULL) {
    sscanf(strTotal, "%X", &total);
  }

  mdump(addr, total);
}

// return the last byte count
int disassemble_addr_into_string(char *str, int addr, bool useAddr28)
{
  int last_bytecount = 0;
  char s[32] = { 0 };

  // get memory at current pc
  mem_data mem = get_mem(addr, useAddr28);

  // now, try to disassemble it

  // Program counter
  if (useAddr28)
    sprintf(str, "$%07X ", addr & 0xfffffff);
  else
    sprintf(str, "$%04X ", addr & 0xffff);

  type_opcode_mode mode = opcode_mode[mode_lut[mem.b[0]]];
  sprintf(s, " %10s:%d ", mode.name, mode.val);
  strcat(str, s);

  // Opcode and arguments
  sprintf(s, "%02X ", mem.b[0]);
  strcat(str, s);

  last_bytecount = mode.val + 1;

  if (last_bytecount == 1) {
    strcat(str, "      ");
  }
  if (last_bytecount == 2) {
    sprintf(s, "%02X    ", mem.b[1]);
    strcat(str, s);
  }
  if (last_bytecount == 3) {
    sprintf(s, "%02X %02X ", mem.b[1], mem.b[2]);
    strcat(str, s);
  }

  // Instruction name
  sprintf(s, "%-4s", instruction_lut[mem.b[0]]);
  strcat(str, s);

  switch (mode_lut[mem.b[0]]) {
  case M_impl:
    break;
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
  case M_A:
    break;
  case M_nnnn:
    sprintf(s, " $%02X%02X", mem.b[2], mem.b[1]);
    strcat(str, s);
    break;
  case M_nnrr:
    sprintf(s, " $%02X,$%04X", mem.b[1], (addr + 3 + mem.b[2]));
    strcat(str, s);
    break;
  case M_rr:
    if (mem.b[1] & 0x80)
      sprintf(s, " $%04X", (addr + 2 - 256 + mem.b[1]));
    else
      sprintf(s, " $%04X", (addr + 2 + mem.b[1]));
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
    sprintf(s, " $%04X", (addr + 2 + (mem.b[2] << 8) + mem.b[1]) & 0xffff);
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

  return last_bytecount;
}

int *get_backtrace_addresses(void)
{
  // get current register values
  reg_data reg = get_regs();

  static int addresses[8];

  // get memory at current pc
  mem_data mem = get_mem(reg.sp + 1, false);
  for (int k = 0; k < 8; k++) {
    int addr = mem.b[k * 2] + (mem.b[k * 2 + 1] << 8);
    addr -= 2;
    addresses[k] = addr;
  }

  return addresses;
}

#define PCCNT 400

int zpos = 0;
int zval[PCCNT];
int zflag = 0;

void getz(void)
{
  serialWrite("z\n");
  serialRead(inbuf, BUFSIZE);

  zflag = 1;

  char *strLine = strtok(inbuf, "\n");
  for (int k = 0; k < PCCNT; k++) {
    sscanf(strLine, "%X", &zval[k]);
    strLine = strtok(NULL, "\n");
  }
}

void disassemble(bool useAddr28)
{
  char str[128] = { 0 };
  int last_bytecount = 0;

  if (autowatch)
    cmdWatches();

  int addr;
  int cnt = 1; // number of lines to disassemble

  // get current register values
  reg_data reg = get_regs();
  if (autowatch) {
    show_regs(&reg);
    printf("---------------------------------------\n");
  }

  // get address from parameter?
  char *token = strtok(NULL, " ");

  if (token != NULL) {
    if (strcmp(token, "-") == 0) // '-' equates to current pc
    {
      // get current register values
      addr = reg.pc;
    }
    else
      addr = get_sym_value(token);

    token = strtok(NULL, " ");

    if (token != NULL) {
      cnt = get_sym_value(token);
    }
  }
  // default to current pc
  else {
    addr = reg.pc;
  }

  // are we in a different frame?
  if (addr == reg.pc && traceframe != 0) {
    int *addresses = get_backtrace_addresses();
    addr = addresses[traceframe - 1];

    printf("<<< FRAME#: %d >>>\n", traceframe);
  }

  int idx = 0;

  while (idx < cnt) {
    last_bytecount = disassemble_addr_into_string(str, addr, useAddr28);

    // print from .list ref? (i.e., find source in .a65 file?)
    if (idx == 0) {
      type_fileloc *found = find_in_list(addr);
      cur_file_loc = found;
      if (found) {
        if (found->module)
          printf("> \"%s\"  (%s:%d)\n", found->module, found->file, found->lineno);
        else
          printf("> %s::%d\n", found->file, found->lineno);
        show_location(found);
        printf("---------------------------------------\n");
      }
    }

    // just print the raw disassembly line
    if (cnt != 1 && idx == 0)
      printf("%s%s%s\n", KINV, str, KNRM);
    else
      printf("%s\n", str);

    if (ctrlcflag)
      break;

    addr += last_bytecount;
    idx++;
  } // end while
}

void cmdForwardDis(void)
{
  static char s[64];
  int cnt = 1;

  if (!zflag)
    getz();

  char *token = strtok(NULL, " ");
  if (token != NULL)
    sscanf(token, "%d", &cnt);

  if (zpos - cnt > 0)
    zpos -= cnt;

  sprintf(s, "dis %04X", zval[zpos]);
  strtok(s, " ");

  disassemble(false);
}

void cmdBackwardDis(void)
{
  static char s[64];
  int cnt = 1;

  if (!zflag)
    getz();

  char *token = strtok(NULL, " ");
  if (token != NULL)
    sscanf(token, "%d", &cnt);

  if (zpos + cnt < PCCNT)
    zpos += cnt;

  sprintf(s, "dis %04X", zval[zpos]);
  strtok(s, " ");

  disassemble(false);
}

void cmdMCopy(void)
{
  // get address from parameter?
  char *token = strtok(NULL, " ");

  if (token == NULL) {
    printf("Invalid args\n");
    return;
  }

  int src_addr = get_sym_value(token);

  token = strtok(NULL, " ");

  if (token == NULL) {
    printf("Invalid args\n");
    return;
  }

  int dest_addr = get_sym_value(token);

  token = strtok(NULL, " ");

  if (token == NULL) {
    printf("Invalid args\n");
    return;
  }

  int count = get_sym_value(token);

  char strCmd[256];
  int cnt = 0;
  while (cnt < count) {
    mem_data mem = get_mem(src_addr + cnt, true);
    sprintf(strCmd, "s%X ", dest_addr + cnt);
    for (int k = 0; k < 16; k++) {
      char strVal[10];
      sprintf(strVal, "%02X ", mem.b[k]);
      strcat(strCmd, strVal);
      cnt++;
      if (cnt >= count)
        break;
    }
    printf("%d%%...\r", 100 * cnt / count);
    strcat(strCmd, "\n");
    serialWrite(strCmd);
    serialRead(inbuf, BUFSIZE);

    if (ctrlcflag)
      break;
  }
  printf("\n");
}

void cmdDisassemble(void)
{
  disassemble(false);
}

void strupper(char *str)
{
  char *s = str;
  while (*s) {
    *s = toupper((unsigned char)*s);
    s++;
  }
}

void write_bytes(int *addr, int size, ...)
{
  va_list valist;
  va_start(valist, size);

  char str[16];
  sprintf(outbuf, "s%X", *addr);

  int i = 0;
  while (i < size) {
    sprintf(str, " %02X", va_arg(valist, int));
    strcat(outbuf, str);
    i++;
  }
  strcat(outbuf, "\n");

  serialWrite(outbuf);
  serialRead(inbuf, BUFSIZE);

  va_end(valist);

  *addr += size;
}

int isValidMnemonic(char *str)
{
  strupper(str);
  char *instr = strtok(str, " ");
  for (int k = 0; k < 256; k++) {
    if (strcmp(instruction_lut[k], instr) == 0)
      return 1;
  }
  return 0;
}

int getopcode(int mode, const char *instr)
{
  for (int k = 0; k < 256; k++) {
    if (strcmp(instruction_lut[k], instr) == 0) {
      if (mode_lut[k] == mode)
        return k;
    }
  }

  // couldn't find it
  return -1;
}

int modematcher(char *curmode, char *modeform, int *var1, int *var2)
{
  char str[8];
  char *p = curmode;
  int varcnt = 0;

  for (int k = 0; k < strlen(modeform); k++) {
    // skip any whitespaces in curmode
    while (*p == ' ')
      p++;

    // did we locate a var position?
    if (modeform[k] == '%') {
      k++; // skip over the '%x' token

      if (!isxdigit(*p)) // if no hexadecimal value here, then we've failed to match
        return 0;

      str[0] = '\0';
      int i = 0;
      while (isxdigit(*p)) // skip over the hexadecimal value
      {
        str[i] = *p;
        i++;
        p++;
      }
      str[i] = '\0';

      if (varcnt == 0)
        sscanf(str, "%x", var1);
      else
        sscanf(str, "%x", var2);
      varcnt++;
    }
    // check for non-var locations, such as '(', ')', ','
    else if (modeform[k] == *p) {
      p++;
    }
    else // we failed to match the non-var location
    {
      return 0;
    }
  }

  return varcnt;
}

// return -1=invalid-command, 0=no-command, 1=bytes written
int oneShotAssembly(int *paddr, char *strCommand)
{
  char str[128];
  int val1;
  int val2;
  int opcode = 0;
  int startaddr = *paddr;
  int invalid = 0;
  strcpy(str, strCommand);
  if (str[strlen(str) - 1] == '\n')
    str[strlen(str) - 1] = '\0';

  if (strlen(str) == 0)
    return 0;

  strupper(str);

  char *instr = strtok(str, " ");
  char *mode = strtok(NULL, "\0");

  // todo: check if instruction is valid.
  // if not, show syntax error.

  if (strcmp(instr, "NOP") == 0)
    strcpy(instr, "EOM");

  // figure out instruction mode
  if (mode == NULL || mode[0] == '\0') {
    if ((opcode = getopcode(M_impl, instr)) != -1) {
      write_bytes(paddr, 1, opcode);
    }
    else if ((opcode = getopcode(M_A, instr)) != -1) {
      write_bytes(paddr, 1, opcode);
    }
    else {
      invalid = 1;
    }
  }
  else if (modematcher(mode, "($%x,X)", &val1, &val2) == 1) {
    if ((opcode = getopcode(M_InnX, instr)) != -1 && val1 < 0x100) {
      // e.g. ORA ($20,X)
      write_bytes(paddr, 2, opcode, val1);
    }
    else if ((opcode = getopcode(M_InnnnX, instr)) != -1) {
      // e.g. JSR ($2000,X)
      write_bytes(paddr, 3, opcode, val1 & 0xff, val1 >> 8);
    }
    else {
      invalid = 1;
    }
  }
  else if (modematcher(mode, "$%x,$%x", &val1, &val2) == 2) {
    if ((opcode = getopcode(M_nnrr, instr)) != -1) {
      // e.g. BBR $20,$2005
      int rr = 0;
      // TODO: confirm this arithmetic (for M_nnrr in disassemble_addr_into_string() too)
      if (val2 > *paddr)
        rr = val2 - *paddr - 2;
      else
        rr = (val2 - *paddr - 2) & 0xff;

      write_bytes(paddr, 3, opcode, val1, rr);
    }
    else {
      invalid = 1;
    }
  }
  else if (modematcher(mode, "($%x),Y", &val1, &val2) == 1) {
    if ((opcode = getopcode(M_InnY, instr)) != -1) {
      // e.g. ORA ($20),Y
      write_bytes(paddr, 2, opcode, val1);
    }
    else {
      invalid = 1;
    }
  }
  else if (modematcher(mode, "($%x),Z", &val1, &val2) == 1) {
    if ((opcode = getopcode(M_InnZ, instr)) != -1) {
      // e.g. ORA ($20),Z
      write_bytes(paddr, 2, opcode, val1);
    }
    else {
      invalid = 1;
    }
  }
  else if (modematcher(mode, "$%x,X", &val1, &val2) == 1) {
    if ((opcode = getopcode(M_nnX, instr)) != -1 && val1 < 0x100) {
      // e.g. ORA $20,X
      write_bytes(paddr, 2, opcode, val1);
    }
    else if ((opcode = getopcode(M_nnnnX, instr)) != -1) {
      // e.g. ORA $2000,X
      write_bytes(paddr, 3, opcode, val1 & 0xff, val1 >> 8);
    }
    else {
      invalid = 1;
    }
  }
  else if (modematcher(mode, "$%x,Y", &val1, &val2) == 1) {
    if ((opcode = getopcode(M_nnY, instr)) != -1 && val1 < 0x100) {
      // e.g. STX $20,Y
      write_bytes(paddr, 2, opcode, val1);
    }
    else if ((opcode = getopcode(M_nnnnY, instr)) != -1) {
      // e.g. ORA $2000,Y
      write_bytes(paddr, 3, opcode, val1 & 0xff, val1 >> 8);
    }
    else {
      invalid = 1;
    }
  }
  else if (modematcher(mode, "($%x)", &val1, &val2) == 1) {
    if ((opcode = getopcode(M_Innnn, instr)) != -1) {
      // e.g. JSR ($2000)
      write_bytes(paddr, 3, opcode, val1 & 0xff, val1 >> 8);
    }
    else {
      invalid = 1;
    }
  }
  else if (modematcher(mode, "($%x,SP),Y", &val1, &val2) == 1) {
    if ((opcode = getopcode(M_InnSPY, instr)) != -1) {
      // e.g. STA ($20,SP),Y
      write_bytes(paddr, 2, opcode, val1);
    }
    else {
      invalid = 1;
    }
  }
  else if (modematcher(mode, "$%x", &val1, &val2) == 1) {
    int diff = val1 - *paddr;
    if (diff < 0)
      diff = -diff;

    if ((opcode = getopcode(M_nn, instr)) != -1 && val1 < 0x100) {
      // e.g. STA $20
      write_bytes(paddr, 2, opcode, val1);
    }
    else if ((opcode = getopcode(M_nnnn, instr)) != -1) {
      // e.g. STA $2000
      write_bytes(paddr, 3, opcode, val1 & 0xff, val1 >> 8);
    }

    else if ((opcode = getopcode(M_rr, instr)) != -1 && diff < 0x100) {
      int rr = 0;
      // TODO: confirm this arithmetic (for M_rr in disassemble_addr_into_string() too)
      if (val1 > *paddr)
        rr = val1 - *paddr - 2;
      else
        rr = (val1 - *paddr - 2) & 0xff;
      // e.g. BCC $2005
      write_bytes(paddr, 2, opcode, rr);
    }
    else if ((opcode = getopcode(M_rrrr, instr)) != -1) {
      int rrrr = 0;
      // TODO: confirm this arithmetic (for M_rrrr in disassemble_addr_into_string() too)
      if (val1 > *paddr)
        rrrr = val1 - *paddr - 2;
      else
        rrrr = (val1 - *paddr - 2) & 0xffff;
      // e.g. BPL $AD22
      write_bytes(paddr, 3, opcode, rrrr & 0xff, rrrr >> 8);
    }
    else {
      invalid = 1;
    }
  }
  else if (modematcher(mode, "#$%x", &val1, &val2) == 1) {
    // M_immnn
    // M_immnnnn
    if ((opcode = getopcode(M_immnn, instr)) != -1 && val1 < 0x100) {
      // e.g. LDA #$20
      write_bytes(paddr, 2, opcode, val1);
    }
    else if ((opcode = getopcode(M_immnnnn, instr)) != -1) {
      // e.g. PHW #$1234
      write_bytes(paddr, 3, opcode, val1 & 0xff, val1 >> 8);
    }
    else {
      invalid = 1;
    }
  }
  else {
    invalid = 1;
  }

  if (invalid)
    return -1;

  // return number of bytes written
  return *paddr - startaddr;
}

void cmdAssemble(void)
{
  int addr = 0;
  char str[128] = { 0 };
  char *token = strtok(NULL, " ");
  if (token != NULL) {
    addr = get_sym_value(token);
  }

  do {
    printf("$%07X ", addr);
    fgets(str, 128, stdin);
    int ret = oneShotAssembly(&addr, str);

    if (ret == -1) // invalid?
    {
      printf("???\n");
    }
    else if (ret == 0) // no command?
    {
      return;
    }

  } while (1);
}

void cmdMDisassemble(void)
{
  disassemble(true);
}

void do_continue(int do_soft_break)
{
  traceframe = 0;

  // get address from parameter?
  char *token = strtok(NULL, " ");

  // if <addr> field is provided, use it
  if (token) {
    int addr = get_sym_value(token);

    if (do_soft_break) {
      // set a soft breakpoint (more reliable for now...)
      // doOneShotAssembly("sei");
      step(); // step just once, just in-case user wants to repeat the last continue command (e.g., 'c 3aa9')
      setSoftBreakpoint(addr);
    }
    else {
      // set a hard breakpoint
      char str[100];
      sprintf(str, "b%04X\n", addr);
      serialWrite(str);
      serialRead(inbuf, BUFSIZE);
    }
  }

  // just send an enter command
  serialWrite("t0\n");
  serialRead(inbuf, BUFSIZE);

  // Try keep this in a loop that tests for a breakpoint
  // getting hit, or the user pressing CTRL-C to force
  // a "t1" command to turn trace mode back on
  int cur_pc = -1;
  int same_cnt = 0;
  continue_mode = true;
  while (1) {
    usleep(10000);

    if (ctrlcflag) {
      break;
    }

    // get current register values
    reg_data reg = get_regs();

    if (reg.pc == cur_pc || (softbrkaddr && (reg.pc >= (softbrkaddr & 0xffff) && reg.pc <= ((softbrkaddr + 3) & 0xffff)))) {
      same_cnt++;
      // printf("good addr=$%04X : same_cnt=%d (soft=$%04X)\n", reg.pc, same_cnt, softbrkaddr);
      if (same_cnt == 5) {
        if (reg.pc >= (softbrkaddr & 0xffff) && reg.pc <= ((softbrkaddr + 3) & 0xffff)) {
          clearSoftBreak();
          // doOneShotAssembly("cli");
        }
        break;
      }
    }
    else {
      // printf("lost addr=$%04X (soft=$%04X)\n", reg.pc, softbrkaddr);
      same_cnt = 0;
      cur_pc = reg.pc;
    }
  }

  continue_mode = false;
  if (autocls)
    cmdClearScreen();

  // show the registers
  // serialWrite("r\n");
  // serialRead(inbuf, BUFSIZE);
  // printf("%s", inbuf);

  cmdDisassemble();
}

void cmdSoftContinue(void)
{
  do_continue(1);
}

void cmdContinue(void)
{
  do_continue(0);
}

bool cmdGetContinueMode(void)
{
  return continue_mode;
}

void cmdSetContinueMode(bool val)
{
  continue_mode = val;
}

void hard_next(void)
{
  serialWrite("N\n");
  serialRead(inbuf, BUFSIZE);
}

void step(void)
{
  // just send an enter command
  serialWrite("\n");
  serialRead(inbuf, BUFSIZE);
}

void cmdHardNext(void)
{
  traceframe = 0;

  // get address from parameter?
  char *token = strtok(NULL, " ");

  int count = 1;

  // if <count> field is provided, use it
  if (token) {
    sscanf(token, "%d", &count);
  }

  for (int k = 0; k < count; k++) {
    hard_next();
  }

  if (outputFlag) {
    if (autocls)
      cmdClearScreen();
    printf("%s", inbuf);
    cmdDisassemble();
  }
}

void cmdStep(void)
{
  traceframe = 0;

  // get address from parameter?
  char *token = strtok(NULL, " ");

  int count = 1;

  // if <count> field is provided, use it
  if (token) {
    sscanf(token, "%d", &count);
  }

  for (int k = 0; k < count; k++) {
    step();
  }

  if (outputFlag) {
    if (autocls)
      cmdClearScreen();
    printf("%s", inbuf);
    cmdDisassemble();
  }
}

void cmdNext(void)
{
  traceframe = 0;

  // get address from parameter?
  char *token = strtok(NULL, " ");

  int count = 1;

  // if <count> field is provided, use it
  if (token) {
    sscanf(token, "%d", &count);
  }

  for (int k = 0; k < count; k++) {
    // check if this is a JSR command
    reg_data reg = get_regs();
    mem_data mem = get_mem(reg.pc, false);

    // if not, then just do a normal step
    if (strcmp(instruction_lut[mem.b[0]], "JSR") != 0) {
      step();
    }
    else {
      // if it is JSR, then keep doing step into until it returns to the next command after the JSR

      type_opcode_mode mode = opcode_mode[mode_lut[mem.b[0]]];
      int last_bytecount = mode.val + 1;
      int next_addr = reg.pc + last_bytecount;

      while (reg.pc != next_addr) {
        // just send an enter command
        serialWrite("\n");
        serialRead(inbuf, BUFSIZE);

        reg = get_regs();

        if (ctrlcflag)
          break;
      }

      // show disassembly of current position
      serialWrite("r\n");
      serialRead(inbuf, BUFSIZE);
    } // end if
  }   // end for

  if (outputFlag) {
    if (autocls)
      cmdClearScreen();
    // printf("%s", inbuf);
    cmdDisassemble();
  }
}

void cmdFinish(void)
{
  traceframe = 0;

  reg_data reg = get_regs();

  int cur_sp = reg.sp;
  bool function_returning = false;

  // outputFlag = false;
  while (!function_returning) {
    reg = get_regs();
    mem_data mem = get_mem(reg.pc, false);

    if ((strcmp(instruction_lut[mem.b[0]], "RTS") == 0 || strcmp(instruction_lut[mem.b[0]], "RTI") == 0) && reg.sp == cur_sp)
      function_returning = true;

    cmdClearScreen();
    cmdNext();

    if (ctrlcflag)
      break;
  }
  // outputFlag = true;
  cmdDisassemble();
}

// check symbol-map for value. If not found there, just return
// the hex-value of the string
int get_sym_value(char *token)
{
  int addr = 0;

  // if token starts with ":", then let's assume it is
  // for a line number of the current file
  if (token[0] == ':') {
    int lineno = 0;
    sscanf(&token[1], "%d", &lineno);
    type_fileloc *fl = find_lineno_in_list(lineno);
    if (!cur_file_loc) {
      printf("- Current source file unknown\n");
      return -1;
    }
    if (!fl) {
      printf("- Could not locate code at \"%s:%d\"\n", cur_file_loc->file, lineno);
      return -1;
    }
    addr = fl->addr;
    return addr;
  }

  // otherwise assume it is a symbol (which will fall back to a raw address anyway)
  type_symmap_entry *sme = find_in_symmap(token);
  if (sme != NULL) {
    return sme->addr;
  }
  else {
    sscanf(token, "%X", &addr);
    return addr;
  }
}

void print_byte(char *token)
{
  int addr = get_sym_value(token);

  mem_data mem = get_mem(addr, false);

  printf(" %s: %02X\n", token, mem.b[0]);
}

void cmdPrintByte(void)
{
  char *token = strtok(NULL, " ");

  if (token != NULL) {
    print_byte(token);
  }
}

void print_word(char *token)
{
  int addr = get_sym_value(token);

  mem_data mem = get_mem(addr, false);

  printf(" %s: %02X%02X\n", token, mem.b[1], mem.b[0]);
}

void cmdPrintWord(void)
{
  char *token = strtok(NULL, " ");

  if (token != NULL) {
    print_word(token);
  }
}

void print_dword(char *token)
{
  int addr = get_sym_value(token);

  mem_data mem = get_mem(addr, false);

  printf(" %s: %02X%02X%02X%02X\n", token, mem.b[3], mem.b[2], mem.b[1], mem.b[0]);
}

void cmdPrintDWord(void)
{
  char *token = strtok(NULL, " ");

  if (token != NULL) {
    print_dword(token);
  }
}

void print_string(char *token)
{
  int addr = get_sym_value(token);
  static char string[2048] = { 0 };

  int cnt = 0;
  string[0] = '\0';

  while (1) {
    mem_data mem = get_mem(addr + cnt, false);

    for (int k = 0; k < 16; k++) {
      // If string is over 100 chars, let's truncate it, for safety...
      if (cnt > 100) {
        string[cnt++] = '.';
        string[cnt++] = '.';
        string[cnt++] = '.';
        mem.b[k] = 0;
      }

      string[cnt] = mem.b[k];

      if (mem.b[k] == 0) {
        printf(" %s: %s\n", token, string);
        return;
      }
      cnt++;
    }
  }
}

void print_dump(type_watch_entry *watch)
{
  int count = 16; // default count

  if (watch->param1)
    sscanf(watch->param1, "%X", &count);

  printf(" %s:\n", watch->name);

  int addr = get_sym_value(watch->name);

  dump(addr, count);
}

void print_mdump(type_watch_entry *watch)
{
  int count = 16; // default count

  if (watch->param1)
    sscanf(watch->param1, "%X", &count);

  printf(" %s:\n", watch->name);

  int addr = get_sym_value(watch->name);

  mdump(addr, count);
}

void cmdPrintString(void)
{
  char *token = strtok(NULL, " ");

  if (token != NULL) {
    print_string(token);
  }
}

void cmdClearScreen(void)
{
  printf("%s%s", KCLEAR, KPOS0_0);
}

void cmdAutoClearScreen(void)
{
  char *token = strtok(NULL, " ");

  // if no parameter, then just toggle it
  if (token == NULL)
    autocls = !autocls;
  else if (strcmp(token, "1") == 0)
    autocls = true;
  else if (strcmp(token, "0") == 0)
    autocls = false;

  printf(" - autocls is turned %s.\n", autocls ? "on" : "off");
}

void cmdSetBreakpoint(void)
{
  char *token = strtok(NULL, " ");
  char str[100];

  if (token != NULL) {
    int addr = get_sym_value(token);

    if (addr == -1)
      return;

    printf("- Setting hardware breakpoint to $%04X\n", addr);

    sprintf(str, "b%04X\n", addr);
    serialWrite(str);
    serialRead(inbuf, BUFSIZE);
  }
}

bool inHypervisorMode(void)
{
  // get current register values
  reg_data reg = get_regs();

  if (reg.maph == 0x3F00)
    return true;

  return false;
}

void clearSoftBreak(void)
{
  char str[100];

  // stop the cpu
  serialWrite("t1\n");
  usleep(10000);
  serialRead(inbuf, BUFSIZE);

  bool in_hv = inHypervisorMode();
  if (in_hv)
    softbrkaddr |= 0xfff0000;

  // inject JMP command to loop over itself
  sprintf(str, "s%04X %02X %02X %02X\n", softbrkaddr, softbrkmem[0], softbrkmem[1], softbrkmem[2]);
  serialWrite(str);
  serialRead(inbuf, BUFSIZE);
  softbrkaddr = 0;
}

void setSoftBreakpoint(int addr)
{
  char str[100];

  softbrkaddr = addr;

  int cpu_stopped = isCpuStopped();

  if (!cpu_stopped) {
    serialWrite("t1\n");
    usleep(10000);
    serialRead(inbuf, BUFSIZE);
  }

  bool in_hv = inHypervisorMode();
  if (in_hv)
    addr |= 0xfff0000;

  // user manually enforcing a hypervisor breakpoint? (or any other >64kb for that matter)
  if (addr > 0xffff)
    in_hv = true;

  mem_data mem = get_mem(addr, in_hv);
  softbrkmem[0] = mem.b[0];
  softbrkmem[1] = mem.b[1];
  softbrkmem[2] = mem.b[2];

  // inject JMP command to loop over itself
  sprintf(str, "s%04X %02X %02X %02X\n", addr, 0x4C, addr & 0xff, (addr >> 8) & 0xff);
  serialWrite(str);
  serialRead(inbuf, BUFSIZE);

  if (!cpu_stopped) {
    serialWrite("t0\n");
    usleep(100000);
    serialRead(inbuf, BUFSIZE);
  }

  // sprintf(str, "b%04X\n", addr);
  // serialWrite(str);
  // serialRead(inbuf, BUFSIZE);
}

void cmdSetSoftwareBreakpoint(void)
{
  char *token = strtok(NULL, " ");

  if (token != NULL) {
    int addr = get_sym_value(token);

    if (addr == -1)
      return;

    printf("- Setting software breakpoint to $%04X\n", addr);

    setSoftBreakpoint(addr);
  }
}

void cmd_watch(type_watch type)
{
  char *token = strtok(NULL, " ");

  if (token != NULL) {
    if (find_in_watchlist(type, token)) {
      printf("watch already exists!\n");
      return;
    }

    type_watch_entry we;
    we.type = type;
    we.name = token;
    we.param1 = NULL;

    token = strtok(NULL, " ");
    if (token != NULL) {
      if (type == TYPE_DUMP || type == TYPE_MDUMP) {
        we.param1 = token;
      }
      else {
        // grab a new symbol name?
        type_symmap_entry sme;
        sme.addr = get_sym_value(we.name);
        sme.sval = we.name;
        sme.symbol = token;
        add_to_symmap(sme);
        we.name = token;
      }
    }

    add_to_watchlist(we);

    if (we.param1 != NULL)
      printf("watch added! (%s : %s %s)\n", type_names[type], we.name, we.param1);
    else
      printf("watch added! (%s : %s)\n", type_names[type], we.name);
  }
}

void cmdWatchByte(void)
{
  cmd_watch(TYPE_BYTE);
}

void cmdWatchWord(void)
{
  cmd_watch(TYPE_WORD);
}

void cmdWatchDWord(void)
{
  cmd_watch(TYPE_DWORD);
}

void cmdWatchString(void)
{
  cmd_watch(TYPE_STRING);
}

void cmdWatchDump(void)
{
  cmd_watch(TYPE_DUMP);
}

void cmdWatchMDump(void)
{
  cmd_watch(TYPE_MDUMP);
}

void cmdWatches(void)
{
  type_watch_entry *iter = lstWatches;
  int cnt = 0;

  printf("---------------------------------------\n");

  while (iter != NULL) {
    cnt++;

    printf("#%d: %s ", cnt, type_names[iter->type]);

    switch (iter->type) {
    case TYPE_BYTE:
      print_byte(iter->name);
      break;
    case TYPE_WORD:
      print_word(iter->name);
      break;
    case TYPE_DWORD:
      print_dword(iter->name);
      break;
    case TYPE_STRING:
      print_string(iter->name);
      break;
    case TYPE_DUMP:
      print_dump(iter);
      break;
    case TYPE_MDUMP:
      print_mdump(iter);
      break;
    }

    iter = iter->next;
  }

  if (cnt == 0)
    printf("no watches in list\n");
  printf("---------------------------------------\n");
}

void cmdDeleteWatch(void)
{
  char *token = strtok(NULL, " ");

  if (token != NULL) {
    // user wants to delete all watches?
    if (strcmp(token, "all") == 0) {
      // TODO: add a confirm yes/no prompt...
      outputFlag = false;
      while (delete_from_watchlist(1))
        ;
      outputFlag = true;
      printf("deleted all watches!\n");
    }
    else {
      int wnum;
      int n = sscanf(token, "%d", &wnum);

      if (n == 1) {
        delete_from_watchlist(wnum);
      }
    }
  }
}

void cmdAutoWatch(void)
{
  char *token = strtok(NULL, " ");

  // if no parameter, then just toggle it
  if (token == NULL)
    autowatch = !autowatch;
  else if (strcmp(token, "1") == 0)
    autowatch = true;
  else if (strcmp(token, "0") == 0)
    autowatch = false;

  printf(" - autowatch is turned %s.\n", autowatch ? "on" : "off");
}

void cmdPetscii(void)
{
  char *token = strtok(NULL, " ");

  // if no parameter, then just toggle it
  if (token == NULL)
    petscii = !petscii;
  else if (strcmp(token, "1") == 0)
    petscii = true;
  else if (strcmp(token, "0") == 0)
    petscii = false;

  printf(" - petscii is turned %s.\n", petscii ? "on" : "off");
}

void cmdFastMode(void)
{
#ifdef __CYGWIN__
  printf("Command not available under Cygwin (only capable of 2,000,000bps)\n");
#else
  char *token = strtok(NULL, " ");

  // if no parameter, then just toggle it
  if (token == NULL)
    fastmode = !fastmode;
  else if (strcmp(token, "1") == 0)
    fastmode = true;
  else if (strcmp(token, "0") == 0)
    fastmode = false;

  serialBaud(fastmode);

  printf(" - fastmode is turned %s.\n", fastmode ? "on" : "off");
#endif
}

void cmdScope(void)
{
  char *token = strtok(NULL, " ");

  if (token == NULL)
    return;
  sscanf(token, "%d", &dis_scope);

  if (autocls)
    cmdClearScreen();
  cmdDisassemble();
}

void cmdOffs(void)
{
  char *token = strtok(NULL, " ");

  if (token == NULL)
    return;
  sscanf(token, "%d", &dis_offs);

  if (autocls)
    cmdClearScreen();
  cmdDisassemble();
}

int parseBinaryString(char *str)
{
  int val = 0;
  int weight = 1;
  for (int k = strlen(str) - 1; k >= 0; k--) {
    if (str[k] == '1')
      val += weight;
    weight <<= 1;
  }
  return val;
}

char *toBinaryString(int val)
{
  static char str[64];

  int maxbit = (1 << 31);
  if (val / 65536 == 0)
    maxbit = (1 << 15);
  if (val / 256 == 0)
    maxbit = (1 << 7);

  int cnt = 0;
  int bitcnt = 0;
  for (int k = maxbit; k > 0; k >>= 1) {
    if (val & k)
      str[cnt] = '1';
    else
      str[cnt] = '0';

    cnt++;
    bitcnt++;
    if ((bitcnt % 4) == 0) {
      str[cnt] = ' ';
      cnt++;
    }
  }
  str[cnt] = '\0';
  return str;
}

void cmdPrintValue(void)
{
  char *strVal = strtok(NULL, " ");

  if (strVal == NULL) {
    printf("Missing <value> parameter!\n");
    return;
  }

  int val = 0;
  if (strVal[0] == '$')
    val = get_sym_value(strVal + 1);
  else if (strVal[0] == '%')
    val = parseBinaryString(strVal + 1);
  else if (isdigit(strVal[0]))
    sscanf(strVal, "%d", &val);
  else
    val = get_sym_value(strVal);

  char charval[64] = "";
  if (val >= 32 && val < 256)
    sprintf(charval, "(char='%c')", val);

  printf("  $%04X  /  %d  /  %%%s %s\n", val, val, toBinaryString(val), charval);
}

int isCpuStopped(void)
{
  int cnt = 0;
  int cur_pc = -1;
  int same_cnt = 0;
  while (cnt < 10) {
    // get current register values
    reg_data reg = get_regs();

    usleep(10000);

    if (reg.pc == cur_pc) {
      same_cnt++;
      if (same_cnt == 5)
        return 1;
    }
    else {
      same_cnt = 0;
      cur_pc = reg.pc;
    }
    cnt++;
  }

  return 0;
}

int doOneShotAssembly(char *strCommand)
{
  int numbytes;
  char str[128];

  int cpu_stopped = isCpuStopped();

  if (!cpu_stopped) {
    printf("Stopping CPU first...\n");
    serialWrite("t1\n");
    usleep(10000);
    serialRead(inbuf, BUFSIZE);
  }

  reg_data reg = get_regs();
  int tmppc = 0xf0;

  usleep(10000);

  // get memory at 0xf0 (usually always 'writable' memory in zero-page)
  // serialFlush();
  mem_data mem = get_mem(tmppc, false);
  serialFlush();
  // serialRead(inbuf, BUFSIZE);

  usleep(10000);

  numbytes = oneShotAssembly(&tmppc, strCommand);
  serialFlush();

  usleep(10000);

  if (numbytes > 0) {
    // reset pc
    serialWrite("gf0\n");
    serialRead(inbuf, BUFSIZE);

    usleep(100000);

    // just send an enter command to do single step
    serialWrite("\n");

    usleep(100000);

    // reset pc
    sprintf(str, "g%X\n", reg.pc);
    serialWrite(str);
    serialRead(inbuf, BUFSIZE);

    // usleep(100000);

    // reset byte-values at this point
    for (int k = 0; k < numbytes; k++) {
      sprintf(str, "s777%04X %02X\n", 0xf0 + k, mem.b[k]);
      serialWrite(str);
      usleep(10000);
      serialRead(inbuf, BUFSIZE);
    }
  }

  serialFlush();

  return numbytes;
}

void cmdSymbolValue(void)
{
  char *token = strtok(NULL, " ");

  if (token != NULL) {
    type_symmap_entry *sme = find_in_symmap(token);

    if (sme != NULL)
      printf("%s : %s\n", sme->sval, sme->symbol);
  }
}

void cmdSave(void)
{
  char *strBinFile = strtok(NULL, " ");

  if (!strBinFile) {
    printf("Missing <binfile> parameter!\n");
    return;
  }

  char *strAddr = strtok(NULL, " ");
  if (!strAddr) {
    printf("Missing <addr> parameter!\n");
    return;
  }

  char *strCount = strtok(NULL, " ");
  if (!strCount) {
    printf("Missing <count> parameter!\n");
    return;
  }

  int addr = get_sym_value(strAddr);
  int count;
  sscanf(strCount, "%X", &count);

  int cnt = 0;
  FILE *fsave = fopen(strBinFile, "wb");
  while (cnt < count) {
    // get memory at current pc
    mem_data *multimem = get_mem28array(addr + cnt);

    for (int line = 0; line < 16; line++) {
      mem_data *mem = &multimem[line];

      for (int k = 0; k < 16; k++) {
        fputc(mem->b[k], fsave);

        cnt++;

        if (cnt >= count)
          break;
      }

      printf("0x%X bytes saved...\r", cnt);
      if (cnt >= count)
        break;
    }

    if (ctrlcflag)
      break;
  }

  printf("\n0x%X bytes saved to \"%s\"\n", cnt, strBinFile);
  fclose(fsave);
}

void cmdLoad(void)
{
  char *strBinFile = strtok(NULL, " ");

  if (!strBinFile) {
    printf("Missing <binfile> parameter!\n");
    return;
  }

  char *strAddr = strtok(NULL, " ");
  if (!strAddr) {
    printf("Missing <addr> parameter!\n");
    return;
  }

  int addr = get_sym_value(strAddr);

  FILE *fload = fopen(strBinFile, "rb");
  if (fload) {
    fseek(fload, 0, SEEK_END);
    int fsize = ftell(fload);
    rewind(fload);
    char *buffer = (char *)malloc(fsize * sizeof(char));
    if (buffer) {
      fread(buffer, fsize, 1, fload);

      int i = 0;
      while (i < fsize) {
        int outSize = fsize - i;
        if (outSize > 16) {
          outSize = 16;
        }

        put_mem28array(addr + i, (unsigned char *)(buffer + i), outSize);
        i += outSize;
      }

      free(buffer);
    }
    fclose(fload);
  }
  else {
    printf("Error opening the file '%s'!\n", strBinFile);
  }
}

void cmdBackTrace(void)
{
  char str[128] = { 0 };

  // get current register values
  reg_data reg = get_regs();

  disassemble_addr_into_string(str, reg.pc, false);
  if (traceframe == 0)
    printf(KINV "#0: %s\n" KNRM, str);
  else
    printf("#0: %s\n", str);

  // get memory at current pc
  int *addresses = get_backtrace_addresses();

  for (int k = 0; k < 8; k++) {
    disassemble_addr_into_string(str, addresses[k], false);
    if (traceframe - 1 == k)
      printf(KINV "#%d: %s\n" KNRM, k + 1, str);
    else
      printf("#%d: %s\n", k + 1, str);
  }
}

void cmdUpFrame(void)
{
  if (traceframe == 0) {
    printf("Already at highest frame! (frame#0)\n");
    return;
  }

  traceframe--;

  if (autocls)
    cmdClearScreen();
  cmdDisassemble();
}

void cmdDownFrame(void)
{
  if (traceframe == 8) {
    printf("Already at lowest frame! (frame#8)\n");
    return;
  }

  traceframe++;

  if (autocls)
    cmdClearScreen();
  cmdDisassemble();
}

void search_range(int addr, int total, unsigned char *bytes, int length)
{
  int cnt = 0;
  bool found_start = false;
  int found_count = 0;
  int start_loc = 0;
  int results_cnt = 0;

  printf("Searching for: ");
  for (int k = 0; k < length; k++) {
    printf("%02X ", bytes[k]);
  }
  printf("\n");

  while (cnt < total) {
    // get memory at current pc
    mem_data *multimem = get_mem28array(addr + cnt);

    for (int m = 0; m < 32; m++) {
      mem_data mem = multimem[m];

      for (int k = 0; k < 16; k++) {
        if (!found_start) {
          if (mem.b[k] == bytes[0]) {
            found_start = true;
            start_loc = mem.addr + k;
            found_count++;
          }
        }
        else // matched till the end?
        {
          if (mem.b[k] == bytes[found_count]) {
            found_count++;
            // we found a complete match?
            if (found_count == length) {
              printf("%07X\n", start_loc);
              found_start = false;
              found_count = 0;
              start_loc = 0;
              results_cnt++;
            }
          }
          else {
            found_start = false;
            start_loc = 0;
            found_count = 0;
          }
        }
      } // end for k

      cnt += 16;

      if (cnt > total)
        break;
    } // end for m

    if (ctrlcflag)
      break;
  }

  if (results_cnt == 0) {
    printf("None found...\n");
  }
}

void cmdSearch(void)
{
  char *strAddr = strtok(NULL, " ");
  char bytevals[64] = { 0 };
  int len = 0;

  if (strAddr == NULL) {
    printf("Missing <addr28> parameter!\n");
    return;
  }

  int addr = get_sym_value(strAddr);

  int total = 16;
  char *strTotal = strtok(NULL, " ");

  if (strTotal != NULL) {
    sscanf(strTotal, "%X", &total);
  }

  char *strValues = strtok(NULL, "\0");

  // provided a string?
  if (strValues[0] == '\"') {
    search_range(addr, total, (unsigned char *)&strValues[1], strlen(strValues) - 2);
  }
  else {

    char *sval = strtok(strValues, " ");
    do {
      int ival;
      sscanf(sval, "%X", &ival);
      bytevals[len] = ival;
      len++;
    } while ((sval = strtok(NULL, " ")) != NULL);

    search_range(addr, total, (unsigned char *)bytevals, len);
  }
}

void cmdScreenshot(void)
{
  int orig_fcntl = fcntl(fd, F_GETFL, NULL);
  fcntl(fd, F_SETFL, orig_fcntl | O_NONBLOCK);
  get_video_state();
  do_screen_shot_ascii();
  fcntl(fd, F_SETFL, orig_fcntl);
}

/** TODO: refactor do_type_text out of m65.c **/
/** extern **/ int type_text_cr = 0;
void cmdType(void)
{
  char *tok = strtok(NULL, "\0");
  int orig_fcntl = fcntl(fd, F_GETFL, NULL);
  fcntl(fd, F_SETFL, orig_fcntl | O_NONBLOCK);

  if (tok != NULL) {
    type_text_cr = 1;
    /** TODO: do_type_text(tok); */
  }
  else {
    /** TODO: do_type_text("-"); */
  }
  fcntl(fd, F_SETFL, orig_fcntl);
}

void cmdFtp(void)
{
  int orig_fcntl = fcntl(fd, F_GETFL, NULL);
  fcntl(fd, F_SETFL, orig_fcntl | O_NONBLOCK);
  do_ftp(pathBitstream);
  fcntl(fd, F_SETFL, orig_fcntl);
  serialClose();
  serialOpen(devSerial);
}

int cmdGetCmdCount(void)
{
  return sizeof(command_details) / sizeof(type_command_details) - 1;
}

char *cmdGetCmdName(int idx)
{
  return command_details[idx].name;
}
