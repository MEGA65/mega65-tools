/*

  Upload one or more files to SD card on MEGA65

  Copyright (C) 2018 Paul Gardner-Stephen
  Portions Copyright (C) 2013 Serval Project Inc.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>

#include "m65common.h"
#include "filehost.h"
#include "diskman.h"
#include "dirtymock.h"

#define BOOL int
#define TRUE 1
#define FALSE 0

#define BYTES_PER_MB 1048576

#define SECTOR_CACHE_SIZE 4096
int sector_cache_count = 0;
unsigned char sector_cache[SECTOR_CACHE_SIZE][512];
unsigned int sector_cache_sectors[SECTOR_CACHE_SIZE];

struct m65dirent {
  long d_ino;                /* start cluster */
  long d_filelen;            /* length of file */
  unsigned short d_reclen;   /* Always sizeof struct dirent. */
  unsigned short d_namlen;   /* Length of name in d_name. */
  unsigned char d_attr;      /* FAT file attributes */
  unsigned d_type;           /* Object type (digested attributes) */
  char d_name[FILENAME_MAX]; /* File name. */
};

#define RET_FAIL -1
#define RET_NOT_FOUND 0
#define RET_FOUND 1

char current_dir[1024] = "/";

#define SLOW_FACTOR 1
#define SLOW_FACTOR2 1

#ifdef WINDOWS
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <readline/readline.h>
#include <readline/history.h>
#endif

int open_file_system(void);
int download_slot(int sllot, char* dest_name);
int download_file(char* dest_name, char* local_name, int showClusters);
void show_clustermap(void);
void show_cluster(void);
void dump_sectors(void);
void restore_sectors(void);
void show_secinfo(void);
void show_mbrinfo(void);
void show_vbrinfo(void);
void poke_sector(void);
void perform_filehost_read(char* searchterm);
void perform_filehost_get(int num, char* destname);
void perform_filehost_flash(int fhnum, int slotnum);
void perform_flash(char* fname, int slotnum);
void list_all_roms(void);
int show_directory(char* path);
void show_local_directory(char* searchpattern);
void change_local_dir(char* path);
void show_local_pwd(void);
int delete_file(char* name);
int rename_file(char* name, char* dest_name);
int upload_file(char* name, char* dest_name);
int sdhc_check(void);
void request_remotesd_version(void);
void request_quit(void);
void mount_file(char* filename);
#define CACHE_NO 0
#define CACHE_YES 1
int read_sector(const unsigned int sector_number, unsigned char* buffer, int useCache, int readAhead);
int write_sector(const unsigned int sector_number, unsigned char* buffer);
int load_helper(void);
int stuff_keybuffer(char* s);
int create_dir(char*);
int fat_opendir(char*);
int fat_readdir(struct m65dirent*);
BOOL safe_open_dir(void);
BOOL find_file_in_curdir(char* filename, struct m65dirent* de);
BOOL create_directory_entry_for_file(char* filename);
unsigned int calc_first_cluster_of_file(void);
BOOL is_d81_file(char* filename);
void wrap_upload(char* fname);
char* get_file_extension(char* filename);

// Helper routine for faster sector writing
extern unsigned int helperroutine_len;
extern unsigned char helperroutine[];
int helper_installed = 0;
int job_done;
int sectors_written;
int job_status_fresh = 0;

long long start_usec = 0;

int osk_enable = 0;

int not_already_loaded = 1;

int halt = 0;

int first_load = 1;
int first_go64 = 1;

unsigned char viciv_regs[0x100];
int mode_report = 0;

char serial_port[1024] = "/dev/ttyUSB1";
char* bitstream = NULL;
char* username = NULL;
char* password = NULL;

unsigned char* sd_read_buffer = NULL;
int sd_read_offset = 0;

int file_system_found = 0;
unsigned int partition_start = 0;
unsigned int partition_size = 0;
unsigned char sectors_per_cluster = 0;
unsigned int sectors_per_fat = 0;
unsigned int data_sectors = 0;
unsigned int first_cluster = 0;
unsigned int fsinfo_sector = 0;
unsigned int reserved_sectors = 0;
unsigned int fat1_sector = 0, fat2_sector = 0, first_cluster_sector;

unsigned int syspart_start = 0;
unsigned int syspart_size = 0;
unsigned int syspart_freeze_area = 0;
unsigned int syspart_freeze_program_size = 0;
unsigned int syspart_slot_size = 0;
unsigned int syspart_slot_count = 0;
unsigned int syspart_slotdir_sectors = 0;
unsigned int syspart_service_area = 0;
unsigned int syspart_service_area_size = 0;
unsigned int syspart_service_slot_size = 0;
unsigned int syspart_service_slot_count = 0;
unsigned int syspart_service_slotdir_sectors = 0;

unsigned char mbr[512];
unsigned char fat_mbr[512];
unsigned char syspart_sector0[512];
unsigned char syspart_configsector[512];

int dirent_raw = 0;
int clustermap_start = 0;
int clustermap_count = 0;
int cluster_num = 0;
char secdump_file[256] = { 0 };
int secdump_start = 0;
int secdump_count = 0;
char secrestore_file[256] = { 0 };
int secrestore_start = 0;
int poke_secnum = 0;
int poke_offset = 0;
int poke_value = 0;
int fhnum = 0;
int slotnum = 0;

#define M65DT_REG 1
#define M65DT_DIR 2
#define M65DT_UNKNOWN 4
#define M65DT_FREESLOT 0xff

int sd_status_fresh = 0;
unsigned char sd_status[16];

extern const char* version_string;

void usage(void)
{
  fprintf(stderr, "MEGA65 cross-development tool for FTP-like access to MEGA65 SD card via serial monitor interface\n");
  fprintf(stderr, "version: %s\n\n", version_string);
  fprintf(stderr, "usage: mega65_ftp [-l <serial port>] [-s <230400|2000000|4000000>]  [-b bitstream] [[-c command] ...]\n");
  fprintf(stderr, "  -l - Name of serial port to use, e.g., /dev/ttyUSB1\n");
  fprintf(stderr, "  -s - Speed of serial port in bits per second. This must match what your bitstream uses.\n");
  fprintf(stderr, "       (Almost always 2000000 is the correct answer).\n");
  fprintf(stderr, "  -b - Name of bitstream file to load.\n");
  fprintf(stderr, "\n");
  exit(-3);
}

#define READ_SECTOR_BUFFER_ADDRESS 0xFFD6e00
#define WRITE_SECTOR_BUFFER_ADDRESS 0xFFD6e00

int queued_command_count = 0;
#define MAX_QUEUED_COMMANDS 64
char* queued_commands[MAX_QUEUED_COMMANDS];

int queue_command(char* c)
{
  if (queued_command_count < MAX_QUEUED_COMMANDS)
    queued_commands[queued_command_count++] = c;
  else {
    fprintf(stderr, "ERROR: Too many commands queued via -c\n");
  }
  return 0;
}

unsigned char show_buf[512];
int show_sector(unsigned int sector_num)
{
  if (read_sector(sector_num, show_buf, CACHE_YES, 0)) {
    printf("ERROR: Could not read sector %d ($%x)\n", sector_num, sector_num);
    return -1;
  }
  dump_bytes(0, "Sector contents", show_buf, 512);
  return 0;
}

int parse_string_param(char** src, char* dest)
{
  int cnt = 0;
  char* srcptr = *src;
  char endchar = ' ';
  if (*srcptr == '\"') {
    endchar = '\"';
    srcptr++;
    *src = srcptr;
  }

  while (*srcptr != endchar && *srcptr != '\0') {
    if (*srcptr == '\0' && endchar == '\"')
      return RET_NOT_FOUND;
    srcptr++;
    cnt++;
  }

  if (cnt == 0)
    return RET_NOT_FOUND;

  strncpy(dest, *src, cnt);
  dest[cnt] = '\0';

  if (endchar == '\"')
    srcptr++;

  *src = srcptr;
  return RET_FOUND;
}

int parse_int_param(char** src, int* dest)
{
  int cnt = 0;
  char str[128];
  char* srcptr = *src;

  while (*srcptr != ' ' && *srcptr != '\0') {
    if (*srcptr < '0' && *srcptr > '9')
      return RET_NOT_FOUND;
    srcptr++;
    cnt++;
  }

  if (cnt == 0)
    return RET_NOT_FOUND;

  strncpy(str, *src, cnt);
  str[cnt] = '\0';
  *dest = atoi(str);

  *src = srcptr;
  return RET_FOUND;
}

int skip_whitespace(char** orig)
{
  char* ptrstr = *orig;

  // skip any spaces in str
  while (*ptrstr == ' ' || *ptrstr == '\t') {
    if (*ptrstr == '\0')
      return 0;
    ptrstr++;
  }

  *orig = ptrstr;
  return 1;
}

int parse_format_specifier(char** pptrformat, char** pptrstr, va_list* pargs, int* pcnt)
{
  int found;
  if (**pptrformat == '%') {
    (*pptrformat)++;
    if (**pptrformat == 's') {
      if (!skip_whitespace(pptrstr))
        return RET_FAIL;
      found = parse_string_param(pptrstr, va_arg(*pargs, char*));
      if (found)
        (*pcnt)++;
      else
        return RET_FAIL;
    }
    else if (**pptrformat == 'd') {
      if (!skip_whitespace(pptrstr))
        return 0;
      found = parse_int_param(pptrstr, va_arg(*pargs, int*));
      if (found)
        (*pcnt)++;
      else
        return RET_FAIL;
    }
    else
      return RET_FAIL;

    return RET_FOUND;
  }

  return RET_NOT_FOUND;
}

int parse_non_whitespace(char** pptrformat, char** pptrstr)
{
  if (**pptrformat != ' ') {
    if (!skip_whitespace(pptrstr))
      return 0;

    if (**pptrformat != **pptrstr)
      return 0;

    (*pptrstr)++;
  }
  return 1;
}

int parse_command(const char* str, const char* format, ...)
{
  va_list args;
  va_start(args, format);

  // scan through str looking for '%' tokens
  char* ptrstr = (char*)str;
  char* ptrformat = (char*)format;
  int cnt = 0;

  while (*ptrformat != '\0') {
    int ret = parse_format_specifier(&ptrformat, &ptrstr, &args, &cnt);
    if (ret == RET_FAIL)
      return cnt;
    else if (ret == RET_NOT_FOUND) {
      if (!parse_non_whitespace(&ptrformat, &ptrstr))
        return cnt;
    }

    ptrformat++;
  }

  va_end(args);
  return cnt;
}

int execute_command(char* cmd)
{
  unsigned int sector_num;

  if (strlen(cmd) > 1000) {
    fprintf(stderr, "ERROR: Command too long\n");
    return -1;
  }
  int slot = 0;
  char src[1024];
  char dst[1024];
  if ((!strcmp(cmd, "exit")) || (!strcmp(cmd, "quit"))) {
    printf("Reseting MEGA65 and exiting.\n");

    request_quit();
    if (xemu_flag)
      usleep(30000);
    exit(0);
  }

  if (parse_command(cmd, "getslot %d %s", &slot, dst) == 2) {
    download_slot(slot, dst);
  }
  else if (parse_command(cmd, "get %s %s", src, dst) == 2) {
    download_file(src, dst, 0);
  }
  else if (parse_command(cmd, "put %s %s", src, dst) == 2) {
    upload_file(src, dst);
  }
  else if (parse_command(cmd, "dput %s", src) == 1) {
    wrap_upload(src);
  }
  else if (parse_command(cmd, "del %s", src) == 1) {
    delete_file(src);
  }
  else if (parse_command(cmd, "rename %s %s", src, dst) == 2) {
    rename_file(src, dst);
  }
  else if (parse_command(cmd, "sector %d", &sector_num) == 1) {
    // Clear cache to force re-reading
    sector_cache_count = 0;
    show_sector(sector_num);
  }
  else if (parse_command(cmd, "sector $%x", &sector_num) == 1) {
    show_sector(sector_num);
  }
  else if (sscanf(cmd, "dirent_raw %d", &dirent_raw) == 1) {
    printf("dirent_raw = %d\n", dirent_raw);
  }
  else if (parse_command(cmd, "dir %s", src) == 1) {
    show_directory(src);
  }
  else if (!strcmp(cmd, "dir")) {
    show_directory(current_dir);
  }
  else if (parse_command(cmd, "ldir %s", src) == 1) {
    show_local_directory(src);
  }
  else if (!strcmp(cmd, "ldir")) {
    show_local_directory(NULL);
  }
  else if (parse_command(cmd, "put %s", src) == 1) {
    char* dest = src;
    // Set destination name to last element of source name, if no destination name provided
    for (int i = 0; src[i]; i++)
      if (src[i] == '/')
        dest = &src[i + 1];
    upload_file(src, dest);
  }
  else if (parse_command(cmd, "mkdir %s", src) == 1) {
    create_dir(src);
  }
  else if (parse_command(cmd, "lcd %s", src) == 1) {
    change_local_dir(src);
  }
  else if (!strcmp(cmd, "lpwd")) {
    show_local_pwd();
  }
  else if ((parse_command(cmd, "chdir %s", src) == 1) || (parse_command(cmd, "cd %s", src) == 1)) {
    if (src[0] == '/') {
      // absolute path
      if (fat_opendir(src)) {
        fprintf(stderr, "ERROR: Could not open directory '%s'\n", src);
      }
      strcpy(current_dir, src);
    }
    else {
      // relative path
      char temp_path[1024], src_copy[1024];
      {
        // Apply each path segment, handling . and .. appropriately
        snprintf(temp_path, 1024, "%s", current_dir);
        strcpy(src_copy, src);
        while (src_copy[0]) {

          while (src_copy[0] == '/')
            strcpy(src_copy, &src_copy[1]);

          int seg_len = 0;
          char seg[1024];
          for (seg_len = 0; src_copy[seg_len]; seg_len++) {
            if (src_copy[seg_len] == '/')
              break;
            else
              seg[seg_len] = src_copy[seg_len];
          }
          seg[seg_len] = 0;
          if (!strcmp(seg, ".")) {
            // Ignore "." elements
          }
          else if (!strcmp(seg, "..")) {
            // Remove an element for each ".."
            int len = 0;
            for (int i = 0; temp_path[i]; i++)
              if (temp_path[i] == '/')
                len = i;
            if (len < 1)
              len = 1;
            temp_path[len] = 0;
          }
          else {
            // Append element otherwise
            if (strcmp(temp_path, "/"))
              snprintf(&temp_path[strlen(temp_path)], 1024 - strlen(temp_path), "/%s", seg);
            else
              snprintf(&temp_path[strlen(temp_path)], 1024 - strlen(temp_path), "%s", seg);
          }
          strcpy(src_copy, &src_copy[seg_len]);
        }
        // Stay in current directory
      }

      if (fat_opendir(temp_path)) {
        fprintf(stderr, "ERROR: Could not open directory '%s'\n", temp_path);
      }
      else
        strcpy(current_dir, temp_path);
    }
  }
  else if (parse_command(cmd, "get %s", src) == 1) {
    download_file(src, src, 0);
  }
  else if (parse_command(cmd, "clusters %s", src) == 1) {
    download_file(src, src, 1);
  }
  else if (parse_command(cmd, "mount %s", src) == 1) {
    mount_file(src);
  }
  else if (sscanf(cmd, "clustermap %d %d", &clustermap_start, &clustermap_count) == 2) {
    show_clustermap();
  }
  else if (sscanf(cmd, "clustermap %d", &clustermap_start) == 1) {
    clustermap_count = 1;
    show_clustermap();
  }
  else if (sscanf(cmd, "cluster %d", &cluster_num) == 1) {
    show_cluster();
  }
  else if (sscanf(cmd, "secdump %s %d %d", secdump_file, &secdump_start, &secdump_count) == 3) {
    dump_sectors();
  }
  else if (sscanf(cmd, "secrestore %s %d", secrestore_file, &secrestore_start) == 2) {
    restore_sectors();
  }
  else if (!strcmp(cmd, "secinfo")) {
    show_secinfo();
  }
  else if (!strcmp(cmd, "mbrinfo")) {
    show_mbrinfo();
  }
  else if (!strcmp(cmd, "vbrinfo")) {
    show_vbrinfo();
  }
  else if (sscanf(cmd, "poke %d %d %d", &poke_secnum, &poke_offset, &poke_value) == 3) {
    poke_sector();
  }
  else if (sscanf(cmd, "fhget %d %s", &fhnum, src) == 2) {
    perform_filehost_get(fhnum, src);
  }
  else if (sscanf(cmd, "fhget %d", &fhnum) == 1) {
    perform_filehost_get(fhnum, NULL);
  }
  else if (sscanf(cmd, "fhflash %d %d", &fhnum, &slotnum) == 2) {
    perform_filehost_flash(fhnum, slotnum);
  }
  else if (sscanf(cmd, "fh %s", src) == 1) {
    perform_filehost_read(src);
  }
  else if (!strcmp(cmd, "fh")) {
    perform_filehost_read(NULL);
  }
  else if (sscanf(cmd, "flash %s %d", src, &slotnum) == 2) {
    perform_flash(src, slotnum);
  }
  else if (!strcmp(cmd, "roms")) {
    list_all_roms();
  }
  else if (!strcasecmp(cmd, "help")) {
    printf("MEGA65 File Transfer Program Command Reference:\n\n");

    printf("dir [directory|wildcardpattern] - show contents of current or specified sdcard directory. Can use a wildcard "
           "pattern on current directory.\n");
    printf("ldir [wildcardpattern] - shows the contents of current local directory.\n");
    printf("cd [directory] - change current sdcard working directory.\n");
    printf("lcd [directory] - change current local working directory.\n");
    printf("put <file> [destination name] - upload file to SD card, and optionally rename it destination file.\n");
    printf("get <file> [destination name] - download file from SD card, and optionally rename it destination file.\n");
    printf("dput <file> - upload .prg file wrapped into a .d81 file\n");
    printf("del <file> - delete a file from SD card.\n");
    printf("mkdir <dirname> - create a directory on the SD card.\n");
    printf("cd <dirname> - change directory on the SD card. (aka. 'chdir')\n");
    printf("rename <oldname> <newname> - rename a file on the SD card.\n");
    printf("clusters <file> - show cluster chain of specified file.\n");
    printf("mount <d81file> - Mount the specified .d81 file (which resides on the SD card).\n");
    printf("sector <number|$hex number> - display the contents of the specified sector.\n");
    printf("getslot <slot> <destination name> - download a freeze slot.\n");
    printf("dirent_raw 0|1 - flag to hide/show 32-byte dump of directory entries.\n");
    printf("clustermap <startidx> [<count>] - show cluster-map entries for specified range.\n");
    printf("cluster <num> - dump the entire contents of this cluster.\n");
    printf("secdump <filename> <startsec> <count> - dump the specified sector range to a file.\n");
    printf("secrestore <filename> <startsec> - restore a dumped file back into the specified sector area.\n");
    printf("secinfo - lists the locations of various useful sectors, for easy reference.\n");
    printf("mbrinfo - lists the partitions specified in the MBR (sector 0)\n");
    printf("vbrinfo - lists the VBR details of the main Mega65 partition\n");
    printf("poke <sector> <offset> <val> - poke a value into a sector, at the desired offset.\n");
    printf("fh - retrieve a list of files available on the filehost at files.mega65.org\n");
    printf("fhget <num> [destname] - download a file from the filehost and upload it onto your sd-card\n");
    printf("fhflash <num> <slotnum> - download a cor file from the filehost and flash it to specified slot via vivado\n");
    printf("roms - list all MEGA65x.ROM files on your sd-card along with their version information\n");
    printf("exit - leave this programme.\n");
    printf("quit - leave this programme.\n");
  }
  else {
    fprintf(stderr, "ERROR: Unknown command or invalid syntax. Type help for help.\n");
    return -1;
  }
  return 0;
}

extern int debug_serial;

#ifdef WINDOWS
char* getpass(char* prompt)
{
  printf("%s", prompt);
  static char password[128];
  password[0] = '\0';
  int idx = 0;

  for (;;) {
    int c = _getch();
    switch (c)
    {
      case '\r':
      case '\n':
      case EOF:
        _putch('\n');
        break;
      case 8: // backspace
        if (idx > 0) {
          idx--;
          password[idx] = '\0';
          _putch(8);
        }
        continue;
      default:
        _putch('*'); //mask
        password[idx] = c;
        idx++;
        password[idx] = '\0';
        continue;
    }
    break;
  }
  return password;
}
#endif

int DIRTYMOCK(main)(int argc, char** argv)
{
#ifdef WINDOWS
  // working around mingw64-stdout line buffering issue with advice suggested here:
  // https://stackoverflow.com/questions/13035075/printf-not-printing-on-console
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
#endif
  start_time = time(0);
  start_usec = gettime_us();

  int opt;
  while ((opt = getopt(argc, argv, "b:Ds:l:c:u:p:")) != -1) {
    switch (opt) {
    case 'D':
      debug_serial = 1;
      break;
    case 'l':
      strcpy(serial_port, optarg);
      break;
    case 's':
      serial_speed = atoi(optarg);
      switch (serial_speed) {
      case 1000000:
      case 1500000:
      case 4000000:
      case 230400:
      case 2000000:
        break;
      default:
        usage();
      }
      break;
    case 'b':
      bitstream = strdup(optarg);
      break;
    case 'c':
      queue_command(optarg);
      break;
    case 'u':
      username = strdup(optarg);
      break;
    case 'p':
      password = strdup(optarg);
      break;
    default: /* '?' */
      usage();
    }
  }

  if (argc - optind == 1)
    usage();

  if (username && !password) {
    password = getpass("Password: ");
  }
  errno = 0;

  open_the_serial_port(serial_port);

  rxbuff_detect();

  // Load bitstream if file provided
  if (bitstream) {
    char cmd[1024];
    snprintf(cmd, 1024, "fpgajtag -a %s", bitstream);
    fprintf(stderr, "%s\n", cmd);
    system(cmd);
    fprintf(stderr, "[T+%lldsec] Bitstream loaded\n", (long long)time(0) - start_time);
  }

  // We used to push the interface to 4mbit to speed things up, but that's not needed now.
  // In fact, with the RX buffering allowing us to fix a bunch of other problems that were
  // slowing things down, at 4mbit/sec we are now too fast for the serial monitor to keep up
  // when receiving stuff

  fake_stop_cpu();

  load_helper();

  // Give helper time to get all sorted.
  // Without this delay serial monitor commands to set memory seem to fail :/
  usleep(500000);

  //  monitor_sync();

  sdhc_check();

  if (!file_system_found)
    open_file_system();

  char prompt[1024];

  if (queued_command_count) {
    for (int i = 0; i < queued_command_count; i++)
      execute_command(queued_commands[i]);
    return 0;
  }
  else {
#ifdef WINDOWS
    char cmd[8192];
    int len = 0;
    int c;
    snprintf(prompt, 1024, "MEGA65 SD-Card:%s> ", current_dir);
    printf("%s", prompt);
    fflush(stdout);
    while (1) {
      c = fgetc(stdin);
      if (c == 0x0a || c == 0x0d) {
        execute_command(cmd);
        len = 0;
        cmd[0] = 0;
        snprintf(prompt, 1024, "MEGA65 SD-Card:%s> ", current_dir);
        printf("%s", prompt);
        fflush(stdout);
      }
      else if (c == EOF)
        break;
      else if (len < 8191) {
        cmd[len++] = c;
        cmd[len] = 0;
      }
    }
#else
    char* cmd = NULL;
    using_history();
    int ret = snprintf(prompt, 1024, "MEGA65 SD-Card:%s> ", current_dir);
    if (ret < 0)
      ; // suppressing gcc-8 -Wformat-truncation warning like this for now...

    while ((cmd = readline(prompt)) != NULL) {
      execute_command(cmd);
      add_history(cmd);
      free(cmd);
      ret = snprintf(prompt, 1024, "MEGA65 SD-Card:%s> ", current_dir);
      if (ret < 0)
        ; // suppressing gcc-8 -Wformat-truncation warning like this for now...
    }
#endif
  }

  return 0;
}

void wait_for_sdready(void)
{
  do {

    // Ask for SD card status
    while (mega65_peek(0xffd3680) & 0x3) {
      // Send reset sequence
      printf("SD card not yet ready, so reset it.\n");
      mega65_poke(0xffd3680, 0);
      mega65_poke(0xffd3680, 1);
      sleep(1);
    }

    //     printf("SD Card looks ready.\n");
    //    printf("wait_for_sdready() took %lld usec\n",gettime_us()-start);
  } while (0);
  return;
}

int wait_for_sdready_passive(void)
{
  int retVal = 0;
  do {
    //    long long start=gettime_us();

    int tries = 16;
    int sleep_time = 1;

    // Ask for SD card status
    while (mega65_peek(0xffd3680) & 3) {
      // printf("SD card error 0x3 - failing\n");
      tries--;
      if (tries)
        usleep(sleep_time);
      else {
        retVal = -1;
        break;
      }
      sleep_time *= 2;
    }
    // printf("SD Card looks ready.\n");
    //    printf("wait_for_sdready_passive() took %lld usec\n",gettime_us()-start);
  } while (0);
  return retVal;
}

int sdhc = -1;
int onceOnly = 1;

int sdhc_check(void)
{
  unsigned char buffer[512];

  sdhc = -1;

  int r0 = read_sector(0, buffer, CACHE_NO, 0);
  int r1 = read_sector(1, buffer, CACHE_NO, 0);
  int r200 = read_sector(0x200, buffer, CACHE_NO, 0);
  //  printf("%d %d %d\n",r0,r1,r200);
  if (r0 || r200) {
    fprintf(stderr, "Could not detect SD/SDHC card\n");
    exit(-3);
  }
  if (r1)
    sdhc = 0;
  else
    sdhc = 1;
  if (sdhc)
    fprintf(stderr, "SD card is SDHC\n");
  return sdhc;
}

int load_helper(void)
{
  int retVal = 0;
  do {
    if (!helper_installed) {
      // Install helper routine

      monitor_sync();

      // First see if the helper is already running by looking for the
      // MEGA65FT1.0 string
      request_remotesd_version();
      sleep(1);
      char buffer[8193];
      int bytes = serialport_read(fd, (unsigned char*)buffer, 8192);
      buffer[8192] = 0;
      if (bytes >= (int)strlen("MEGA65FT1.0")) {
        for (int i = 0; i < bytes - strlen("MEGA65FT1.0"); i++) {
          printf("i=%d, bytes=%d, strlen=%d\n", i, bytes, (int)strlen("MEGA65FT1.0"));
          if (!strncmp("MEGA65FT1.0", &buffer[i], strlen("MEGA65FT1.0"))) {
            printf("Helper already running. Nothing to do.\n");
            return 0;
          }
        }
      }

      detect_mode();

      if ((!saw_c64_mode)) {
        start_cpu();
        switch_to_c64mode();
      }

      fake_stop_cpu();

      char cmd[1024];

      // Load helper, minus the 2 byte load address header
      push_ram(0x0801, helperroutine_len - 2, &helperroutine[2]);

      printf("Helper in memory\n");

      if (saw_openrom) {
        stuff_keybuffer("RUN\r");
      }
      else {
        // Launch helper programme
        snprintf(cmd, 1024, "g080d\r");
        slow_write(fd, cmd, strlen(cmd));
        wait_for_prompt();
      }

      snprintf(cmd, 1024, "t0\r");
      slow_write(fd, cmd, strlen(cmd));
      wait_for_prompt();

      helper_installed = 1;
      printf("\nNOTE: Fast SD card access routine installed.\n");
    }
  } while (0);
  return retVal;
}

int data_byte_count = 0;
uint8_t queue_jobs = 0;
uint16_t queue_addr = 0xc001;
uint8_t queue_read_data[1024 * 1024];
uint32_t queue_read_len = 0;

uint8_t queue_cmds[0x0fff];

uint8_t q_rle_count = 0, q_raw_count = 0, q_rle_enable = 0;

void queue_data_decode(uint8_t v)
{
  if (0)
    fprintf(stderr, "Decoding $%02x, rle_count=%d, raw_count=%d, data_byte_count=$%04x\n", v, q_rle_count, q_raw_count,
        data_byte_count);
  if (q_rle_count) {
    //    fprintf(stderr,"$%02x x byte $%02x\n",q_rle_count,v);
    data_byte_count -= q_rle_count;
    for (int i = 0; i < q_rle_count; i++) {
      if (queue_read_len < 1024 * 1024)
        queue_read_data[queue_read_len++] = v;
    }
    q_rle_count = 0;
  }
  else if (q_raw_count) {
    //    fprintf(stderr,"Raw byte $%02x\n",v);
    if (queue_read_len < 1024 * 1024)
      queue_read_data[queue_read_len++] = v;
    if (data_byte_count)
      data_byte_count--;
    q_raw_count--;
  }
  else {
    //    fprintf(stderr,"Data code $%02x\n",v);
    if (v & 0x80) {
      q_rle_count = v & 0x7f;
      //      fprintf(stderr,"RLE of $%02x bytes\n",q_rle_count);
    }
    else {
      q_raw_count = v & 0x7f;
      //      fprintf(stderr,"$%02x raw bytes\n",q_raw_count);
    }
  }
}

void queue_data_decode_raw(uint8_t v)
{
  if (0)
    fprintf(stderr, "Decoding raw byte $%02x, data_byte_count=$%04x\n", v, data_byte_count);
  if (queue_read_len < 1024 * 1024)
    queue_read_data[queue_read_len++] = v;
  if (data_byte_count)
    data_byte_count--;
}

void queue_add_job(uint8_t* j, int len)
{
  bcopy(j, &queue_cmds[queue_addr - 0xc001], len);
  queue_jobs++;
  queue_addr += len;
  //  printf("remote job queued.\n");
}

void job_process_results(void)
{
  long long now = gettime_us();
  queue_read_len = 0;
  uint8_t buff[8192];

  uint8_t recent[32];

  data_byte_count = 0;

  int debug_rx = 0;

  while (1) {
    int b = serialport_read(fd, buff, 8192);
    if (b < 1)
      usleep(0);
    if (b > 0)
      if (debug_rx)
        dump_bytes(0, "jobresponse", buff, b);
    for (int i = 0; i < b; i++) {
      // Keep rolling window of most recent chars for interpreting job
      // results
      if (data_byte_count) {
        if (q_rle_enable)
          queue_data_decode(buff[i]);
        else
          queue_data_decode_raw(buff[i]);
      }
      else {
        bcopy(&recent[1], &recent[0], 30);
        recent[30] = buff[i];
        recent[31] = 0;
        // fprintf(stderr,"i=%d, b=%d, recent[30-10]='%s'\n",i,b,&recent[30-10]);
        if (!strncmp((char*)&recent[30 - 10], "FTBATCHDONE", 11)) {
          long long endtime = gettime_us();
          if (debug_rx)
            printf("%lld: Saw end of batch job after %lld usec\n", endtime - start_usec, endtime - now);
          //	  dump_bytes(0,"read data",queue_read_data,queue_read_len);
          return;
        }
        if (!strncmp((char*)recent, "FTJOBDONE:", 10)) {
          int jn = atoi((char*)&recent[10]);
          if (debug_rx)
            printf("Saw job #%d completion.\n", jn);
        }
        int j_addr, n;
        uint32_t transfer_size;
        int fn = sscanf((char*)recent, "FTJOBDATA:%x:%x:%n", &j_addr, &transfer_size, &n);
        if (fn == 2) {
          if (debug_rx)
            printf("Spotted job data: Reading $%x bytes of RLE data, offset %d,"
                   " %02x %02x\n",
                transfer_size, n, recent[n], recent[n + 1]);
          q_rle_count = 0;
          q_raw_count = 0;
          q_rle_enable = 1;
          data_byte_count = transfer_size;
          // Don't forget to process the bytes we have already injested
          for (int k = n; k <= 30; k++) {
            if (data_byte_count) {
              queue_data_decode(recent[k]);
            }
          }
        }
        // NOTE: the tricky '%n' specifier at the end.
        // It's the count of the number of characters of the format string already processed by the function
        fn = sscanf((char*)recent, "FTJOBDATR:%x:%x:%n", &j_addr, &transfer_size, &n);
        if (fn == 2) {
          if (debug_rx)
            // note that we're hoping that recent[n] and onwards contain the bytes immediately *after* 'FTJOBDATR:%x:%x:'
            printf("Spotted job data: Reading $%x bytes of raw data (j_addr=$%04X) (offset %d, %02x %02x)\n", transfer_size,
                j_addr, n, recent[n], recent[n + 1]);
          q_rle_count = 0;
          q_raw_count = 0;
          q_rle_enable = 0;
          data_byte_count = transfer_size;
          // printf("data_byte_count=0x%X\n", data_byte_count);
          // Don't forget to process the bytes we have already injested
          // I.e., don't accidentally miss any initial data-bytes that were on the tail-end of this 'JTJOBDATR:%x:%x:' string
          for (int k = n; k <= 30; k++) {
            if (data_byte_count) {
              queue_data_decode_raw(recent[k]);
            }
          }
        }
      }
    }
  }
}

void queue_execute(void)
{
  //  long long start = gettime_us();

  // Push queued jobs in one go
  push_ram(0xc001, queue_addr - 0xc001, queue_cmds);
  //  dump_bytes(0,"queue_cmds",queue_cmds,queue_addr-0xc001);

  // Then set number of jobs to execute them
  // mega65_poke will try to read data after from on the
  // serial interface, which messes up the protocol.
  // so don't do that.
  char cmd[1024];
  snprintf(cmd, 1024, "sc000 %x\r", queue_jobs);
  slow_write(fd, cmd, strlen(cmd));

  job_process_results();
  queue_addr = 0xc001;
  queue_jobs = 0;
}

uint32_t write_buffer_offset = 0;
uint8_t write_data_buffer[65536];
uint32_t write_sector_numbers[65536 / 512];
uint8_t write_sector_count = 0;

void queue_physical_write_sector(uint32_t sector_number, uint32_t mega65_address)
{
  uint8_t job[9];
  job[0] = 0x02;
  job[5] = sector_number >> 0;
  job[6] = sector_number >> 8;
  job[7] = sector_number >> 16;
  job[8] = sector_number >> 24;
  job[1] = mega65_address >> 0;
  job[2] = mega65_address >> 8;
  job[3] = mega65_address >> 16;
  job[4] = mega65_address >> 24;
  //  printf("queue writing to sector $%08x\n",sector_number);
  queue_add_job(job, 9);
}

int execute_write_queue(void)
{
  if (write_sector_count == 0)
    return 0;

  int retVal = 0;
  do {
    if (0)
      printf("Executing write queue with %d sectors in the queue (write_buffer_offset=$%08x)\n", write_sector_count,
          write_buffer_offset);
    push_ram(0x50000, write_buffer_offset, &write_data_buffer[0]);

    // XXX - Sort sector number order and merge consecutive writes into
    // multi-sector writes would be a good idea here.
    for (int i = 0; i < write_sector_count; i++) {
      queue_physical_write_sector(write_sector_numbers[i], 0x50000 + (i << 9));
    }
    queue_execute();

    // Reset write queue
    write_buffer_offset = 0;
    write_sector_count = 0;
  } while (0);
  return retVal;
}

void queue_write_sector(uint32_t sector_number, uint8_t* buffer)
{
  // Merge writes to same sector
  for (int i = 0; i < write_sector_count; i++) {
    if (sector_number == write_sector_numbers[i]) {
      // printf("Updating sector $%08x while in the write queue.\n",sector_number);
      bcopy(buffer, &write_data_buffer[i << 9], 512);
      return;
    }
  }

  // Purge pending jobs when they are banked up
  // (only 32KB at a time, as the l command for fast pushing data
  // can't do 64KB
  if (write_buffer_offset >= 32768)
    execute_write_queue();

  // printf("adding sector $%08x to the write queue (pos#%d)\n", sector_number, write_sector_count);
  bcopy(buffer, &write_data_buffer[write_buffer_offset], 512);
  write_buffer_offset += 512;
  write_sector_numbers[write_sector_count] = sector_number;
  write_sector_count++;
}

void queue_read_sector(uint32_t sector_number, uint32_t mega65_address)
{
  uint8_t job[9];
  job[0] = 0x01;
  job[5] = sector_number >> 0;
  job[6] = sector_number >> 8;
  job[7] = sector_number >> 16;
  job[8] = sector_number >> 24;
  job[1] = mega65_address >> 0;
  job[2] = mega65_address >> 8;
  job[3] = mega65_address >> 16;
  job[4] = mega65_address >> 24;
  //  printf("queue reading from sector $%08x into MEM @ $%08x\n",sector_number,mega65_address);
  queue_add_job(job, 9);
}

void queue_read_sectors(uint32_t sector_number, uint16_t sector_count)
{
  uint8_t job[7];
  job[0] = 0x04; // switching from 0x03 (RLE) to 0x04 (no RLE) as I'm sensing RLE is problematic, and wasn't even being
                 // triggered (except by chance), to due a bug on the 'remotesd.c' side.
  job[1] = sector_count >> 0;
  job[2] = sector_count >> 8;
  job[3] = sector_number >> 0;
  job[4] = sector_number >> 8;
  job[5] = sector_number >> 16;
  job[6] = sector_number >> 24;
  //  printf("queue reading %d sectors, beginning with sector $%08x\n",sector_count,sector_number);
  queue_add_job(job, 7);
}

void queue_read_mem(uint32_t mega65_address, uint32_t len)
{
  uint8_t job[9];
  job[0] = 0x11;
  job[1] = mega65_address >> 0;
  job[2] = mega65_address >> 8;
  job[3] = mega65_address >> 16;
  job[4] = mega65_address >> 24;
  job[5] = len >> 0;
  job[6] = len >> 8;
  job[7] = len >> 16;
  job[8] = len >> 24;
  //  printf("queue reading mem @ $%08x (len = %d)\n",mega65_address,len);
  queue_add_job(job, 9);
}

// XXX - DO NOT USE A BUFFER THAT IS ON THE STACK OR BAD BAD THINGS WILL HAPPEN
int DIRTYMOCK(read_sector)(const unsigned int sector_number, unsigned char* buffer, int useCache, int readAhead)
{
  int retVal = 0;
  do {

    int cachedRead = 0;

    if (useCache == CACHE_YES) {
      for (int i = 0; i < sector_cache_count; i++) {
        if (sector_cache_sectors[i] == sector_number) {
          bcopy(sector_cache[i], buffer, 512);
          retVal = 0;
          cachedRead = 1;
          break;
        }
      }
    }

    if (cachedRead)
      break;

    // Do read using new remote job queue mechanism that is hopefully
    // lower latency than the old way
    // Request multiple sectors at once to make it more efficient
    int batch_read_size = 16;
    if (readAhead > 16)
      batch_read_size = readAhead;

    //    for (int n=0;n<batch_read_size;n++)
    //      queue_read_sector(sector_number+n,0x40000+(n<<9));
    //    queue_read_mem(0x40000,512*batch_read_size);
    queue_read_sectors(sector_number, batch_read_size);
    queue_execute();

    for (int n = 0; n < batch_read_size; n++) {
      bcopy(&queue_read_data[n << 9], buffer, 512);
      //      printf("Sector $%08x:\n",sector_number+n);
      //      dump_bytes(3,"read sector",buffer,512);

      // Store in cache / update cache
      int i;
      for (i = 0; i < sector_cache_count; i++)
        if (sector_cache_sectors[i] == sector_number + n)
          break;
      if (i < SECTOR_CACHE_SIZE) {
        bcopy(buffer, sector_cache[i], 512);
        sector_cache_sectors[i] = sector_number + n;
        if (sector_cache_count < (i + 1))
          sector_cache_count = i + 1;
      }
      else {
        execute_write_queue();
        sector_cache_count = 0;
      }
    }

    // Make sure to return the actual sector that was asked for
    bcopy(&queue_read_data[0], buffer, 512);

  } while (0);
  if (retVal)
    printf("FAIL reading sector %d\n", sector_number);
  return retVal;
}

unsigned char verify[512];

int DIRTYMOCK(write_sector)(const unsigned int sector_number, unsigned char* buffer)
{
  int retVal = 0;
  do {
    // With new method, we write the data, then schedule the write to happen with a job
#if 0
    char cmd[1024];
    // Clear pending input first    
    int b=1;
    while(b>0){
      b=serialport_read(fd,(uint8_t *)cmd,1024);
      //      if (b) dump_bytes(3,"write_sector() flush data",cmd,b);
    }
#endif

    queue_write_sector(sector_number, buffer);

    // Store in cache / update cache
    int i;
    for (i = 0; i < sector_cache_count; i++)
      if (sector_cache_sectors[i] == sector_number)
        break;
    if (i < SECTOR_CACHE_SIZE) {
      bcopy(buffer, sector_cache[i], 512);
      sector_cache_sectors[i] = sector_number;
      if (sector_cache_count < (i + 1))
        sector_cache_count = i + 1;
    }
    else {
      execute_write_queue();
      sector_cache_count = 0;
    }

  } while (0);
  if (retVal)
    printf("FAIL writing sector %d\n", sector_number);
  return retVal;
}

int open_file_system(void)
{
  int retVal = 0;
  do {
    if (read_sector(0, mbr, CACHE_YES, 0)) {
      fprintf(stderr, "ERROR: Could not read MBR\n");
      retVal = -1;
      break;
    }

    for (int i = 0; i < 4; i++) {
      unsigned char* part_ent = &mbr[0x1be + (i * 0x10)];
      // dump_bytes(0,"partent",part_ent,16);
      if (part_ent[4] == 0x0c || part_ent[4] == 0x0b) {
        partition_start = part_ent[8] + (part_ent[9] << 8) + (part_ent[10] << 16) + (part_ent[11] << 24);
        partition_size = part_ent[12] + (part_ent[13] << 8) + (part_ent[14] << 16) + (part_ent[15] << 24);
        printf("Found FAT32 partition in partition slot %d : start sector=$%x, size=%d MB\n", i, partition_start,
            partition_size / 2048);
      }
      if (part_ent[4] == 0x41) {
        syspart_start = part_ent[8] + (part_ent[9] << 8) + (part_ent[10] << 16) + (part_ent[11] << 24);
        syspart_size = part_ent[12] + (part_ent[13] << 8) + (part_ent[14] << 16) + (part_ent[15] << 24);
        printf("Found MEGA65 system partition in partition slot %d : start sector=$%x, size=%d MB\n", i, syspart_start,
            syspart_size / 2048);
      }
    }

    if (syspart_start) {
      // Ok, so we know where the partition starts, so now find the FATs
      if (read_sector(syspart_start, syspart_sector0, CACHE_YES, 0)) {
        printf("ERROR: Could not read system partition sector 0\n");
        retVal = -1;
        break;
      }
      if (strncmp("MEGA65SYS00", (char*)&syspart_sector0[0], 10)) {
        printf("ERROR: MEGA65 System Partition is missing MEGA65SYS00 marker.\n");
        dump_bytes(0, "SYSPART Sector 0", syspart_sector0, 512);
        retVal = -1;
        break;
      }
      syspart_freeze_area = syspart_sector0[0x10] + (syspart_sector0[0x11] << 8) + (syspart_sector0[0x12] << 16)
                          + (syspart_sector0[0x13] << 24);
      syspart_freeze_program_size = syspart_sector0[0x14] + (syspart_sector0[0x15] << 8) + (syspart_sector0[0x16] << 16)
                                  + (syspart_sector0[0x17] << 24);
      syspart_slot_size = syspart_sector0[0x18] + (syspart_sector0[0x19] << 8) + (syspart_sector0[0x1a] << 16)
                        + (syspart_sector0[0x1b] << 24);
      syspart_slot_count = syspart_sector0[0x1c] + (syspart_sector0[0x1d] << 8);
      syspart_slotdir_sectors = syspart_sector0[0x1e] + (syspart_sector0[0x1f] << 8);
      syspart_service_area = syspart_sector0[0x20] + (syspart_sector0[0x21] << 8) + (syspart_sector0[0x22] << 16)
                           + (syspart_sector0[0x23] << 24);
      syspart_service_area_size = syspart_sector0[0x24] + (syspart_sector0[0x25] << 8) + (syspart_sector0[0x26] << 16)
                                + (syspart_sector0[0x27] << 24);
      syspart_service_slot_size = syspart_sector0[0x28] + (syspart_sector0[0x29] << 8) + (syspart_sector0[0x2a] << 16)
                                + (syspart_sector0[0x2b] << 24);
      syspart_service_slot_count = syspart_sector0[0x2c] + (syspart_sector0[0x2d] << 8);
      syspart_service_slotdir_sectors = syspart_sector0[0x2e] + (syspart_sector0[0x2f] << 8);
    }

    if (!partition_start) {
      retVal = -1;
      break;
    }
    if (!partition_size) {
      retVal = -1;
      break;
    }

    // Ok, so we know where the partition starts, so now find the FATs
    if (read_sector(partition_start, fat_mbr, CACHE_YES, 0)) {
      printf("ERROR: Could not read FAT MBR\n");
      retVal = -1;
      break;
    }

    if (fat_mbr[510] != 0x55) {
      printf("ERROR: Invalid FAT MBR signature in sector %d ($%x)\n", partition_start, partition_start);
      retVal = -1;
      break;
    }
    if (fat_mbr[511] != 0xAA) {
      printf("ERROR: Invalid FAT MBR signature in sector %d ($%x)\n", partition_start, partition_start);
      dump_bytes(0, "fat_mbr", fat_mbr, 512);
      retVal = -1;
      break;
    }
    if (fat_mbr[12] != 2) {
      printf("ERROR: FAT32 file system uses a sector size other than 512 bytes\n");
      retVal = -1;
      break;
    }
    if (fat_mbr[16] != 2) {
      printf("ERROR: FAT32 file system has more or less than 2 FATs\n");
      retVal = -1;
      break;
    }
    sectors_per_cluster = fat_mbr[13];
    reserved_sectors = fat_mbr[14] + (fat_mbr[15] << 8);
    data_sectors = (fat_mbr[0x20] << 0) | (fat_mbr[0x21] << 8) | (fat_mbr[0x22] << 16) | (fat_mbr[0x23] << 24);
    sectors_per_fat = (fat_mbr[0x24] << 0) | (fat_mbr[0x25] << 8) | (fat_mbr[0x26] << 16) | (fat_mbr[0x27] << 24);
    first_cluster = (fat_mbr[0x2c] << 0) | (fat_mbr[0x2d] << 8) | (fat_mbr[0x2e] << 16) | (fat_mbr[0x2f] << 24);
    fsinfo_sector = fat_mbr[0x30] + (fat_mbr[0x31] << 8);
    fat1_sector = reserved_sectors;
    fat2_sector = fat1_sector + sectors_per_fat;
    first_cluster_sector = fat2_sector + sectors_per_fat;

    printf("FAT32 file system has %dMB formatted capacity, first cluster = %d, %d sectors per FAT\n", data_sectors / 2048,
        first_cluster, sectors_per_fat);
    printf("FATs begin at sector 0x%x and 0x%x\n", fat1_sector, fat2_sector);

    file_system_found = 1;

  } while (0);
  return retVal;
}

unsigned char buf[512];

unsigned int get_next_cluster(int cluster)
{
  unsigned int retVal = 0xFFFFFFFF;

  do {
    // Read chain entry for this cluster
    int cluster_sector_number = cluster / (512 / 4); // This is really the sector that the current cluster-id is located in
                                                     // the FAT table
    int cluster_sector_offset = (cluster * 4) & 511;

    // Read sector of cluster
    if (read_sector(partition_start + fat1_sector + cluster_sector_number, buf, CACHE_YES, 0))
      break;

    // Get value out
    retVal = (buf[cluster_sector_offset + 0] << 0) | (buf[cluster_sector_offset + 1] << 8)
           | (buf[cluster_sector_offset + 2] << 16) | (buf[cluster_sector_offset + 3] << 24);

    // mask out highest 4 bits (these seem to be flags on some systems)
    retVal &= 0x0fffffff;
  } while (0);
  return retVal;
}

unsigned char dir_sector_buffer[512];
unsigned int dir_sector = -1; // no dir
int dir_cluster = 0;
int dir_sector_in_cluster = 0;
int dir_sector_offset = 0;

int fat_opendir(char* path)
{
  int retVal = 0;
  do {

    dir_cluster = first_cluster;
    dir_sector = first_cluster_sector;
    dir_sector_offset = -32;
    dir_sector_in_cluster = 0;

    retVal = read_sector(partition_start + dir_sector, dir_sector_buffer, CACHE_YES, 0);
    if (retVal)
      dir_sector = -1;

    while (strlen(path)) {
      // Get name of next dir segment
      int seg_len = 0;
      char path_seg[1024] = "";
      for (seg_len = 0; path[seg_len] && path[seg_len] != '/'; seg_len++)
        continue;
      strcpy(path_seg, path);
      path_seg[seg_len] = 0;
      path += seg_len;
      while (path[0] == '/')
        path++;

      if (path_seg[0]) {
        // Each call we have the first sector of the directory
        // Go through the directory looking for the file
        // We can use fat_readdir for this
        struct m65dirent d;
        int found = 0;
        while (!fat_readdir(&d)) {
          if (!strcmp(d.d_name, path_seg)) {
            if (d.d_attr & 0x10) {
              // Its a dir, so change directory to here, and repeat

              dir_cluster = d.d_ino;
              dir_sector = first_cluster_sector + (dir_cluster - first_cluster) * sectors_per_cluster;
              dir_sector_offset = -32;
              dir_sector_in_cluster = 0;

              //	      printf("Found matching subdir '%s' @ cluster %ld\n",d.d_name,d.d_ino);

              retVal = read_sector(partition_start + dir_sector, dir_sector_buffer, CACHE_YES, 0);
              if (retVal)
                dir_sector = -1;
              else
                found = 1;

              break;
            }
            else {
              fprintf(stderr, "ERROR: %s is not a directory\n", path_seg);
              retVal = -1;
              break;
            }
          }
          if (retVal)
            break;
        }
        if (!found) {
          fprintf(stderr, "ERROR: Could not find directory segment '%s'\n", path_seg);

          dir_cluster = first_cluster;
          dir_sector = first_cluster_sector;
          dir_sector_offset = -32;
          dir_sector_in_cluster = 0;

          retVal = -1;
          break;
        }
      }
      if (retVal)
        break;
    }

    // printf("dir_cluster = $%x, dir_sector = $%x\n", dir_cluster, partition_start + dir_sector);

  } while (0);
  return retVal;
}

int advance_to_next_entry(void)
{
  int retVal = 0;

  // Advance to next entry
  dir_sector_offset += 32;
  if (dir_sector_offset == 512) {
    dir_sector_offset = 0;
    dir_sector++;
    dir_sector_in_cluster++;
    if (dir_sector_in_cluster == sectors_per_cluster) {
      // Follow to next cluster
      int next_cluster = get_next_cluster(dir_cluster);
      if (next_cluster < 0xFFFFFF0 && next_cluster) {
        dir_cluster = next_cluster;
        dir_sector_in_cluster = 0;
        dir_sector = first_cluster_sector + (next_cluster - first_cluster) * sectors_per_cluster;
      }
      else {
        // End of directory reached
        dir_sector = -1;
        retVal = -2;
        return retVal;
      }
    }
    if (dir_sector != -1)
      retVal = read_sector(partition_start + dir_sector, dir_sector_buffer, CACHE_YES, 0);
    if (retVal)
      dir_sector = -1;
  }

  return retVal;
}

void debug_vfatchunk(void)
{
  int start = 0x01;
  int len = 5;

  for (int k = start; k < (start + len * 2); k += 2)
    printf("%c", dir_sector_buffer[dir_sector_offset + k]);

  start = 0x0E;
  len = 6;

  for (int k = start; k < (start + len * 2); k += 2)
    printf("%c", dir_sector_buffer[dir_sector_offset + k]);

  start = 0x1C;
  len = 2;

  for (int k = start; k < (start + len * 2); k += 2)
    printf("%c", dir_sector_buffer[dir_sector_offset + k]);

  printf("\n");
}

void copy_to_dnamechunk_from_offset(char* dnamechunk, int offset, int numuc2chars)
{
  for (int k = 0; k < numuc2chars; k++) {
    dnamechunk[k] = dir_sector_buffer[dir_sector_offset + offset + k * 2];
  }
}

void copy_vfat_chars_into_dname(char* dname, int seqnumber)
{
  // increment char-pointer to the seqnumber string chunk we'll copy across
  dname = dname + 13 * (seqnumber - 1);
  copy_to_dnamechunk_from_offset(dname, 0x01, 5);
  dname += 5;
  copy_to_dnamechunk_from_offset(dname, 0x0E, 6);
  dname += 6;
  copy_to_dnamechunk_from_offset(dname, 0x1C, 2);
}

int fat_readdir(struct m65dirent* d)
{
  int retVal = 0;
  int vfatEntry = 0;
  int deletedEntry = 0;
  d->d_type = 0;

  do {
    retVal = advance_to_next_entry();

    if (retVal == -2) // exiting due to end-of-directory?
    {
      retVal = -1;
      break;
    }

    if (dir_sector == -1) {
      retVal = -1;
      break;
    }
    if (!d) {
      retVal = -1;
      break;
    }

    // printf("Found dirent %d %d %d\n",dir_sector,dir_sector_offset,dir_sector_in_cluster);

    // Read in all FAT32-VFAT entries to extract out long filenames
    if (dir_sector_buffer[dir_sector_offset + 0x0B] == 0x0F) {
      vfatEntry = 1;
      int firstTime = 1;
      int seqnumber;
      do {
        // printf("seq = 0x%02X\n", dir_sector_buffer[dir_sector_offset+0x00]);
        // debug_vfatchunk();
        int seq = dir_sector_buffer[dir_sector_offset + 0x00];

        if (seq == 0xE5) // if deleted-entry, then ignore
        {
          // printf("deleteentry!\n");
          deletedEntry = 1;
        }

        seqnumber = seq & 0x1F;

        // assure there is a null-terminator
        if (firstTime) {
          d->d_name[seqnumber * 13] = 0;
          firstTime = 0;
        }

        // vfat seqnumbers will be parsed from high to low, each containing up to 13 UCS-2 characters
        copy_vfat_chars_into_dname(d->d_name, seqnumber);
        advance_to_next_entry();

        // if next dirent is not a vfat entry, break out
        if (dir_sector_buffer[dir_sector_offset + 0x0B] != 0x0F)
          break;
      } while (seqnumber != 1);
    }

    // ignore any vfat files starting with '.' (such as mac osx '._*' metadata files)
    if (vfatEntry && d->d_name[0] == '.') {
      // printf("._ vfat hide\n");
      d->d_name[0] = 0;
      return 0;
    }

    // ignored deleted vfat entries too (mac osx '._*' files are marked as deleted entries)
    if (deletedEntry) {
      d->d_name[0] = 0;
      return 0;
    }

    // if the DOS 8.3 entry is a deleted-entry, then ignore
    if (dir_sector_buffer[dir_sector_offset] == 0xE5) {
      d->d_name[0] = 0;
      return 0;
    }

    int attrib = dir_sector_buffer[dir_sector_offset + 0x0B];

    // if this is the volume-name of the partition, then ignore
    if (attrib == 0x08) {
      d->d_name[0] = 0;
      return 0;
    }

    // if the hidden attribute is turned on, then ignore
    if (attrib & 0x02) {
      d->d_name[0] = 0;
      return 0;
    }

    // Put cluster number in d_ino
    d->d_ino = (dir_sector_buffer[dir_sector_offset + 0x1A] << 0) | (dir_sector_buffer[dir_sector_offset + 0x1B] << 8)
             | (dir_sector_buffer[dir_sector_offset + 0x14] << 16) | (dir_sector_buffer[dir_sector_offset + 0x15] << 24);

    // if not vfat-longname, then extract out old 8.3 name
    if (!vfatEntry) {
      int namelen = 0;
      int nt_flags = dir_sector_buffer[dir_sector_offset + 0x0C];
      int basename_lowercase = nt_flags & 0x08;
      int extension_lowercase = nt_flags & 0x10;

      // get the 8-byte filename
      if (dir_sector_buffer[dir_sector_offset]) {
        for (int i = 0; i < 8; i++) {
          if (dir_sector_buffer[dir_sector_offset + i]) {
            int c = dir_sector_buffer[dir_sector_offset + i];
            if (basename_lowercase)
              c = tolower(c);
            d->d_name[namelen++] = c;
          }
        }
        while (namelen && d->d_name[namelen - 1] == ' ')
          namelen--;
      }
      // get the 3-byte extension
      if (dir_sector_buffer[dir_sector_offset + 8] && dir_sector_buffer[dir_sector_offset + 8] != ' ') {
        d->d_name[namelen++] = '.';
        for (int i = 0; i < 3; i++) {
          if (dir_sector_buffer[dir_sector_offset + 8 + i]) {
            int c = dir_sector_buffer[dir_sector_offset + 8 + i];
            if (extension_lowercase)
              c = tolower(c);
            d->d_name[namelen++] = c;
          }
        }
        while (namelen && d->d_name[namelen - 1] == ' ')
          namelen--;
      }
      d->d_name[namelen] = 0;
    }

    if (dirent_raw && d->d_name[0])
      dump_bytes(0, "dirent raw", &dir_sector_buffer[dir_sector_offset], 32);

    d->d_filelen = (dir_sector_buffer[dir_sector_offset + 0x1C] << 0) | (dir_sector_buffer[dir_sector_offset + 0x1D] << 8)
                 | (dir_sector_buffer[dir_sector_offset + 0x1E] << 16) | (dir_sector_buffer[dir_sector_offset + 0x1F] << 24);
    d->d_attr = dir_sector_buffer[dir_sector_offset + 0xb];

    if (d->d_attr & 0xC8)
      d->d_type = M65DT_UNKNOWN;
    else if (d->d_attr & 0x10)
      d->d_type = M65DT_DIR;
    else if (d->d_attr == 0 && d->d_name[0] == 0)
      d->d_type = M65DT_FREESLOT;
    else
      d->d_type = M65DT_REG;

  } while (0);
  return retVal;
}

int chain_cluster(unsigned int cluster, unsigned int next_cluster)
{
  int retVal = 0;

  do {
    int fat_sector_num = cluster / (512 / 4);
    int fat_sector_offset = (cluster * 4) & 0x1FF;
    if (fat_sector_num >= sectors_per_fat) {
      printf("ERROR: cluster number too large.\n");
      retVal = -1;
      break;
    }

    // Read in the sector of FAT1
    unsigned char fat_sector[512];
    if (read_sector(partition_start + fat1_sector + fat_sector_num, fat_sector, CACHE_YES, 0)) {
      printf("ERROR: Failed to read sector $%x of first FAT\n", fat_sector_num);
      retVal = -1;
      break;
    }

    //    dump_bytes(0,"FAT sector",fat_sector,512);

    if (0)
      printf("Marking cluster $%x in use by writing to offset $%x of FAT sector $%x\n", cluster, fat_sector_offset,
          fat_sector_num);

    // Set the bytes for this cluster to $0FFFFF8 to mark end of chain and in use
    fat_sector[fat_sector_offset + 0] = (next_cluster >> 0) & 0xff;
    fat_sector[fat_sector_offset + 1] = (next_cluster >> 8) & 0xff;
    fat_sector[fat_sector_offset + 2] = (next_cluster >> 16) & 0xff;
    fat_sector[fat_sector_offset + 3] = (next_cluster >> 24) & 0x0f;

    if (0)
      printf("Marking cluster in use in FAT1\n");

    // Write sector back to FAT1
    if (write_sector(partition_start + fat1_sector + fat_sector_num, fat_sector)) {
      printf("ERROR: Failed to write updated FAT sector $%x to FAT1\n", fat_sector_num);
      retVal = -1;
      break;
    }

    if (0)
      printf("Marking cluster in use in FAT2\n");

    // Write sector back to FAT2
    if (write_sector(partition_start + fat2_sector + fat_sector_num, fat_sector)) {
      printf("ERROR: Failed to write updated FAT sector $%x to FAT1\n", fat_sector_num);
      retVal = -1;
      break;
    }

    if (0)
      printf("Done allocating cluster\n");

  } while (0);

  return retVal;
}

int set_fat_cluster_ptr(unsigned int cluster, unsigned int value)
{
  int retVal = 0;

  do {
    int fat_sector_num = cluster / (512 / 4);
    int fat_sector_offset = (cluster * 4) & 0x1FF;
    if (fat_sector_num >= sectors_per_fat) {
      printf("ERROR: cluster number too large.\n");
      retVal = -1;
      break;
    }

    // Read in the sector of FAT1
    unsigned char fat_sector[512];
    if (read_sector(partition_start + fat1_sector + fat_sector_num, fat_sector, CACHE_YES, 0)) {
      printf("ERROR: Failed to read sector $%x of first FAT\n", fat_sector_num);
      retVal = -1;
      break;
    }

    //    dump_bytes(0,"FAT sector",fat_sector,512);

    if (0)
      printf("Marking cluster $%x in use by writing to offset $%x of FAT sector $%x\n", cluster, fat_sector_offset,
          fat_sector_num);

    // Set the bytes for this cluster to $0FFFFF8 to mark end of chain and in use
    fat_sector[fat_sector_offset + 0] = (value)&0xff;
    fat_sector[fat_sector_offset + 1] = (value >> 8) & 0xff;
    fat_sector[fat_sector_offset + 2] = (value >> 16) & 0xff;
    fat_sector[fat_sector_offset + 3] = (value >> 24) & 0xff;

    if (0)
      printf("Marking cluster in use in FAT1\n");

    // Write sector back to FAT1
    if (write_sector(partition_start + fat1_sector + fat_sector_num, fat_sector)) {
      printf("ERROR: Failed to write updated FAT sector $%x to FAT1\n", fat_sector_num);
      retVal = -1;
      break;
    }

    if (0)
      printf("Marking cluster in use in FAT2\n");

    // Write sector back to FAT2
    if (write_sector(partition_start + fat2_sector + fat_sector_num, fat_sector)) {
      printf("ERROR: Failed to write updated FAT sector $%x to FAT1\n", fat_sector_num);
      retVal = -1;
      break;
    }

    if (0)
      printf("Done allocating cluster\n");

  } while (0);

  return retVal;
}

int deallocate_cluster(unsigned int cluster)
{
  return set_fat_cluster_ptr(cluster, 0x00000000);
}

int allocate_cluster(unsigned int cluster)
{
  return set_fat_cluster_ptr(cluster, 0x0ffffff8);
}

unsigned int chained_cluster(unsigned int cluster)
{
  unsigned int retVal = 0;

  do {
    int fat_sector_num = cluster / (512 / 4);
    int fat_sector_offset = (cluster * 4) & 0x1FF;
    if (fat_sector_num >= sectors_per_fat) {
      printf("ERROR: cluster number too large.\n");
      retVal = -1;
      break;
    }

    // Read in the sector of FAT1
    unsigned char fat_sector[512];
    if (read_sector(partition_start + fat1_sector + fat_sector_num, fat_sector, CACHE_YES, 0)) {
      printf("ERROR: Failed to read sector $%x of first FAT\n", fat_sector_num);
      retVal = -1;
      break;
    }

    // Set the bytes for this cluster to $0FFFFF8 to mark end of chain and in use
    retVal = fat_sector[fat_sector_offset + 0];
    retVal |= fat_sector[fat_sector_offset + 1] << 8;
    retVal |= fat_sector[fat_sector_offset + 2] << 16;
    retVal |= fat_sector[fat_sector_offset + 3] << 24;

    //    printf("Cluster %d chains to cluster %d ($%x)\n",cluster,retVal,retVal);

  } while (0);

  return retVal;
}

unsigned char fat_sector[512];

BOOL is_free_cluster(unsigned int cluster)
{
  int i, o;
  i = cluster / (512 / 4);
  o = cluster % (512 / 4) * 4;

  if (read_sector(partition_start + fat1_sector + i, fat_sector, CACHE_YES, 0)) {
    printf("ERROR: Failed to read sector $%x of first FAT\n", i);
    exit(-1);
  }

  if (!(fat_sector[o] | fat_sector[o + 1] | fat_sector[o + 2] | fat_sector[o + 3])) {
    return TRUE;
  }

  return FALSE;
}

unsigned int find_free_cluster(unsigned int first_cluster)
{
  unsigned int cluster = 0;

  int retVal = 0;

  do {
    int i, o;

    i = first_cluster / (512 / 4);
    o = (first_cluster % (512 / 4)) * 4;

    for (; i < sectors_per_fat; i++) {
      // Read FAT sector
      //      printf("Checking FAT sector $%x for free clusters.\n",i);
      if (read_sector(partition_start + fat1_sector + i, fat_sector, CACHE_YES, 0)) {
        printf("ERROR: Failed to read sector $%x of first FAT\n", i);
        retVal = -1;
        break;
      }

      if (retVal)
        break;

      // Search for free sectors
      for (; o < 512; o += 4) {
        if (!(fat_sector[o] | fat_sector[o + 1] | fat_sector[o + 2] | fat_sector[o + 3])) {
          // Found a free cluster.
          cluster = i * (512 / 4) + (o / 4);
          // printf("cluster sector %d, offset %d yields cluster %d\n",i,o,cluster);
          break;
        }
      }
      o = 0;

      if (cluster || retVal)
        break;
    }

    // printf("I believe cluster $%x is free.\n",cluster);

    retVal = cluster;
  } while (0);

  return retVal;
}

unsigned int find_contiguous_clusters(unsigned int total_clusters)
{
  unsigned int start_cluster = 0;

  while (1) {
    BOOL is_contiguous = TRUE;
    unsigned int cnt;
    start_cluster = find_free_cluster(start_cluster);

    for (cnt = 1; cnt < total_clusters; cnt++) {
      if (!is_free_cluster(start_cluster + cnt)) {
        is_contiguous = FALSE;
        break;
      }
    }

    if (is_contiguous)
      break;

    start_cluster += cnt;
  }

  return start_cluster;
}

typedef struct _llist {
  void* item;
  struct _llist* next;
} llist;

void llist_free(llist* lstitem)
{
  llist* next;
  while (lstitem != NULL) {
    free(lstitem->item);
    next = lstitem->next;
    free(lstitem);
    lstitem = next;
  }
}

llist* llist_new(void)
{
  llist* lst = (llist*)malloc(sizeof(llist));
  memset(lst, 0, sizeof(llist));
  return lst;
}

void llist_add(llist* lst, void* item, int compare(void*, void*))
{
  if (lst->item == NULL) {
    lst->item = item;
    return;
  }

  llist* prev = NULL;

  while (lst != NULL) {
    // we found a home for it?
    if (compare(lst->item, item) > 0) {
      llist* mvlst = llist_new();
      mvlst->item = lst->item;
      mvlst->next = lst->next;

      lst->item = item;
      lst->next = mvlst;
      return;
    }
    prev = lst;
    lst = lst->next;
  }
  // couldn't insert before, so add to end
  llist* lstnew = llist_new();
  lstnew->item = item;
  prev->next = lstnew;
}

int compare_dirents(void* s, void* d)
{
  struct m65dirent* src = (struct m65dirent*)s;
  struct m65dirent* dest = (struct m65dirent*)d;
  // both dirs?
  if ((dest->d_attr & 0x10) && (src->d_attr & 0x10)) {
    // compare filenames
    return stricmp(src->d_name, dest->d_name);
  }
  // new item is dir and existing item isn't?
  else if ((dest->d_attr & 0x10) && !(src->d_attr & 0x10))
    return 1;
  // new item is file and existing item is a dir?
  else if (!(dest->d_attr & 0x10) && (src->d_attr & 0x10))
    return -1;
  else
    // compare filenames
    return stricmp(src->d_name, dest->d_name);
}

int read_direntries(llist* lst, char* path)
{
  struct m65dirent de;

  if (fat_opendir(path)) {
    return 0;
  }
  // printf("Opened directory, dir_sector=%d (absolute sector = %d)\n",dir_sector,partition_start+dir_sector);
  while (!fat_readdir(&de)) {
    struct m65dirent* denew = (struct m65dirent*)malloc(sizeof(struct m65dirent));
    memcpy(denew, &de, sizeof(struct m65dirent));
    llist_add(lst, denew, compare_dirents);
  }

  return 1;
}

int contains_dir(llist* lst, char* path)
{
  while (lst != NULL) {
    struct m65dirent* itm = (struct m65dirent*)lst->item;
    if (itm->d_attr & 0x10 && strcmp(itm->d_name, path) == 0)
      return 1;

    lst = lst->next;
  }

  return 0;
}

int compare_char(char a, char b, int case_sensitive)
{
  if (case_sensitive) {
    return a == b;
  }
  else {
    return tolower(a) == tolower(b);
  }
}

// Initial effort borrowed from Robert James Mieta's snippet in this thread:
// - https://stackoverflow.com/questions/23457305/compare-strings-with-wildcard
// Needed a bit of refinement to get the wildcards working for me though.
int is_match(char* line, char* pattern, int case_sensitive)
{
  int wildcard = 0;

  do {
    if (compare_char(*pattern, *line, case_sensitive) || (*pattern == '?')) {
      line++;
      pattern++;
    }
    else if (*pattern == '*') {
      if (*(++pattern) == '\0') {
        return 1;
      }
      wildcard = 1;
    }
    else if (wildcard) {
      if (compare_char(*line, *pattern, case_sensitive)) {
        wildcard = 0;
        line++;
        pattern++;
      }
      else {
        line++;
      }
    }
    else {
      return 0;
    }
  } while (*line);

  if (*pattern == '\0') {
    return 1;
  }
  else {
    return 0;
  }
}

void show_local_directory(char* searchpattern)
{
  DIR* d;
  struct dirent* dir;

  // list directories first
  d = opendir(".");
  if (d) {
    while ((dir = readdir(d)) != NULL) {
      if (searchpattern && !is_match(dir->d_name, searchpattern, 1))
        continue;

      struct stat file_stats;
      if (!stat(dir->d_name, &file_stats)) {
        if (S_ISDIR(file_stats.st_mode))
          printf("       <DIR> %s\n", dir->d_name);
      }
    }
  }
  closedir(d);

  // list files next
  d = opendir(".");
  if (d) {
    while ((dir = readdir(d)) != NULL) {
      if (searchpattern && !is_match(dir->d_name, searchpattern, 1))
        continue;

      struct stat file_stats;
      if (!stat(dir->d_name, &file_stats)) {
        if (!S_ISDIR(file_stats.st_mode)) {
          if (dir->d_name[0] && file_stats.st_size >= 0)
            printf("%12d %s\n", (int)file_stats.st_size, dir->d_name);
        }
      }
    }
  }
  closedir(d);
}

void show_local_pwd(void)
{
  char cwd[4096];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    printf("%s\n", cwd);
  }
}

void change_local_dir(char* path)
{
  if (chdir(path))
    printf("ERROR: Failed to change directory (%s)!\n", path);
}

int show_directory(char* path)
{
  llist* lst_dirents = llist_new();
  char* searchterm = NULL;

  int retVal = 0;

  do {
    if (!file_system_found)
      open_file_system();
    if (!file_system_found) {
      fprintf(stderr, "ERROR: Could not open file system.\n");
      retVal = -1;
      break;
    }

    // check if it's an absolute path to a folder
    if (fat_opendir(path) == 0) {
      // if so read direntries within it
      if (!read_direntries(lst_dirents, path))
        break;
    }
    // if not abs-path, then assume it's a file/dir/wildcard for the current working directory
    else {
      if (!read_direntries(lst_dirents, current_dir))
        break;

      // check if the user wants to 'dir' a sub-folder
      if (contains_dir(lst_dirents, path)) {
        llist_free(lst_dirents);
        lst_dirents = llist_new();

        if (!read_direntries(lst_dirents, path))
          break;
      }
      else if (strcmp(path, current_dir) != 0)
        searchterm = path;
    }

    llist* cur = lst_dirents;
    while (cur != NULL) {
      struct m65dirent* itm = (struct m65dirent*)cur->item;

      if (searchterm && !is_match(itm->d_name, searchterm, 1)) {
        cur = cur->next;
        continue;
      }

      if (itm->d_attr & 0x10)
        printf("       <DIR> %s\n", itm->d_name);
      else if (itm->d_name[0] && itm->d_filelen >= 0)
        printf("%12d %s\n", (int)itm->d_filelen, itm->d_name);
      cur = cur->next;
    }
  } while (0);

  llist_free(lst_dirents);

  return retVal;
}

int check_file_system_access(void)
{
  int retVal = 0;

  if (!file_system_found)
    open_file_system();
  if (!file_system_found) {
    fprintf(stderr, "ERROR: Could not open file system.\n");
    retVal = -1;
  }
  return retVal;
}

int read_int32_from_offset_in_buffer(int offset)
{
  int val = (dir_sector_buffer[offset] << 0) | (dir_sector_buffer[offset + 1] << 8) | (dir_sector_buffer[offset + 2] << 16)
          | (dir_sector_buffer[offset + 3] << 24);

  return val;
}

void show_clustermap(void)
{
  int clustermap_end = clustermap_start + clustermap_count;
  int previous_clustermap_sector = 0;
  int abs_fat1_sector = partition_start + fat1_sector;

  for (int clustermap_idx = clustermap_start; clustermap_idx < clustermap_end; clustermap_idx++) {
    int clustermap_sector = abs_fat1_sector + (clustermap_idx * 4) / 512;
    int clustermap_offset = (clustermap_idx * 4) % 512;

    // printf("clustermap_sector = %d\nclustermap_offset=%d\n", clustermap_sector, clustermap_offset);

    // do we need to read in the next sector?
    if (clustermap_sector != previous_clustermap_sector) {
      int retVal = read_sector(clustermap_sector, dir_sector_buffer, CACHE_YES, 0);
      if (retVal) {
        fprintf(stderr, "Failed to read next sector(%d)\n", clustermap_sector);
        return;
      }
      previous_clustermap_sector = clustermap_sector;
    }

    int clustermap_val = read_int32_from_offset_in_buffer(clustermap_offset);
    clustermap_val &= 0x0fffffff; // map out flags in top 4 bits
    printf("%d:  %d  ($%08X)\n", clustermap_idx, clustermap_val, clustermap_val);
  }
}

void show_cluster(void)
{
  char str[50];
  int abs_cluster2_sector = partition_start + first_cluster_sector + (cluster_num - 2) * sectors_per_cluster;

  for (int idx = 0; idx < sectors_per_cluster; idx++) {
    read_sector(abs_cluster2_sector + idx, dir_sector_buffer, CACHE_YES, 0);
    sprintf(str, "Sector %d:\n", abs_cluster2_sector + idx);
    dump_bytes(0, str, dir_sector_buffer, 512);
  }
}

void dump_sectors(void)
{
  FILE* fsave = fopen(secdump_file, "wb");
  for (int sector = secdump_start; sector < (secdump_start + secdump_count); sector++) {
    read_sector(sector, dir_sector_buffer, CACHE_YES, 0);
    fwrite(dir_sector_buffer, 1, 512, fsave);
    printf("\rSaving... (%d%%)", (sector - secdump_start) * 100 / secdump_count);
  }
  fclose(fsave);
  printf("\rSaved to file \"%s\".         \n", secdump_file);
}

void restore_sectors(void)
{
  struct stat st;
  stat(secrestore_file, &st);
  int secrestore_count = st.st_size / 512;

  FILE* fload = fopen(secrestore_file, "rb");
  for (int sector = secrestore_start; sector < (secrestore_start + secrestore_count); sector++) {
    fread(dir_sector_buffer, 1, 512, fload);
    write_sector(sector, dir_sector_buffer);
    printf("\rLoading... (%d%%)", (sector - secrestore_start) * 100 / secrestore_count);
  }
  fclose(fload);
  printf("\rLoaded file \"%s\" at starting-sector %d.\n", secrestore_file, secrestore_start);
}

void poke_sector(void)
{
  read_sector(poke_secnum, dir_sector_buffer, CACHE_NO, 0);
  dir_sector_buffer[poke_offset] = poke_value;
  write_sector(poke_secnum, dir_sector_buffer);
  // Flush any pending sector writes out
  execute_write_queue();
}

int endswith(char* fname, char* ext)
{
  char* actual_ext = strrchr(fname, '.');
  if (!ext)
    return 0;

  if (strcmp(ext, actual_ext) == 0)
    return 1;

  return 0;
}

void perform_filehost_read(char* searchterm)
{
  if (username != NULL) {
    log_in_and_get_cookie(username, password);
  }

  read_filehost_struct(searchterm);
}

int get_first_sector_of_file(char* name)
{
  struct m65dirent de;

  if (!safe_open_dir())
    return -1;

  if (!find_file_in_curdir(name, &de)) {
    printf("?  FILE NOT FOUND ERROR FOR \"%s\"\n", name);
    return -1;
  }

  unsigned int cluster_num = calc_first_cluster_of_file();
  int abs_cluster2_sector = partition_start + first_cluster_sector + (cluster_num - 2) * sectors_per_cluster;
  return abs_cluster2_sector;
}

void list_all_roms(void)
{
  llist* lst_dirents = llist_new();
  char* searchterm = "MEGA6*.ROM";

  do {
    if (!file_system_found)
      open_file_system();
    if (!file_system_found) {
      fprintf(stderr, "ERROR: Could not open file system.\n");
      break;
    }

    if (!read_direntries(lst_dirents, "/"))
      break;

    llist* cur = lst_dirents;
    while (cur != NULL) {
      struct m65dirent* itm = (struct m65dirent*)cur->item;

      if (searchterm && !is_match(itm->d_name, searchterm, 1)) {
        cur = cur->next;
        continue;
      }

      int sector_number = get_first_sector_of_file(itm->d_name);

      if (sector_number == -1)
        break;

      if (read_sector(sector_number, show_buf, CACHE_YES, 128)) {
        printf("ERROR: Failed to read to sector %d\n", sector_number);
        break;
      }

      char version[64] = "???";

      // closed-rom?
      if (show_buf[0x16] == 'V') {
        strncpy(version, (char*)show_buf + 0x16, 7);
        version[7] = '\0';
      }
      // open-rom
      else if (show_buf[0x10] == 'O' || show_buf[0x10] == 'V') {
        strncpy(version, (char*)show_buf + 0x10, 9);
        version[9] = '\0';
      }

      if (itm->d_name[0] && itm->d_filelen >= 0)
        printf("%11s - %s\n", itm->d_name, version);
      cur = cur->next;
    }
  } while (0);

  llist_free(lst_dirents);
}

void wrap_upload(char* fname)
{
  char* d81name = create_d81_for_prg(fname);
  strcpy(fname, d81name);

  if (fname) {
    upload_file(fname, fname);
  }
  else {
    printf("ERROR: Unable to download file from filehost!\n");
  }
}

void perform_filehost_get(int num, char* destname)
{
  char* fname = download_file_from_filehost(num);

  if (endswith(fname, ".prg") || endswith(fname, ".PRG")) {
    char* d81name = create_d81_for_prg(fname);
    strcpy(fname, d81name);
  }

  if (fname) {
    if (destname)
      upload_file(fname, destname);
    else
      upload_file(fname, fname);
  }
  else {
    printf("ERROR: Unable to download file from filehost!\n");
  }
}

// borrowed from mega65-core's "megaflash.c"
// - should probably be in some common library that can be
//   used on the m65 and pc side.
typedef struct {
  int model_id;
  char* name;
  int slot_size; // in MB
  int slot_count;
  char fpga_part[16];
  char qspi_part[32];
} models_type;

// clang-format off
models_type models[] = {
  // model_id   name                     slot_size(MB)   slot_count  fpga_part        qspi_part
    { 0x01,   "MEGA65 R1",                      4,        4,        "xc7a200t_0", "s25fl128sxxxxxx0-spi-x1_x2_x4" },
    { 0x02,   "MEGA65 R2",                      4,        8,        "xc7a100t_0", "s25fl256sxxxxxx0-spi-x1_x2_x4" },
    { 0x03,   "MEGA65 R3",                      8,        4,        "xc7a200t_0", "s25fl256sxxxxxx0-spi-x1_x2_x4" },
    { 0x21,   "MEGAphone R1",                   4,        4,        "xc7a200t_0", "s25fl128sxxxxxx0-spi-x1_x2_x4" },
    { 0x40,   "Nexys4 PSRAM",                   4,        4,        "xc7a100t_0", "s25fl128sxxxxxx0-spi-x1_x2_x4" },
    { 0x41,   "Nexys4DDR",                      4,        4,        "xc7a100t_0", "s25fl128sxxxxxx0-spi-x1_x2_x4" },
    { 0x42,   "Nexys4DDR with widget board",    4,        4,        "xc7a100t_0", "s25fl128sxxxxxx0-spi-x1_x2_x4" },
    { 0xFD,   "QMTECH Wukong A100T board",      4,        4,        "xc7a100t_0", "s25fl128sxxxxxx0-spi-x1_x2_x4" },
    { 0xFE,   "Simulation",                     0,        0,        "",             "" },
    { 0xFF,   "? unknown ?",                    0,        0,        "",             "" }
};
// clang-format on

models_type* get_model(uint8_t model_id)
{
  uint8_t k;
  uint8_t l = sizeof(models) / sizeof(models_type);

  for (k = 0; k < l; k++) {
    if (model_id == models[k].model_id) {
      return &models[k];
    }
  }

  return &models[l - 1]; // the last item is '? unknown ?'
}

int get_model_id_from_core_file(char* corefile)
{
  FILE* f = fopen(corefile, "rb");

  unsigned char buffer[512];
  bzero(buffer, 512);
  int bytes = fread(buffer, 1, 512, f);

  fclose(f);

  if (bytes > 0x70)
    return buffer[0x70];

  return 0;
}

int check_model_id_field(char* corefile)
{
  uint8_t hardware_model_id = mega65_peek(0xFFD3629);
  uint8_t core_model_id;

  core_model_id = get_model_id_from_core_file(corefile);

  printf(".COR file model id: $%02X - %s\n", core_model_id, get_model(core_model_id)->name);
  printf(" Hardware model id: $%02X - %s\n\n", hardware_model_id, get_model(hardware_model_id)->name);

  if (hardware_model_id == core_model_id) {
    printf("Verified .COR file matches hardware.\n"
           "Safe to flash.\n");
    return 1;
  }

  if (core_model_id == 0x00) {
    printf(".COR file is missing model-id field.\n"
           "Cannot confirm if .COR matches hardware.\n"
           "Are you sure you want to flash? (y/n)\n\n");

    char inp[10];
    scanf("%s", inp);
    if (strcmp(inp, "y") != 0)
      return 0;

    printf("Ok, will proceed to flash\n");
    return 1;
  }

  printf("Verification error!\n"
         "This .COR file is not intended for this hardware.\n");
  return 0;
}

void write_tcl_script(models_type* mdl)
{
  char fpga[128];
  char qspi[128];
  char hwcfg[128];
  char hwcfgtype[128];
  sprintf(fpga, "[lindex [get_hw_devices %s] 0]", mdl->fpga_part);
  sprintf(qspi, "[lindex [get_cfgmem_parts {%s}] 0]", mdl->qspi_part);
  sprintf(hwcfg, "[ get_property PROGRAM.HW_CFGMEM %s]", fpga);
  sprintf(hwcfgtype, "[ get_property PROGRAM.HW_CFGMEM_TYPE %s]", fpga);

  FILE* f = fopen("write-flash.tcl", "wt");
  fprintf(f, "open_hw\n");
  fprintf(f, "connect_hw_server\n");
  fprintf(f, "open_hw_target\n");
  fprintf(f, "create_hw_cfgmem -hw_device %s %s\n", fpga, qspi);
  fprintf(f, "set_property PROGRAM.BLANK_CHECK  0 %s\n", hwcfg);
  fprintf(f, "set_property PROGRAM.ERASE  1 %s\n", hwcfg);
  fprintf(f, "set_property PROGRAM.CFG_PROGRAM  1 %s\n", hwcfg);
  fprintf(f, "set_property PROGRAM.VERIFY  1 %s\n", hwcfg);
  fprintf(f, "set_property PROGRAM.CHECKSUM  0 %s\n", hwcfg);
  fprintf(f, "refresh_hw_device %s\n", fpga);
  fprintf(f, "set_property PROGRAM.ADDRESS_RANGE  {use_file} %s\n", hwcfg);
  fprintf(f, "set_property PROGRAM.FILES [list \"out.mcs\" ] %s\n", hwcfg);
  fprintf(f, "set_property PROGRAM.PRM_FILE {} %s\n", hwcfg);
  fprintf(f, "set_property PROGRAM.UNUSED_PIN_TERMINATION {pull-none} %s\n", hwcfg);
  fprintf(f, "set_property PROGRAM.BLANK_CHECK  0 %s\n", hwcfg);
  fprintf(f, "set_property PROGRAM.ERASE  1 %s\n", hwcfg);
  fprintf(f, "set_property PROGRAM.CFG_PROGRAM  1 %s\n", hwcfg);
  fprintf(f, "set_property PROGRAM.VERIFY  1 %s\n", hwcfg);
  fprintf(f, "set_property PROGRAM.CHECKSUM  0 %s\n", hwcfg);
  fprintf(f, "startgroup\n");
  fprintf(f,
      "if {![string equal %s [get_property MEM_TYPE [get_property CFGMEM_PART %s]]] }  { create_hw_bitstream -hw_device %s "
      "[get_property PROGRAM.HW_CFGMEM_BITFILE %s]; program_hw_devices %s; };\n",
      hwcfgtype, hwcfg, fpga, fpga, fpga);
  fprintf(f, "program_hw_cfgmem -hw_cfgmem %s\n", hwcfg);
  fprintf(f, "endgroup\n");
  fprintf(f, "quit\n");
  fclose(f);
}

int bit2mcs(int argc, char* argv[]);

BOOL initial_flashing_checks(void)
{
  // assess if running in xemu. If so, exit
  if (xemu_flag) {
    printf("%d - This command is not available for xemu.\n", xemu_flag);
    return FALSE;
  }

  // Query m65-hardware to learn what the m65model type is
  // Based on model-type, assess if slotnum is valid
  uint8_t hardware_model_id = mega65_peek(0xFFD3629);
  models_type* mdl = get_model(hardware_model_id);
  if (slotnum >= mdl->slot_count) {
    printf("- Valid slots on your hardware range from 0 to %d.\n", mdl->slot_count - 1);
    return FALSE;
  }

  return TRUE;
}

void flash_core_to_slot(char* fname, int slotnum)
{
  uint8_t hardware_model_id = mega65_peek(0xFFD3629);
  models_type* mdl = get_model(hardware_model_id);

  // Assure this is a .cor file
  if (stricmp(get_file_extension(fname), ".cor") != 0) {
    printf("- This file does not have '.cor' file extension!\n");
    return;
  }

  // If hardware model-id doesn't match .cor file's model-id, then don't permit
  if (!check_model_id_field(fname))
    return;

  // If we got here, all is good, so convert .cor to .mcs via bit2mcs tool
  char offset[16];
  sprintf(offset, "%08X", slotnum * mdl->slot_size * BYTES_PER_MB);
  printf("Creating 'out.mcs' at offset: %s...\n", offset);
  char* argv[4] = { "progname", fname, "out.mcs", offset };
  bit2mcs(4, argv);

  printf("Creating 'write-flash.tcl'...\n");
  // Then trigger the *-write-flash.tcl script to be run via vivado
  // (pointing it to our new 'out.mcs' file)
  write_tcl_script(mdl);

  // assess vivado path
  char vivado_path[512] = "C:\\Xilinx\\Vivado\\2021.1\\bin\\vivado";
  if (getenv("VIVADO_PATH") == NULL) {
    printf("- Env-var VIVADO_PATH not set, using default location of \"%s\"...\n", vivado_path);
  }
  else {
    strcpy(vivado_path, getenv("VIVADO_PATH"));
  }

  printf("Running 'write-flash.tcl' via Vivado... (use VIVADO_PATH env-var to specify path)\n");
  // NOTE: Consider permitting users to set a VIVADO_PATH to alter the default path that mega65_ftp will try use.
  char vivado_cmd[512];
  sprintf(vivado_cmd, "%s -mode batch -nojournal -nolog -notrace -source write-flash.tcl", vivado_path);
  system(vivado_cmd);

  // After flashing completes, remind user to power cycle their hardware
  printf("\nIf all went well, \"%s\" has been flashed to slot %d.\n\n"
         "Please power cycle your device.\n\n"
         "The 'mega65_ftp' tool will exit now. To start another session, re-run after power-cycling.\n\n",
      fname, slotnum);

  // exit mega65_ftp (as a new session will need to be created after the power-cycle anyway)
  exit(0);
}

void perform_flash(char* fname, int slotnum)
{
  if (!initial_flashing_checks())
    return;

  flash_core_to_slot(fname, slotnum);
}

void perform_filehost_flash(int fhnum, int slotnum)
{
  if (!initial_flashing_checks())
    return;

  // grab the file from the filehost
  char* fname = download_file_from_filehost(fhnum);

  flash_core_to_slot(fname, slotnum);

}

void show_secinfo(void)
{
  int abs_fat1_sector = partition_start + fat1_sector;
  int abs_fat2_sector = partition_start + fat2_sector;
  int abs_cluster2_sector = partition_start + first_cluster_sector;

  if (!file_system_found)
    open_file_system();
  printf("\n");
  printf("  SECTOR : CONTENT\n");
  printf("  ------   -------\n");
  printf("% 8d : MBR (Master Boot Record)\n", 0);
  printf("% 8d : VBR of 1st Partition\n", partition_start);
  printf("% 8d : 1st FAT (cluster-chain map)\n", abs_fat1_sector);
  printf("% 8d : 2nd FAT (backup)\n", abs_fat2_sector);
  printf("% 8d : cluster #2 (root-directory table)\n", abs_cluster2_sector);
  printf("\n");
}

void show_vbrinfo(void)
{
  unsigned char sector[512];
  if (!file_system_found)
    open_file_system();
  if (read_sector(partition_start, sector, CACHE_YES, 0)) {
    printf("Failed to read sector %d...\n", partition_start);
    return;
  }

  printf("OEM Name=\"%c%c%c%c%c%c%c%c\"\n", sector[0x03], sector[0x04], sector[0x05], sector[0x06], sector[0x07],
      sector[0x08], sector[0x09], sector[0x0A]);
  printf("FAT32 Extended BIOS Parameter Block:\n");
  printf("{\n");
  printf("  DOS 3.31 BPB\n");
  printf("  {\n");
  printf("    DOS 2.0 BPB\n");
  printf("    {\n");
  printf("      Bytes per logical sector = %d\n", *(unsigned short*)&sector[0x0B]);
  printf("      Logical sectors per cluster = %d\n", sector[0x0D]);
  printf("      Count of reserved logical sectors before 1st FAT = %d\n", *(unsigned short*)&sector[0x0E]);
  printf("      Number of FATs = %d\n", sector[0x10]);
  printf("      Max no# of FAT12/16 root dir entries = %d\n", *(unsigned short*)&sector[0x11]);
  printf("      Total logical sector (0 for FAT32) = %d\n", *(unsigned short*)&sector[0x13]);
  printf("      Media Descriptor = 0x%02X\n", sector[0x15]);
  printf("      Logical sectors per FAT (0 for FAT32) = %d\n", *(unsigned short*)&sector[0x16]);
  printf("    }\n");
  printf("    Physical sectors per track (for INT 13h CHS geometry) = %d\n", *(unsigned short*)&sector[0x18]);
  printf("    Number of heads (for disks with INT 13h CHS geometry) = %d\n", *(unsigned short*)&sector[0x1A]);
  printf("    Count of hidden sectors preceding the partition of this FAT volume = %d\n", *(unsigned int*)&sector[0x1C]);
  printf("    Total logical sectors (if greater than 65535) = %d\n", *(unsigned int*)&sector[0x20]);
  printf("  }\n");
  printf("  Logical sectors per FAT = %d\n", *(unsigned int*)&sector[0x24]);
  printf("  Drive description / mirroring flags = 0x%02X 0x%02X\n", sector[0x28], sector[0x29]);
  printf("  Version = 0x%02X 0x%02X\n", sector[0x2A], sector[0x2B]);
  printf("  Cluster number of root directory start = %d\n", *(unsigned int*)&sector[0x2C]);
  printf("  Logical sector number of FS Information Sector = %d\n", *(unsigned short*)&sector[0x30]);
  printf("  First logical sector number of copy of 3 FAT boot sectors = %d\n", *(unsigned short*)&sector[0x32]);
  printf("  Cf. 0x024 for FAT12/FAT16 (Physical Drive Number) = 0x%02X\n", sector[0x40]);
  printf("  Cf. 0x025 for FAT12/FAT16 (Used for various purposes; see FAT12/FAT16) = 0x%02X\n", sector[0x41]);
  printf("  Cf. 0x026 for FAT12/FAT16 (Extended boot signature, 0x29) = 0x%02X\n", sector[0x42]);
  printf("  Cf. 0x027 for FAT12/FAT16 (Volume ID) = %02X %02X %02X %02X\n", sector[0x43], sector[0x44], sector[0x45],
      sector[0x46]);
  printf("  Cf. 0x02B for FAT12/FAT16 (Volume Label) = %c%c%c%c%c%c%c%c%c%c%c\n", sector[0x47], sector[0x48], sector[0x49],
      sector[0x4a], sector[0x4b], sector[0x4c], sector[0x4d], sector[0x4e], sector[0x4f], sector[0x50], sector[0x51]);
  printf("  Cf. 0x036 for FAT12/FAT16 (File system type) = %c%c%c%c%c%c%c%c\n", sector[0x52], sector[0x53], sector[0x54],
      sector[0x55], sector[0x56], sector[0x57], sector[0x58], sector[0x59]);
  printf("}\n");

  printf("\n");
}

void show_mbrinfo(void)
{
  unsigned char sector[512];
  if (!file_system_found)
    open_file_system();
  if (read_sector(0, sector, CACHE_YES, 0)) {
    printf("Failed to read sector 0...\n");
    return;
  }

  int pt_ofs = 0x1be;
  int part_cnt = 0;

  for (int part_id = 0; part_id < 4; part_id++) {
    if (sector[pt_ofs + 0x04] == 0x00) // is partition-type 0? (unused)
      continue;

    printf("Partition %d\n", part_id);
    printf("-----------\n");
    if (sector[pt_ofs] == 0x80)
      printf("- active/bootable partition\n");
    printf("- Partition type = 0x%02X\n", sector[pt_ofs + 0x04]);
    int c = sector[pt_ofs + 0x03] + ((sector[pt_ofs + 0x02] & 0xC0) << 2);
    int h = sector[pt_ofs + 0x02] & 0x3F;
    int s = sector[pt_ofs + 0x01];
    printf("- First sector chs: cylinder = %d, head = %d, sector = %d\n", c, h, s);
    c = sector[pt_ofs + 0x07] + ((sector[pt_ofs + 0x06] & 0xC0) << 2);
    h = sector[pt_ofs + 0x06] & 0x3F;
    s = sector[pt_ofs + 0x05];
    printf("- Last sector chs: cylinder = %d, head = %d, sector = %d\n", c, h, s);
    printf("- First sector LBA: %d\n", *(unsigned int*)&sector[pt_ofs + 0x08]);
    printf("- Number of sectors: %d\n", *(unsigned int*)&sector[pt_ofs + 0x0C]);

    part_cnt++;
    printf("\n");

    pt_ofs += 16;
  }
  if (part_cnt == 0)
    printf("No partitions found!\n\n");
}

unsigned int get_first_cluster_of_file(void)
{
  return (dir_sector_buffer[dir_sector_offset + 0x1A] << 0) | (dir_sector_buffer[dir_sector_offset + 0x1B] << 8)
       | (dir_sector_buffer[dir_sector_offset + 0x14] << 16) | (dir_sector_buffer[dir_sector_offset + 0x15] << 24);
}

int delete_file(char* name)
{
  struct m65dirent de;

  if (check_file_system_access() == -1)
    return -1;

  if (fat_opendir(current_dir))
    return -1;

  if (!find_file_in_curdir(name, &de)) {
    printf("File %s does not exist.\n", name);
    return -1;
  }

  unsigned int first_cluster_of_file = get_first_cluster_of_file();

  // remove entry from cluster#2
  bzero(&dir_sector_buffer[dir_sector_offset], 32);
  if (write_sector(partition_start + dir_sector, dir_sector_buffer)) {
    printf("Failed to write updated directory sector.\n");
    return -1;
  }

  // remove cluster-chain from cluster-map in FAT tables #1 and #2
  unsigned int current_cluster = first_cluster_of_file;
  unsigned int next_cluster = 0;
  do {
    next_cluster = chained_cluster(current_cluster);
    if (0) {
      printf("current_cluster=0x%08X\n", current_cluster);
      printf("next_cluster=0x%08X\n", next_cluster);
    }
    deallocate_cluster(current_cluster);
    current_cluster = next_cluster;
  } while (current_cluster > 0 && current_cluster < 0xffffff8);

  // Flush any pending sector writes out
  execute_write_queue();

  printf("File '%s' successfully deleted\n", name);
  return 0;
}

// returns:
// -1 = problems opening file-system
// 0 = doesn't exist
// 1 = exists
int contains_file(char* name)
{
  struct m65dirent de;

  if (check_file_system_access() == -1)
    return -1;

  if (fat_opendir(current_dir))
    return -1;

  // printf("Opened directory, dir_sector=%d (absolute sector = %d)\n",dir_sector,partition_start+dir_sector);
  while (!fat_readdir(&de)) {
    // if (de.d_name[0]) printf("'%s'   %d\n",de.d_name,(int)de.d_filelen);
    // else dump_bytes(0,"empty dirent",&dir_sector_buffer[dir_sector_offset],32);
    if (!strcasecmp(de.d_name, name)) {
      // Found file, so will replace it
      // printf("Found \"%s\" on the file system, beginning at cluster %d\n", name, (int)de.d_ino);
      return 1;
    }
  }
  // if dir-sector = -1, that means we got to the end of the directory entries without finding a match
  if (dir_sector == -1)
    return 0;

  return 0;
}

int rename_file(char* name, char* dest_name)
{
  int retVal = 0;
  do {

    if (!contains_file(name)) {
      printf("ERROR: File %s does not exist.\n", name);
      return -1;
    }

    if (contains_file(dest_name) == 1) {
      printf("ERROR: Cannot rename to \"%s\", as this file already exists.\n", dest_name);
      return -2;
    }

    // need to call this again to set various global variable details for the found file properly
    contains_file(name);

    // Write name
    for (int i = 0; i < 11; i++)
      dir_sector_buffer[dir_sector_offset + i] = 0x20;
    for (int i = 0; i < 9; i++)
      if (dest_name[i] == '.') {
        // Write out extension
        for (int j = 0; j < 3; j++)
          if (dest_name[i + 1 + j])
            dir_sector_buffer[dir_sector_offset + 8 + j] = dest_name[i + 1 + j];
        break;
      }
      else if (!dest_name[i])
        break;
      else
        dir_sector_buffer[dir_sector_offset + i] = dest_name[i];

    // Write modified directory entry back to disk
    if (write_sector(partition_start + dir_sector, dir_sector_buffer)) {
      printf("Failed to write updated directory sector.\n");
      retVal = -1;
      break;
    }

    // Flush any pending sector writes out
    execute_write_queue();

  } while (0);

  return retVal;
}

// #ifdef USE_LFN
// returns: 0 = doesn't need long name, 1 = needs long name
int normalise_long_name(char* long_name, char* short_name, char* dir_name)
{
  struct m65dirent de;
  int base_len = 0;
  int ext_len = 0;
  int dot_count = 0;
  int needs_long_name = 0;

  short_name[8 + 3] = 0;

  // Handle . and .. special cases
  if (!strcmp(long_name, ".")) {
    bcopy(".          ", short_name, 8 + 3);
    return 0;
  }
  if (!strcmp(long_name, "..")) {
    bcopy("..         ", short_name, 8 + 3);
    return 0;
  }

  for (int i = 0; long_name[i]; i++) {
    if (long_name[i] == '.') {
      dot_count++;
      if (dot_count > 1)
        needs_long_name = 1;
    }
    else {
      if (toupper(long_name[i]) != long_name[i])
        needs_long_name = 1;
      if (dot_count == 0) {
        if (base_len < 8) {
          short_name[base_len] = toupper(long_name[i]);
        }
        else {
          needs_long_name = 1;
        }
      }
      else if (dot_count == 1) {
        if (ext_len < 3) {
          short_name[8 + ext_len] = toupper(long_name[i]);
        }
        else {
          needs_long_name = 1;
        }
      }
    }
  }

  if (needs_long_name) {
    // Put ~X suffix on base name.
    // XXX Needs to be unique in the sub-directory
    for (int i = 1; i <= 99999; i++) {
      int length_of_number = 5;
      if (i < 10000)
        length_of_number = 4;
      if (i < 1000)
        length_of_number = 3;
      if (i < 100)
        length_of_number = 2;
      if (i < 10)
        length_of_number = 1;
      int ofs = 7 - length_of_number;
      char temp[9];
      snprintf(temp, 8 - ofs, "~%d", i);
      bcopy(temp, &short_name[ofs], 8 - ofs);
      printf("  considering short-name '%s'...\n", short_name);
      if (fat_opendir(dir_name)) {
        fprintf(stderr, "ERROR: Could not open directory '%s' to check for LFN uniqueness.\n", dir_name);
        // So just assume its unique
        break;
      }
      else {
        // iterate through the directory looking for
        while (!fat_readdir(&de)) {
          // Compare short name with each directory entry.
          // XXX We have the non-dotted version to compare against,
          // so maybe we should just do direct sector reading and examination,
          // rather than calling fat_readdir(). Else we can make fat_readdir()
          // store the unmodified short-name (and eventually long-name)?
          fprintf(stderr, "ERROR: Not implemented.\n");
          return -1;
          // exit(-1);
        }
      }
    }
  }

  return needs_long_name;
}

unsigned char lfn_checksum(const unsigned char* pFCBName)
{
  int i;
  unsigned char sum = 0;

  for (i = 11; i; i--)
    sum = ((sum & 1) << 7) + (sum >> 1) + *pFCBName++;

  return sum;
}
// #endif

int upload_single_file(char* name, char* dest_name)
{
  struct m65dirent de;
  int retVal = 0;
  do {

    // #ifdef USE_LFN
    char short_name[8 + 3 + 1];

    // Normalise dest_name into 8.3 format.
    normalise_long_name(dest_name, short_name, current_dir);

    // Calculate checksum of 8.3 name
    unsigned char lfn_csum = lfn_checksum((unsigned char*)short_name);
    // #endif

    time_t upload_start = time(0);

    struct stat st;
    if (stat(name, &st)) {
      fprintf(stderr, "ERROR: Could not stat file '%s'\n", name);
      perror("stat() failed");
      return -1;
    }
    //    printf("File '%s' is %ld bytes long.\n",name,(long)st.st_size);

    if (!safe_open_dir())
      return -1;

    if (!find_file_in_curdir(dest_name, &de)) {
      // File does not (yet) exist, get ready to create it
      printf("%s does not yet exist on the file system -- searching for empty directory slot to create it in.\n", dest_name);

      if (fat_opendir(current_dir)) {
        retVal = -1;
        break;
      }
      struct m65dirent de;
      while (!fat_readdir(&de)) {
        if (!de.d_name[0] && de.d_type == M65DT_FREESLOT) {
          if (0)
            printf("Found empty slot at dir_sector=%d, dir_sector_offset=%d\n", dir_sector, dir_sector_offset);

          if (!create_directory_entry_for_file(dest_name))
            return -1;

          // Stop looking for an empty directory entry slot
          break;
        }
      }
    }

    if (dir_sector == -1) {
      printf("ERROR: Directory is full.  Request support for extending directory into multiple clusters.\n");
      retVal = -1;
      break;
    }
    else {
      //      printf("Directory entry is at offset $%03x of sector $%x\n",dir_sector_offset,dir_sector);
    }

    // Read out the first cluster. If zero, then we need to allocate a first cluster.
    // After that, we can allocate and chain clusters in a constant manner
    unsigned int first_cluster_of_file = calc_first_cluster_of_file();

    if (!first_cluster_of_file) {
      //      printf("File currently has no first cluster allocated.\n");

      int a_cluster = find_contiguous_clusters(st.st_size / (512 * sectors_per_cluster));

      if (!a_cluster) {
        printf("ERROR: Failed to find a free cluster.\n");
        retVal = -1;
        break;
      }
      if (allocate_cluster(a_cluster)) {
        printf("ERROR: Could not allocate cluster $%x\n", a_cluster);
        retVal = -1;
        break;
      }

      // Write cluster number into directory entry
      dir_sector_buffer[dir_sector_offset + 0x1A] = (a_cluster >> 0) & 0xff;
      dir_sector_buffer[dir_sector_offset + 0x1B] = (a_cluster >> 8) & 0xff;
      dir_sector_buffer[dir_sector_offset + 0x14] = (a_cluster >> 16) & 0xff;
      dir_sector_buffer[dir_sector_offset + 0x15] = (a_cluster >> 24) & 0xff;

      if (write_sector(partition_start + dir_sector, dir_sector_buffer)) {
        printf("ERROR: Failed to write updated directory sector after allocating first cluster.\n");
        retVal = -1;
        break;
      }

      first_cluster_of_file = a_cluster;
    } // else printf("First cluster of file is $%x\n",first_cluster_of_file);

    // Now write the file out sector by sector, and allocate new clusters as required
    int remaining_length = st.st_size;
    int sector_in_cluster = 0;
    int file_cluster = first_cluster_of_file;
    unsigned int sector_number;
    FILE* f = fopen(name, "rb");

    if (!f) {
      printf("ERROR: Could not open file '%s' for reading.\n", name);
      retVal = -1;
      break;
    }

    while (remaining_length > 0) {
      if (sector_in_cluster >= sectors_per_cluster) {
        // Advance to next cluster
        // If we are currently the last cluster, then allocate a new one, and chain it in

        int next_cluster = chained_cluster(file_cluster);
        if (next_cluster == 0 || next_cluster >= 0xffffff8) {
          next_cluster = find_free_cluster(file_cluster);
          if (allocate_cluster(next_cluster)) {
            printf("ERROR: Could not allocate cluster $%x\n", next_cluster);
            retVal = -1;
            break;
          }
          if (chain_cluster(file_cluster, next_cluster)) {
            printf("ERROR: Could not chain cluster $%x to $%x\n", file_cluster, next_cluster);
            retVal = -1;
            break;
          }
        }
        if (!next_cluster) {
          printf("ERROR: Could not find a free cluster\n");
          retVal = -1;
          break;
        }

        file_cluster = next_cluster;
        sector_in_cluster = 0;
      }

      // Write sector
      unsigned char buffer[512];
      bzero(buffer, 512);
      int bytes = fread(buffer, 1, 512, f);
      sector_number = partition_start + first_cluster_sector + (sectors_per_cluster * (file_cluster - first_cluster))
                    + sector_in_cluster;
      if (0)
        printf("T+%lld : Read %d bytes from file, writing to sector $%x (%d) for cluster %d\n", gettime_us() - start_usec,
            bytes, sector_number, sector_number, file_cluster);
      printf("\rUploaded %lld bytes.", (long long)st.st_size - remaining_length);
      fflush(stdout);

      if (write_sector(sector_number, buffer)) {
        printf("ERROR: Failed to write to sector %d\n", sector_number);
        retVal = -1;
        break;
      }
      //      printf("T+%lld : after write.\n",gettime_us()-start_usec);

      sector_in_cluster++;
      remaining_length -= 512;
    }

    // XXX check for orphan clusters at the end, and if present, free them.

    // Write file size into directory entry
    dir_sector_buffer[dir_sector_offset + 0x1C] = (st.st_size >> 0) & 0xff;
    dir_sector_buffer[dir_sector_offset + 0x1D] = (st.st_size >> 8) & 0xff;
    dir_sector_buffer[dir_sector_offset + 0x1E] = (st.st_size >> 16) & 0xff;
    dir_sector_buffer[dir_sector_offset + 0x1F] = (st.st_size >> 24) & 0xff;

    if (write_sector(partition_start + dir_sector, dir_sector_buffer)) {
      printf("ERROR: Failed to write updated directory sector after updating file length.\n");
      retVal = -1;
      break;
    }

    // Flush any pending sector writes out
    execute_write_queue();

    if (time(0) == upload_start)
      upload_start = time(0) - 1;
    printf("\rUploaded %lld bytes in %lld seconds (%.1fKB/sec)\n", (long long)st.st_size, (long long)time(0) - upload_start,
        st.st_size * 1.0 / 1024 / (time(0) - upload_start));
  } while (0);

  return retVal;
}

int upload_file(char* name, char* dest_name)
{
  DIR* d;
  struct dirent* dir;

  // if no wildcards in name, then just upload a single file
  if (!strstr(name, "*"))
    return upload_single_file(name, dest_name);

  // check for wildcards in name
  // list directories first
  d = opendir(".");
  if (d) {
    while ((dir = readdir(d)) != NULL) {
      if (dir->d_name && !is_match(dir->d_name, name, 1))
        continue;

      struct stat file_stats;
      if (!stat(dir->d_name, &file_stats)) {
        if (!S_ISDIR(file_stats.st_mode))
          upload_single_file(dir->d_name, dir->d_name);
      }
    }
  }
  closedir(d);
  return 0;
}

// Must be a single path segment.  Creating sub-directories requires multiple chdir/cd + mkdir calls
int create_dir(char* dest_name)
{
  int parent_cluster = 2;
  struct m65dirent de;
  int retVal = 0;
  do {

    time_t upload_start = time(0);

    if (!file_system_found)
      open_file_system();
    if (!file_system_found) {
      fprintf(stderr, "ERROR: Could not open file system.\n");
      retVal = -1;
      break;
    }

    if (fat_opendir(current_dir)) {
      retVal = -1;
      break;
    }
    parent_cluster = dir_cluster;
    //    printf("Opened directory, dir_sector=%d (absolute sector = %d)\n",dir_sector,partition_start+dir_sector);
    while (!fat_readdir(&de)) {
      // if (de.d_name[0]) printf("%13s   %d\n",de.d_name,(int)de.d_filelen);
      //      else dump_bytes(0,"empty dirent",&dir_sector_buffer[dir_sector_offset],32);
      if (!strcasecmp(de.d_name, dest_name)) {
        // Name exists
        fprintf(stderr, "ERROR: File or directory '%s' already exists.\n", dest_name);
        retVal = -1;
        break;
      }
    }
    if (dir_sector == -1) {
      // File does not (yet) exist, get ready to create it
      printf("%s does not yet exist on the file system -- searching for empty directory slot to create it in.\n", dest_name);

      if (fat_opendir(current_dir)) {
        retVal = -1;
        break;
      }
      struct m65dirent de;
      while (!fat_readdir(&de)) {
        if (!de.d_name[0]) {
          if (0)
            printf("Found empty slot at dir_sector=%d, dir_sector_offset=%d\n", dir_sector, dir_sector_offset);

          // Create directory entry, and write sector back to SD card
          unsigned char dir[32];
          bzero(dir, 32);

          // Write name
          for (int i = 0; i < 11; i++)
            dir[i] = 0x20;
          for (int i = 0; i < 9; i++)
            if (dest_name[i] == '.') {
              // Write out extension
              for (int j = 0; j < 3; j++)
                if (dest_name[i + 1 + j])
                  dir[8 + j] = dest_name[i + 1 + j];
              break;
            }
            else if (!dest_name[i])
              break;
            else
              dir[i] = dest_name[i];

          // Set file attributes (only directory bit)
          dir[0xb] = 0x10;

          // Store create time and date
          time_t t = time(0);
          struct tm* tm = localtime(&t);
          dir[0xe] = (tm->tm_sec >> 1) & 0x1F; // 2 second resolution
          dir[0xe] |= (tm->tm_min & 0x7) << 5;
          dir[0xf] = (tm->tm_min & 0x3) >> 3;
          dir[0xf] |= (tm->tm_hour) << 2;
          dir[0x10] = tm->tm_mday & 0x1f;
          dir[0x10] |= ((tm->tm_mon + 1) & 0x7) << 5;
          dir[0x11] = ((tm->tm_mon + 1) & 0x1) >> 3;
          dir[0x11] |= (tm->tm_year - 80) << 1;

          //	  dump_bytes(0,"New directory entry",dir,32);

          // (Cluster and size we set after writing to the file)

          // Copy back into directory sector, and write it
          bcopy(dir, &dir_sector_buffer[dir_sector_offset], 32);
          if (write_sector(partition_start + dir_sector, dir_sector_buffer)) {
            printf("Failed to write updated directory sector.\n");
            retVal = -1;
            break;
          }

          break;
        }
      }
    }
    if (dir_sector == -1) {
      printf("ERROR: Directory is full.  Request support for extending directory into multiple clusters.\n");
      retVal = -1;
      break;
    }
    else {
      //      printf("Directory entry is at offset $%03x of sector $%x\n",dir_sector_offset,dir_sector);
    }

    // Read out the first cluster. If zero, then we need to allocate a first cluster.
    // After that, we can allocate and chain clusters in a constant manner
    unsigned int first_cluster_of_file = (dir_sector_buffer[dir_sector_offset + 0x1A] << 0)
                                       | (dir_sector_buffer[dir_sector_offset + 0x1B] << 8)
                                       | (dir_sector_buffer[dir_sector_offset + 0x14] << 16)
                                       | (dir_sector_buffer[dir_sector_offset + 0x15] << 24);
    if (!first_cluster_of_file) {
      //      printf("File currently has no first cluster allocated.\n");

      int a_cluster = find_free_cluster(0);
      if (!a_cluster) {
        printf("ERROR: Failed to find a free cluster.\n");
        retVal = -1;
        break;
      }
      if (allocate_cluster(a_cluster)) {
        printf("ERROR: Could not allocate cluster $%x\n", a_cluster);
        retVal = -1;
        break;
      }

      // Write cluster number into directory entry
      dir_sector_buffer[dir_sector_offset + 0x1A] = (a_cluster >> 0) & 0xff;
      dir_sector_buffer[dir_sector_offset + 0x1B] = (a_cluster >> 8) & 0xff;
      dir_sector_buffer[dir_sector_offset + 0x14] = (a_cluster >> 16) & 0xff;
      dir_sector_buffer[dir_sector_offset + 0x15] = (a_cluster >> 24) & 0xff;

      if (write_sector(partition_start + dir_sector, dir_sector_buffer)) {
        printf("ERROR: Failed to write updated directory sector after allocating first cluster.\n");
        retVal = -1;
        break;
      }

      first_cluster_of_file = a_cluster;
    } // else printf("First cluster of file is $%x\n",first_cluster_of_file);

    // Now write the file out sector by sector, and allocate new clusters as required
    int remaining_length = 4096; // XXX assumes 4KB clusters
    int sector_in_cluster = 0;
    int file_cluster = first_cluster_of_file;
    unsigned int sector_number;

    while (remaining_length > 0) {
      if (sector_in_cluster >= sectors_per_cluster) {
        // Advance to next cluster
        // If we are currently the last cluster, then allocate a new one, and chain it in

        int next_cluster = chained_cluster(file_cluster);
        if (next_cluster == 0 || next_cluster >= 0xffffff8) {
          next_cluster = find_free_cluster(file_cluster);
          if (allocate_cluster(next_cluster)) {
            printf("ERROR: Could not allocate cluster $%x\n", next_cluster);
            retVal = -1;
            break;
          }
          if (chain_cluster(file_cluster, next_cluster)) {
            printf("ERROR: Could not chain cluster $%x to $%x\n", file_cluster, next_cluster);
            retVal = -1;
            break;
          }
        }
        if (!next_cluster) {
          printf("ERROR: Could not find a free cluster\n");
          retVal = -1;
          break;
        }

        file_cluster = next_cluster;
        sector_in_cluster = 0;
      }

      // Write sector
      unsigned char buffer[512];
      bzero(buffer, 512);

      if (remaining_length == 4096) {
        // Build . and .. directory entries in first sector of directory

        time_t t = time(0);
        struct tm* tm = localtime(&t);

        for (int i = 0; i < 11; i++)
          buffer[0x00 + i] = ' ';
        buffer[0x00 + 0] = '.';
        buffer[0x00 + 0xb] = 0x10;                     // directory
        buffer[0x00 + 0xe] = (tm->tm_sec >> 1) & 0x1F; // 2 second resolution
        buffer[0x00 + 0xe] |= (tm->tm_min & 0x7) << 5;
        buffer[0x00 + 0xf] = (tm->tm_min & 0x3) >> 3;
        buffer[0x00 + 0xf] |= (tm->tm_hour) << 2;
        buffer[0x00 + 0x10] = tm->tm_mday & 0x1f;
        buffer[0x00 + 0x10] |= ((tm->tm_mon + 1) & 0x7) << 5;
        buffer[0x00 + 0x11] = ((tm->tm_mon + 1) & 0x1) >> 3;
        buffer[0x00 + 0x11] |= (tm->tm_year - 80) << 1;
        buffer[0x00 + 0x1A] = (first_cluster_of_file >> 0) & 0xff;
        buffer[0x00 + 0x1B] = (first_cluster_of_file >> 8) & 0xff;
        buffer[0x00 + 0x14] = (first_cluster_of_file >> 16) & 0xff;
        buffer[0x00 + 0x15] = (first_cluster_of_file >> 24) & 0xff;

        for (int i = 0; i < 11; i++)
          buffer[0x20 + i] = ' ';
        buffer[0x20 + 0] = '.';
        buffer[0x20 + 1] = '.';
        buffer[0x20 + 0xb] = 0x10;                     // directory
        buffer[0x20 + 0xe] = (tm->tm_sec >> 1) & 0x1F; // 2 second resolution
        buffer[0x20 + 0xe] |= (tm->tm_min & 0x7) << 5;
        buffer[0x20 + 0xf] = (tm->tm_min & 0x3) >> 3;
        buffer[0x20 + 0xf] |= (tm->tm_hour) << 2;
        buffer[0x20 + 0x10] = tm->tm_mday & 0x1f;
        buffer[0x20 + 0x10] |= ((tm->tm_mon + 1) & 0x7) << 5;
        buffer[0x20 + 0x11] = ((tm->tm_mon + 1) & 0x1) >> 3;
        buffer[0x20 + 0x11] |= (tm->tm_year - 80) << 1;
        buffer[0x20 + 0x1A] = (parent_cluster >> 0) & 0xff;
        buffer[0x20 + 0x1B] = (parent_cluster >> 8) & 0xff;
        buffer[0x20 + 0x14] = (parent_cluster >> 16) & 0xff;
        buffer[0x20 + 0x15] = (parent_cluster >> 24) & 0xff;
      }

      int bytes = 512;
      sector_number = partition_start + first_cluster_sector + (sectors_per_cluster * (file_cluster - first_cluster))
                    + sector_in_cluster;
      if (0)
        printf("T+%lld : Read %d bytes from file, writing to sector $%x (%d) for cluster %d\n", gettime_us() - start_usec,
            bytes, sector_number, sector_number, file_cluster);
      fflush(stdout);

      if (write_sector(sector_number, buffer)) {
        printf("ERROR: Failed to write to sector %d\n", sector_number);
        retVal = -1;
        break;
      }
      //      printf("T+%lld : after write.\n",gettime_us()-start_usec);

      sector_in_cluster++;
      remaining_length -= 512;
    }

    // XXX check for orphan clusters at the end, and if present, free them.

    // Write file size into directory entry
    dir_sector_buffer[dir_sector_offset + 0x1C] = (4096) & 0xff;
    dir_sector_buffer[dir_sector_offset + 0x1D] = (4096) & 0xff;
    dir_sector_buffer[dir_sector_offset + 0x1E] = (4096) & 0xff;
    dir_sector_buffer[dir_sector_offset + 0x1F] = (4096) & 0xff;

    if (write_sector(partition_start + dir_sector, dir_sector_buffer)) {
      printf("ERROR: Failed to write updated directory sector after updating file length.\n");
      retVal = -1;
      break;
    }

    // Flush any pending sector writes out
    execute_write_queue();

    if (time(0) == upload_start)
      upload_start = time(0) - 1;
    printf("\rUploaded %lld bytes in %lld seconds (%.1fKB/sec)\n", (long long)4096, (long long)time(0) - upload_start,
        4096 * 1.0 / 1024 / (time(0) - upload_start));
  } while (0);

  return retVal;
}

unsigned char download_buffer[512];

int download_slot(int slot_number, char* dest_name)
{
  int retVal = 0;
  do {

    if (!syspart_start) {
      printf("ERROR: No system partition detected.\n");
      retVal = -1;
      break;
    }

    if (slot_number < 0 || slot_number >= syspart_slot_count) {
      printf("ERROR: Invalid slot number (valid range is 0 -- %d)\n", syspart_slot_count);
      retVal = -1;
      break;
    }

    FILE* f = fopen(dest_name, "wb");
    if (!f) {
      printf("ERROR: Could not open file '%s' for writing\n", dest_name);
      retVal = -1;
      break;
    }
    printf("Saving slot %d into '%s'\n", slot_number, dest_name);

    for (int i = 0; i < syspart_slot_size; i++) {
      unsigned char sector[512];
      int sector_num = syspart_start + syspart_freeze_area + syspart_slotdir_sectors + slot_number * syspart_slot_size + i;
      if (!i)
        printf("Downloading %d sectors beginning at sector $%08x\n", syspart_slot_size, sector_num);
      if (read_sector(sector_num, sector, CACHE_YES, 64)) {
        printf("ERROR: Could not read sector %d/%d of freeze slot %d (absolute sector %d)\n", i, syspart_slot_size,
            slot_number, sector_num);
        retVal = -1;
        break;
      }
      fwrite(sector, 512, 1, f);
      printf(".");
      fflush(stdout);
    }
    fclose(f);
    printf("\n");

  } while (0);

  return retVal;
}

BOOL safe_open_dir(void)
{
  if (!file_system_found)
    open_file_system();
  if (!file_system_found) {
    fprintf(stderr, "ERROR: Could not open file system.\n");
    return FALSE;
  }

  if (fat_opendir(current_dir)) {
    return FALSE;
  }

  //    printf("Opened directory, dir_sector=%d (absolute sector = %d)\n",dir_sector,partition_start+dir_sector);

  return TRUE;
}

BOOL find_file_in_curdir(char* filename, struct m65dirent* de)
{
  while (!fat_readdir(de)) {
    // if (de->d_name[0]) printf("%13s   %d\n",de->d_name,(int)de->d_filelen);
    //      else dump_bytes(0,"empty dirent",&dir_sector_buffer[dir_sector_offset],32);
    if (!strcasecmp(de->d_name, filename)) {
      // Found file, so will replace it
      //	printf("%s already exists on the file system, beginning at cluster %d\n",name,(int)de->d_ino);
      return TRUE;
    }
  }
  return FALSE;
}

unsigned int calc_first_cluster_of_file(void)
{
  return (dir_sector_buffer[dir_sector_offset + 0x1A] << 0) | (dir_sector_buffer[dir_sector_offset + 0x1B] << 8)
       | (dir_sector_buffer[dir_sector_offset + 0x14] << 16) | (dir_sector_buffer[dir_sector_offset + 0x15] << 24);
}

BOOL create_directory_entry_for_file(char* filename)
{
  // Create directory entry, and write sector back to SD card
  unsigned char dir[32];
  bzero(dir, 32);

  // Write name
  for (int i = 0; i < 11; i++)
    dir[i] = 0x20;
  for (int i = 0; i < 9; i++)
    if (filename[i] == '.') {
      // Write out extension
      for (int j = 0; j < 3; j++)
        if (filename[i + 1 + j])
          dir[8 + j] = filename[i + 1 + j];
      break;
    }
    else if (!filename[i])
      break;
    else
      dir[i] = filename[i];

  // Set file attributes (only archive bit)
  dir[0xb] = 0x20;

  // Store create time and date
  time_t t = time(0);
  struct tm* tm = localtime(&t);
  dir[0xe] = (tm->tm_sec >> 1) & 0x1F; // 2 second resolution
  dir[0xe] |= (tm->tm_min & 0x7) << 5;
  dir[0xf] = (tm->tm_min & 0x3) >> 3;
  dir[0xf] |= (tm->tm_hour) << 2;
  dir[0x10] = tm->tm_mday & 0x1f;
  dir[0x10] |= ((tm->tm_mon + 1) & 0x7) << 5;
  dir[0x11] = ((tm->tm_mon + 1) & 0x1) >> 3;
  dir[0x11] |= (tm->tm_year - 80) << 1;

  //	  dump_bytes(0,"New directory entry",dir,32);

  // (Cluster and size we set after writing to the file)

  // Copy back into directory sector, and write it
  bcopy(dir, &dir_sector_buffer[dir_sector_offset], 32);
  if (write_sector(partition_start + dir_sector, dir_sector_buffer)) {
    printf("Failed to write updated directory sector.\n");
    return FALSE;
  }
  return TRUE;
}

char* get_file_extension(char* filename)
{
  int i = strlen(filename) - 1;
  do {
    char* c = filename + i;
    if (*c == '.')
      return c;
    i--;
  } while (i >= 0);

  return NULL;
}

BOOL is_d81_file(char* filename)
{
  char* ext = get_file_extension(filename);

  if (!strcmp(ext, ".d81") || !strcmp(ext, ".D81"))
    return TRUE;

  return FALSE;
}

int is_fragmented(char* filename)
{
  int fragmented_flag = 0;

  if (!safe_open_dir())
    exit(-1);

  struct m65dirent de;
  if (!find_file_in_curdir(filename, &de)) {
    printf("?  FILE NOT FOUND ERROR FOR \"%s\"\n", filename);
    exit(-1);
  }

  unsigned int first_cluster_of_file = calc_first_cluster_of_file();

  unsigned int current_cluster = first_cluster_of_file;
  int next_cluster;
  while ((next_cluster = chained_cluster(current_cluster)) != 0xffffff8) {
    if (next_cluster != current_cluster + 1)
      fragmented_flag = 1;
    current_cluster = next_cluster;
  }

  return fragmented_flag;
}

int queue_mount_file(char* filename)
{
  uint8_t job[66]; // for now, I made a max filename size of 64 bytes
                   // (1 byte for job-id, 1 byte for null-terminator)
  int len = strlen(filename);

  if (len > 64) {
    printf("ERROR: For now, maximum filename is limited to 64 bytes\n"
           "       (this is a hardcoded limit for mount that we can change later, if needbe)\n");
    return FALSE;
  }

  job[0] = 0x12;
  strcpy((char*)(job + 1), filename);
  len++;
  job[len] = 0; // add null-terminator at end
  len++;
  queue_add_job(job, len);

  return TRUE;
}

void petscify_text(char* text)
{
  for (int k = 0; k < strlen(text); k++) {
    char c = text[k] & 0xdf;
    // switch lower and upper-case
    if (c >= 0x41 && c <= 0x5a)
      text[k] = text[k] ^ 0x20;
  }
}

void poke(unsigned long addr, unsigned char value)
{
  char cmd[16];
  sprintf(cmd, "s%lx %x\r", addr, value);
  slow_write(fd, cmd, strlen(cmd));
}

void request_remotesd_version(void)
{
  poke(0xc001, 0x13);
  poke(0xc000, 0x01);
}

void request_quit(void)
{
  poke(0xc001, 0xff);
  poke(0xc000, 0x01);
}

void mount_file(char* filename)
{
  if (!safe_open_dir())
    return;

  // Check if file exists
  struct m65dirent de;
  if (!find_file_in_curdir(filename, &de)) {
    printf("?  FILE NOT FOUND ERROR FOR \"%s\"\n", filename);
    return;
  }

  petscify_text(filename);

  if (!queue_mount_file(filename))
    return;

  queue_execute();
}

int download_file(char* dest_name, char* local_name, int showClusters)
{
  struct m65dirent de;
  int retVal = 0;
  do {

    time_t upload_start = time(0);

    if (!safe_open_dir())
      return -1;

    if (!find_file_in_curdir(dest_name, &de)) {
      printf("?  FILE NOT FOUND ERROR FOR \"%s\"\n", dest_name);
      return -1;
    }

    unsigned int first_cluster_of_file = calc_first_cluster_of_file();

    // Now write the file to local disk sector by sector
    int remaining_bytes = de.d_filelen;
    int sector_in_cluster = 0;
    int file_cluster = first_cluster_of_file;
    unsigned int sector_number;
    FILE* f = NULL;

    if (!showClusters) {
      f = fopen(local_name, "wb");
      if (!f) {
        printf("ERROR: Could not open file '%s' for writing.\n", local_name);
        retVal = -1;
        break;
      }
    }
    else
      printf("Clusters: %d", file_cluster);

    while (remaining_bytes) {
      if (sector_in_cluster >= sectors_per_cluster) {
        // Advance to next cluster
        // If we are currently the last cluster, then allocate a new one, and chain it in

        int next_cluster = chained_cluster(file_cluster);
        if (next_cluster == 0 || next_cluster >= 0xffffff8) {
          printf("\n?  PREMATURE END OF FILE ERROR\n");
          if (f)
            fclose(f);
          retVal = -1;
          break;
        }
        if (showClusters) {
          if (next_cluster == (file_cluster + 1))
            printf(".");
          else
            printf("%d, %d", file_cluster, next_cluster);
        }

        file_cluster = next_cluster;
        sector_in_cluster = 0;
      }

      if (f) {
        // Read sector
        sector_number = partition_start + first_cluster_sector + (sectors_per_cluster * (file_cluster - first_cluster))
                      + sector_in_cluster;

        // We try to read-ahead a lot of sectors, because files are usually not very fragmented,
        // so the extra read-ahead reduces the rount-trip time for scheduling each successive job
        if (read_sector(sector_number, download_buffer, CACHE_YES, 128)) {
          printf("ERROR: Failed to read to sector %d\n", sector_number);
          retVal = -1;
          if (f)
            fclose(f);
          break;
        }

        if (remaining_bytes >= 512)
          fwrite(download_buffer, 512, 1, f);
        else
          fwrite(download_buffer, remaining_bytes, 1, f);
      }

      if (0)
        printf("T+%lld : Read %d bytes from file, writing to sector $%x (%d) for cluster %d\n", gettime_us() - start_usec,
            (int)de.d_filelen, sector_number, sector_number, file_cluster);
      if (!showClusters)
        printf("\rDownloaded %lld bytes.", (long long)de.d_filelen - remaining_bytes);
      fflush(stdout);

      //      printf("T+%lld : after write.\n",gettime_us()-start_usec);

      sector_in_cluster++;
      remaining_bytes -= 512;
      if (remaining_bytes < 0)
        remaining_bytes = 0;
    }

    if (f)
      fclose(f);

    if (time(0) == upload_start)
      upload_start = time(0) - 1;
    if (!showClusters) {
      printf("\rDownloaded %lld bytes in %lld seconds (%.1fKB/sec)\n", (long long)de.d_filelen,
          (long long)time(0) - upload_start, de.d_filelen * 1.0 / 1024 / (time(0) - upload_start));
    }
    else
      printf("\n");

  } while (0);

  return retVal;
}
