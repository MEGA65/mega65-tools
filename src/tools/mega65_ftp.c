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

#include "etherload_common.h"

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
#include "etherload/ethlet_all_done_basic2_map.h"
#include "filehost.h"
#include "diskman.h"
#include "dirtymock.h"
#include "logging.h"

#define BOOL int
#define TRUE 1
#define FALSE 0

#define DE_ATTRIB_DIR 0x10
#define DE_ATTRIB_FILE 0x20

#define BYTES_PER_MB 1048576

#define SECTOR_CACHE_SIZE 4096
int sector_cache_count = 0;
unsigned char sector_cache[SECTOR_CACHE_SIZE][512];
unsigned int sector_cache_sectors[SECTOR_CACHE_SIZE];

// dummy, don't want to include fpgajtag for device discovery yet
char *usbdev_get_next_device(const int start)
{
  return NULL;
}

struct m65dirent {
  long d_ino;                    /* start cluster */
  long d_filelen;                /* length of file */
  unsigned short d_reclen;       /* Always sizeof struct dirent. */
  unsigned short d_namlen;       /* Length of name in d_name. */
  unsigned char d_attr;          /* FAT file attributes */
  unsigned d_type;               /* Object type (digested attributes) */
  struct tm d_time;              /* Creation time? */
  char d_name[FILENAME_MAX];     /* File name. */
  char d_longname[FILENAME_MAX]; /* Long file name. */
  // extra debug fields for info on dir entries
  long de_cluster;
  long de_sector;
  int de_sector_offset;
  unsigned char de_raw[32]; /* preserve dirent_raw info for debugging purposes */
};

void assemble_time_from_raw(unsigned char *buffer, struct m65dirent *de)
{
  memset(&de->d_time, 0, sizeof(struct tm));

  de->d_time.tm_sec = (buffer[0xe] & 0x1f) << 1;
  de->d_time.tm_min = (buffer[0xe] >> 5) & 0x07;

  de->d_time.tm_min |= ((buffer[0xf] & 0x7) << 3);
  de->d_time.tm_hour = buffer[0xf] >> 3;

  de->d_time.tm_mday = buffer[0x10] & 0x1f;
  de->d_time.tm_mon = ((buffer[0x10] >> 5) & 0x7);

  de->d_time.tm_mon |= ((buffer[0x11] & 0x1) << 3);
  de->d_time.tm_mon -= 1;
  de->d_time.tm_year = (buffer[0x11] >> 1) + 80;
}

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
int download_slot(int sllot, char *dest_name);
int download_file(char *dest_name, char *local_name, int showClusters);
int download_flashslot(int slot_number, char *dest_name);
void show_clustermap(void);
void show_cluster(int cluster_num);
void dump_sectors(void);
void restore_sectors(void);
void show_secinfo(void);
void show_mbrinfo(void);
void show_vbrinfo(void);
void poke_sector(void);
void parse_pokes(char *cmd);
void perform_filehost_read(char *searchterm);
void perform_filehost_get(int num, char *destname);
void perform_filehost_flash(int fhnum, int slotnum);
void perform_flash(char *fname, int slotnum);
void list_all_roms(void);
int show_directory(char *path);
void show_local_directory(char *searchpattern);
void change_local_dir(char *path);
void change_dir(char *path);
void show_local_pwd(void);
int delete_file_or_dir(char *name);
int rename_file_or_dir(char *name, char *dest_name);
int upload_file(char *name, char *dest_name);
int sdhc_check(void);
void request_remotesd_version(void);
void request_quit(void);
void mount_file(char *filename);
int ethernet_get_packet_seq(uint8_t *payload, int len);
int ethernet_match_payloads(uint8_t *rx_payload, int rx_len, uint8_t *tx_payload, int tx_len);
int ethernet_is_duplicate(uint8_t *payload, int len, uint8_t *cmp_payload, int cmp_len);
int ethernet_embed_packet_seq(uint8_t *payload, int len, int seq_num);
int ethernet_timeout_handler();
#define CACHE_NO 0
#define CACHE_YES 1
int read_flash(const unsigned int sector_number, unsigned char *buffer);
int read_sector(const unsigned int sector_number, unsigned char *buffer, int useCache, int readAhead);
int write_sector(const unsigned int sector_number, unsigned char *buffer);
int load_helper(void);
int stuff_keybuffer(char *s);
int create_dir(char *);
int fat_opendir(char *, int show_errmsg);
int fat_readdir(struct m65dirent *, int extend_dir_flag);
BOOL safe_open_dir(void);
BOOL find_file_in_curdir(char *filename, struct m65dirent *de);
BOOL create_directory_entry_for_item(
    char *filename, char *short_name, unsigned char lfn_csum, BOOL needs_long_name, int attrib);
unsigned int calc_first_cluster_of_file(void);
unsigned int calc_size_of_file(void);
BOOL is_d81_file(char *filename);
void wrap_upload(char *fname);
char *get_file_extension(char *filename);
int extend_dir_cluster_chain(void);
BOOL find_contiguous_free_direntries(int direntries_needed);
int normalise_long_name(char *long_name, char *short_name, char *dir_name);
void write_file_size_into_direntry(unsigned int size);
void write_cluster_number_into_direntry(int a_cluster);
int calculate_needed_direntries_for_vfat(char *filename);
unsigned char lfn_checksum(const unsigned char *pFCBName);
int get_cluster_count(char *filename);
void wipe_direntries_of_current_file_or_dir(void);
void determine_ethernet_window_size(void);

int direct_sdcard_device = 0;
FILE *fsdcard = NULL;

int ethernet_mode = 0;
int ethernet_window_size = 3;
static int sockfd;
static struct sockaddr_in *servaddr;
static uint8_t eth_packet_queue[1024][1500];
static uint16_t eth_packet_len[1024];
int eth_num_packets = 0;
uint32_t eth_batch_start_sector = 0;
uint8_t eth_batch_size = 0;

// Helper routine for faster sector writing
extern unsigned int helperroutine_len;
extern unsigned char helperroutine[];
extern unsigned int helperroutine_eth_len;
extern unsigned char helperroutine_eth[];
extern char ethlet_all_done_basic2[];
extern int ethlet_all_done_basic2_len;
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

int serial_port_set = 0;
char serial_port[1024] = "/dev/ttyUSB1";
char device_name[1024] = "";
char ip_address[16] = "";
char *bitstream = NULL;
char *username = NULL;
char *password = NULL;

unsigned char *sd_read_buffer = NULL;
int sd_read_offset = 0;

int file_system_found = 0;
unsigned int partition_start = 0xffffffff; // The absolute sector location of the start of the partition (in units of
                                           // sectors)
unsigned int partition_size = 0;
unsigned char sectors_per_cluster = 0;
unsigned int sectors_per_fat = 0;
unsigned int data_sectors = 0;
unsigned int first_cluster = 0;
unsigned int fsinfo_sector = 0;
unsigned int reserved_sectors = 0;
unsigned int fat1_sector = 0, fat2_sector = 0;
unsigned int first_cluster_sector; // Slightly confusing name, as the first cluster is actually cluster #2
                                   // Even more confusing is that this value is relative to the start of the partition
                                   // (partition_start) I.e. to calculate the absolute sector of cluster#2 = partition_start
                                   // + first_cluster_sector

int nosys = 0; // a flag to ignore system partition
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
unsigned char force_helper_push = 0;

#define M65DT_REG 1
#define M65DT_DIR 2
#define M65DT_UNKNOWN 4
#define M65DT_FREESLOT 0xff

int sd_status_fresh = 0;
unsigned char sd_status[16];
int quietFlag = 0;

extern const char *version_string;

void usage(void)
{
  fprintf(stderr, "MEGA65 cross-development tool for FTP-like access to MEGA65 SD card via serial monitor interface\n");
  fprintf(stderr, "version: %s\n\n", version_string);
  fprintf(stderr, "usage: mega65_ftp [-0 <log level>] [-F] [-l <serial port>|-d <device name>|-i <broadcast ip>] [-s "
                  "<230400|2000000|4000000>]  "
                  "[-b bitstream] [[-c command] ...]\n");
  fprintf(stderr, "  -0 - set log level (0 = quiet ... 5 = everything)\n");
  fprintf(stderr, "  -F - force startup, even if other program is detected\n");
  fprintf(stderr, "  -l - Name of serial port to use, e.g., /dev/ttyUSB1\n");
  fprintf(stderr, "  -d - device name of sd-card attached to your pc (e.g. /dev/sdx)\n");
  fprintf(stderr, "  -i - broadcast ip of ethernet interface to use (e.g. 192.168.1.255)\n");
  fprintf(stderr, "  -s - Speed of serial port in bits per second. This must match what your bitstream uses.\n");
  fprintf(stderr, "       (Almost always 2000000 is the correct answer).\n");
  fprintf(stderr, "  -b - Name of bitstream file to load.\n");
  fprintf(stderr, "  -n - suppress scanning of 'system' partition (handy when connecting to partial sdcard dump files).\n");
  fprintf(stderr, "\n");
  exit(-3);
}

#define READ_SECTOR_BUFFER_ADDRESS 0xFFD6e00
#define WRITE_SECTOR_BUFFER_ADDRESS 0xFFD6e00

int queued_command_count = 0;
#define MAX_QUEUED_COMMANDS 64
char *queued_commands[MAX_QUEUED_COMMANDS];

int queue_command(char *c)
{
  if (queued_command_count < MAX_QUEUED_COMMANDS)
    queued_commands[queued_command_count++] = c;
  else
    log_error("too many commands queued via -c");
  return 0;
}

unsigned char show_buf[512];
int show_sector(unsigned int sector_num)
{
  if (read_sector(sector_num, show_buf, CACHE_YES, 0)) {
    log_error("could not read sector %d ($%x)", sector_num, sector_num);
    return -1;
  }
  dump_bytes(0, "Sector contents", show_buf, 512);
  return 0;
}

int parse_string_param(char **src, char *dest)
{
  int cnt = 0;
  char *srcptr = *src;
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

int parse_int_param(char **src, int *dest)
{
  int cnt = 0;
  char str[128];
  char *srcptr = *src;

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

int skip_whitespace(char **orig)
{
  char *ptrstr = *orig;

  // skip any spaces in str
  while (*ptrstr == ' ' || *ptrstr == '\t') {
    if (*ptrstr == '\0')
      return 0;
    ptrstr++;
  }

  *orig = ptrstr;
  return 1;
}

int parse_format_specifier(char **pptrformat, char **pptrstr, va_list *pargs, int *pcnt)
{
  int found;
  if (**pptrformat == '%') {
    (*pptrformat)++;
    if (**pptrformat == 's') {
      if (!skip_whitespace(pptrstr))
        return RET_FAIL;
      found = parse_string_param(pptrstr, va_arg(*pargs, char *));
      if (found)
        (*pcnt)++;
      else
        return RET_FAIL;
    }
    else if (**pptrformat == 'd') {
      if (!skip_whitespace(pptrstr))
        return 0;
      found = parse_int_param(pptrstr, va_arg(*pargs, int *));
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

int parse_non_whitespace(char **pptrformat, char **pptrstr)
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

int parse_command(const char *str, const char *format, ...)
{
  va_list args;
  va_start(args, format);

  // scan through str looking for '%' tokens
  char *ptrstr = (char *)str;
  char *ptrformat = (char *)format;
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

int execute_command(char *cmd)
{
  int cluster_num = 0;
  unsigned int sector_num;

  if (strlen(cmd) > 1000) {
    log_error("command too long");
    return -1;
  }
  int slot = 0;
  char src[1024];
  char dst[1024];
  if ((!strcmp(cmd, "exit")) || (!strcmp(cmd, "quit"))) {
    if (!direct_sdcard_device) {
      log_note("reseting MEGA65 and exiting");

      request_quit();
      if (xemu_flag)
        usleep(30000);
    }
    if (ethernet_mode) {
      etherload_finish();
    }
    exit(0);
  }

  if (parse_command(cmd, "getslot %d %s", &slot, dst) == 2) {
    download_slot(slot, dst);
  }
  else if (parse_command(cmd, "getflash %d %s", &slot, dst) == 2) {
    download_flashslot(slot, dst);
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
    delete_file_or_dir(src);
  }
  else if (parse_command(cmd, "rename %s %s", src, dst) == 2) {
    rename_file_or_dir(src, dst);
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
    char *dest = src;
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
    change_dir(src);
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
    show_cluster(cluster_num);
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
  else if (strncmp(cmd, "poke ", 5) == 0) {
    parse_pokes(cmd + 5);
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
    printf("dirent_raw 0|1|2 - flag to hide/show 32-byte dump of directory entries. (2=more verbose)\n");
    printf("clustermap <startidx> [<count>] - show cluster-map entries for specified range.\n");
    printf("cluster <num> - dump the entire contents of this cluster.\n");
    printf("secdump <filename> <startsec> <count> - dump the specified sector range to a file.\n");
    printf("secrestore <filename> <startsec> - restore a dumped file back into the specified sector area.\n");
    printf("secinfo - lists the locations of various useful sectors, for easy reference.\n");
    printf("mbrinfo - lists the partitions specified in the MBR (sector 0)\n");
    printf("vbrinfo - lists the VBR details of the main Mega65 partition\n");
    printf("poke <sector> <offset> <val> - poke a value into a sector, at the desired offset.\n");
    printf("fh - retrieve a list of files available on the filehost at files.mega65.org\n"
           "     - can use wildcard searches. E.g. 'fh *.cor'\n"
           "     - can use -t to sort by published datetime\n");
    printf("fhget <num> [destname] - download a file from the filehost and upload it onto your sd-card\n"
           "                         (to only download to local drive, set destname to -\n");
    printf("fhflash <num> <slotnum> - download a cor file from the filehost and flash it to specified slot via vivado\n");
    printf("flash <fname> <slotnum> - flash a cor file on your local drive to specified slot via vivado\n");
    printf("roms - list all MEGA65x.ROM files on your sd-card along with their version information\n");
    printf("exit - leave this programme.\n");
    printf("quit - leave this programme.\n");
  }
  else {
    log_error("unknown command or invalid syntax. Type help for help");
    return -1;
  }
  return 0;
}

extern int debug_serial;

#ifdef WINDOWS
char *getpass(char *prompt)
{
  printf("%s", prompt);
  static char password[128];
  password[0] = '\0';
  int idx = 0;

  for (;;) {
    int c = _getch();
    switch (c) {
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
      _putch('*'); // mask
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

int DIRTYMOCK(main)(int argc, char **argv)
{
#ifdef WINDOWS
  // working around mingw64-stdout line buffering issue with advice suggested here:
  // https://stackoverflow.com/questions/13035075/printf-not-printing-on-console
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
#endif
  int loglevel = LOG_NOTE;
  start_time = time(0);
  start_usec = gettime_us();

  log_setup(stderr, LOG_NOTE);

  int opt;
  while ((opt = getopt(argc, argv, "b:Ds:l:c:u:p:d:i:0:nF")) != -1) {
    switch (opt) {
    case '0':
      loglevel = log_parse_level(optarg);
      if (loglevel == -1)
        log_warn("failed to parse log level!");
      else
        log_setup(stderr, loglevel);
      break;
    case 'F':
      force_helper_push = 1;
      break;
    case 'D':
      debug_serial = 1;
      break;
    case 'l':
      strcpy(serial_port, optarg);
      break;
    case 'd':
      strcpy(device_name, optarg);
      direct_sdcard_device = 1;
      break;
    case 'i':
      strncpy(ip_address, optarg, sizeof(ip_address));
      ethernet_mode = 1;
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
    case 'n':
      nosys = 1;
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

  if (direct_sdcard_device) {
    fsdcard = fopen(device_name, "r+b");
    if (fsdcard == NULL) {
      log_error("could not open device '%s'", device_name);
      exit(-3);
    }
  }
  else if (ethernet_mode) {
    unsigned char *helper_ptr = helperroutine_eth + 2;
    int bytes = helperroutine_eth_len - 2;
    int address = 0x0801;
    int block_size = 1024;

    if (etherload_init(ip_address)) {
      log_error("Unable to initialize ethernet communication");
      exit(-1);
    }
    ethl_setup_dmaload();
    trigger_eth_hyperrupt();
    usleep(100000);
    log_info("Starting helper routine transfer...");
    while (bytes > 0) {
      if (bytes < block_size)
        block_size = bytes;
      send_mem(address, helper_ptr, block_size);
      helper_ptr += block_size;
      address += block_size;
      bytes -= block_size;
    }
    wait_all_acks();
    log_info("Helper routine transfer complete");

    // patch in end address
    ethlet_all_done_basic2[ethlet_all_done_basic2_offset_data_end_address] = 0x01;
    ethlet_all_done_basic2[ethlet_all_done_basic2_offset_data_end_address + 1] = 0x08;

    // patch in do_run
    ethlet_all_done_basic2[ethlet_all_done_basic2_offset_do_run] = 1;

    // disable cartridge signature detection
    ethlet_all_done_basic2[ethlet_all_done_basic2_offset_enable_cart_signature] = 0;

    send_ethlet((uint8_t *)ethlet_all_done_basic2, ethlet_all_done_basic2_len);

    sockfd = ethl_get_socket();
    servaddr = ethl_get_server_addr();

    // setup callbacks for job queue protocol
    ethl_setup_callbacks(&ethernet_get_packet_seq, &ethernet_match_payloads, &ethernet_is_duplicate,
        &ethernet_embed_packet_seq, ethernet_timeout_handler);

    // Give helper program time to initialize
    usleep(700000);

    determine_ethernet_window_size();
    sdhc_check();
  }
  else {
    if (open_the_serial_port(serial_port))
      exit(-1);
    xemu_flag = mega65_peek(0xffd360f) & 0x20 ? 0 : 1;

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

    if (load_helper())
      return 1;

    // Give helper time to get all sorted.
    // Without this delay serial monitor commands to set memory seem to fail :/
    usleep(500000);

    //  monitor_sync();

    sdhc_check();
  }

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
    char *cmd = NULL;
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
    log_error("could not detect SD/SDHC card");
    exit(-3);
  }
  if (r1)
    sdhc = 0;
  else
    sdhc = 1;
  if (sdhc)
    log_note("SD card is SDHC");
  return sdhc;
}

int load_helper(void)
{
  char buffer[8193];
  int retVal = 0, bytes;
  do {
    if (!helper_installed) {
      // Install helper routine

      monitor_sync();

      // First see if the helper is already running by looking for the
      // MEGA65FT1.0 string
      request_remotesd_version();
      sleep(1);
      bytes = serialport_read(fd, (unsigned char *)buffer, 8192);
      buffer[bytes] = 0;
      if (strstr(buffer, "MEGA65FT1.0")) {
        helper_installed = 1;
        log_debug("helper already running. Nothing to do");
        return 0;
      }

      detect_mode();

      // the helper is not responding, let's check if there
      // is a program in memory we could destroy
      // WARNING: this also prevents mega65_ftp from starting
      // if the flasher is running! Don't remove!
      snprintf(buffer, 80, saw_c64_mode ? "m0801\n" : "m2001\n");
      serialport_write(fd, (unsigned char *)buffer, strlen(buffer));
      usleep(20000);
      bytes = serialport_read(fd, (unsigned char *)buffer, 8192);
      buffer[bytes] = 0;
      // return should be :00002001:0000... or :00000801:0000... if prg is empty
      if (!strstr(buffer, "01:0000")) {
        if (force_helper_push)
          log_warn("trying to overwriting program in memory!");
        else {
          log_error("a program is already in memory, refusing to start helper! (-F to override)");
          return -1;
        }
      }

      if ((!saw_c64_mode)) {
        start_cpu();
        switch_to_c64mode();
      }

      fake_stop_cpu();

      char cmd[1024];

      // Load helper, minus the 2 byte load address header
      push_ram(0x0801, helperroutine_len - 2, &helperroutine[2]);
      log_debug("pushed helper into memory");

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
      log_note("fast SD card access routine installed");
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
    log_debug(
        "decoding $%02x, rle_count=%d, raw_count=%d, data_byte_count=$%04x", v, q_rle_count, q_raw_count, data_byte_count);
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
    log_debug("decoding raw byte $%02x, data_byte_count=$%04x", v, data_byte_count);
  if (queue_read_len < 1024 * 1024)
    queue_read_data[queue_read_len++] = v;
  if (data_byte_count)
    data_byte_count--;
}

void queue_add_job(uint8_t *j, int len)
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
        if (!strncmp((char *)&recent[30 - 10], "FTBATCHDONE", 11)) {
          long long endtime = gettime_us();
          if (debug_rx)
            printf("%lld: Saw end of batch job after %lld usec\n", endtime - start_usec, endtime - now);
          //	  dump_bytes(0,"read data",queue_read_data,queue_read_len);
          return;
        }
        if (!strncmp((char *)recent, "FTJOBDONE:", 10)) {
          int jn = atoi((char *)&recent[10]);
          if (debug_rx)
            printf("Saw job #%d completion.\n", jn);
        }
        int j_addr, n;
        uint32_t transfer_size;
        int fn = sscanf((char *)recent, "FTJOBDATA:%x:%x:%n", &j_addr, &transfer_size, &n);
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
        fn = sscanf((char *)recent, "FTJOBDATR:%x:%x:%n", &j_addr, &transfer_size, &n);
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

uint8_t memory_read_buffer[256];
uint8_t memory_read_buffer_len = 0;

void ethernet_process_result(uint8_t *rx_payload, int rx_len)
{
  uint8_t *p = &rx_payload[6];
  uint32_t sector_number;
  uint32_t memory_addr;

  switch (*p) {
  case 0x04: // sector read
  {
    sector_number = p[3] + (p[4] << 8) + (p[5] << 16) + (p[6] << 24);
    log_debug("Received sector %d, batch start sector %d", sector_number, eth_batch_start_sector);
    uint32_t index = sector_number - eth_batch_start_sector;
    bcopy(&rx_payload[13], &queue_read_data[queue_read_len + index * 512], 512);
    break;
  }

  case 0x11: // memory read
    memory_read_buffer_len = p[1] + 1;
    memory_addr = p[2] + (p[3] << 8) + (p[4] << 16) + (p[5] << 24);
    log_debug("Received memory block at $%x (%d bytes)", memory_addr, memory_read_buffer_len);
    bcopy(&rx_payload[12], memory_read_buffer, memory_read_buffer_len);
    break;
  }
}

int ethernet_get_packet_seq(uint8_t *payload, int len)
{
  if (len < 6) {
    return -1;
  }

  return payload[4] + (payload[5] << 8);
}

int ethernet_match_payloads(uint8_t *rx_payload, int rx_len, uint8_t *tx_payload, int tx_len)
{
  if (rx_len < 7 || tx_len < 7 || memcmp(rx_payload, "mrsp", 4) || memcmp(tx_payload, "mreq", 4)) {
    return 0;
  }

  /*
    if (rx_payload[4] != tx_payload[4] || rx_payload[5] != tx_payload[5]) {
      return 0;
    }
  */

  switch (rx_payload[6]) {
  case 0x02: // write sector cmd
    // rx_payload[7] is batch id
    // rx_payload[8] is batch size
    // rx_payload[9] is idx in batch
    // ry_payload[10] is 4 bytes of sector number
    if (rx_len != 14 || memcmp(&rx_payload[7], &tx_payload[7], 7) != 0) {
      return 0;
    }
    log_debug("Received packet (write_sector ack) #%d matches expected packet #%d", (rx_payload[4] + (rx_payload[5] << 8)),
        (tx_payload[4] + (tx_payload[5] << 8)));
    break;
  case 0x04: // read sector cmd
    // ry_payload[9] is 4 bytes of sector number
    if (rx_len != 512 + 13 || memcmp(&rx_payload[9], &tx_payload[9], 4) != 0) {
      return 0;
    }
    log_debug("Received packet (read_sector ack) #%d matches expected packet #%d", (rx_payload[4] + (rx_payload[5] << 8)),
        (tx_payload[4] + (tx_payload[5] << 8)));
    ethernet_process_result(rx_payload, rx_len);
    break;
  case 0x11: // read memory cmd
    // ry_payload[7] is number of bytes
    // ry_payload[8] is 4 bytes of address
    if (rx_len < 13 || memcmp(&rx_payload[7], &tx_payload[7], 5) != 0) {
      return 0;
    }
    log_debug("Received packet (read_memory ack) #%d matches expected packet #%d", (rx_payload[4] + (rx_payload[5] << 8)),
        (tx_payload[4] + (tx_payload[5] << 8)));
    ethernet_process_result(rx_payload, rx_len);
    break;
  case 0xff: // quit cmd
    if (rx_len != 7) {
      return 0;
    }
    log_debug("Received packet (quit ack) #%d matches expected packet #%d", (rx_payload[4] + (rx_payload[5] << 8)),
        (tx_payload[4] + (tx_payload[5] << 8)));
    break;
  default:
    return 0;
  }

  return 1;
}

int ethernet_is_duplicate(uint8_t *payload, int len, uint8_t *cmp_payload, int cmp_len)
{
  return 0;
}

int ethernet_embed_packet_seq(uint8_t *payload, int len, int seq_num)
{
  if (len < 7) {
    return 0;
  }
  payload[4] = seq_num;
  payload[5] = seq_num >> 8;
  return 1;
}

const uint8_t ethernet_request_string[4] = { 'm', 'r', 'e', 'q' };

int ethernet_timeout_handler()
{
  log_warn("ACK timeout, requesting ethernet controller reset from MEGA65");
  const int packet_size = 7;
  uint8_t payload[packet_size];
  memcpy(payload, ethernet_request_string, 4); // 'mreq' magic string
  payload[4] = 0;                              // no seq num for tx reset
  payload[5] = 0;                              // no seq num for tx reset
  payload[6] = 0xfe;                           // reset tx command
  ethl_send_packet_unscheduled(payload, packet_size);
  return 1;
}

uint32_t write_buffer_offset = 0;
uint8_t write_data_buffer[65536];
uint32_t write_sector_numbers[65536 / 512];
uint8_t write_sector_count = 0;
uint8_t write_batch_counter = 0;

void process_ethernet_write_sectors_job(uint8_t *job, int batch_size)
{
  // Batches should not be mixed with single writes
  // Make sure we start with an empty queue of pending packets before we
  // start the next batch
  if (batch_size > 1) {
    wait_all_acks();
  }

  const int packet_size = 14 + 512;
  int i;
  uint8_t payload[packet_size];
  memcpy(payload, ethernet_request_string, 4); // 'mreq' magic string
  // bytes [4] and [5] will be filled with packet seq numbers
  payload[6] = *job;
  payload[7] = write_batch_counter;
  payload[8] = (batch_size - 1) & 0xff;
  memcpy(&payload[10], &job[5], 4); // start sector

  for (i = 0; i < batch_size; ++i) {
    int data_offset = job[1] + (job[2] << 8) + (job[3] << 16) + (job[4] << 24) - 0x50000;
    payload[9] = i & 0xff; // slot index
    bcopy(&write_data_buffer[data_offset], &payload[14], 512);
    ethl_send_packet(payload, packet_size);
    job += 9;
  }

  // Batches should not be mixed with single writes, so make sure the batch
  // is applied completely before we do other batches or individual sector writes
  if (batch_size > 1) {
    wait_all_acks();
  }

  ++write_batch_counter;
}

void process_ethernet_read_sectors_job(uint8_t *job)
{
  int i;
  uint8_t payload[13];
  memcpy(payload, ethernet_request_string, 4); // 'mreq' magic string
  memcpy(&payload[6], job, 7);                 // copy job bytes into packet

  eth_batch_start_sector = payload[9] + (payload[10] << 8) + (payload[11] << 16) + (payload[12] << 24);
  eth_batch_size = payload[7];
  --payload[7];
  if (payload[8] != 0) {
    log_error("Unsupported batch size %d for ethernet mode", payload[7] + (payload[8] << 8));
    exit(-1);
  }

  // We'll send a single request packet for a sector range, but will expect individual packets
  // per sector as response (with the sector data in the payload). To detect dropped packets, we
  // schedule an expected ack packet in the Etherload framework for each sector. Thus, we
  // 'unroll' the request packet into multiple packets and schedule them. If a single response
  // packet will be missing the framework will schedule resends for that single packet/sector
  // automatically.
  uint8_t payload_unrolled[13];
  memcpy(payload_unrolled, payload, sizeof(payload));
  payload_unrolled[7] = 1;
  payload_unrolled[8] = 0;
  uint32_t sector_number = eth_batch_start_sector;
  wait_ack_slots_available(eth_batch_size);
  uint16_t seq_num = ethl_get_current_seq_num();
  for (i = 0; i < eth_batch_size; ++i) {
    payload_unrolled[9] = sector_number >> 0;
    payload_unrolled[10] = sector_number >> 8;
    payload_unrolled[11] = sector_number >> 16;
    payload_unrolled[12] = sector_number >> 24;
    memcpy(eth_packet_queue[i], payload_unrolled, sizeof(payload_unrolled));
    ++sector_number;
    ethl_schedule_ack(payload_unrolled, sizeof(payload_unrolled));
    eth_packet_len[i] = sizeof(payload_unrolled);
  }
  // We already scheduled the ack packages, so send the request without scheduling another
  // expected response for it.
  ethernet_embed_packet_seq(payload, sizeof(payload), seq_num);
  ethl_send_packet_unscheduled(payload, sizeof(payload));
}

void process_jobs_ethernet(void)
{
  uint8_t *ptr = queue_cmds;
  int i;
  queue_read_len = 0;
  uint8_t last_cmd = 0;
  int write_batch_size = 0;

  for (i = 0; i < queue_jobs; ++i) {
    uint8_t cur_cmd = *ptr;
    if (last_cmd != cur_cmd && i > 0) {
      // If we switch command (read, write, ...) make sure the one before is completely
      // done before we continue so we don't mix read/write commands
      log_debug("Switch of command type, waiting for acks\n");
      wait_all_acks();
    }
    last_cmd = cur_cmd;

    switch (cur_cmd) {
    case 0x02: // physical write sector
    {
      // We see a new write command, let's detect whether this is a batch of consecutive
      // sectors
      int j;
      uint8_t *look_ahead_ptr = ptr;
      write_batch_size = 1;
      int sector_number = ptr[5] + (ptr[6] << 8) + (ptr[7] << 16) + (ptr[8] << 24);
      for (j = i + 1; j < queue_jobs && write_batch_size <= 256; ++j) {
        look_ahead_ptr += 9;
        if (*look_ahead_ptr != *ptr) {
          break;
        }
        int next_sector_number = look_ahead_ptr[5] + (look_ahead_ptr[6] << 8) + (look_ahead_ptr[7] << 16)
                               + (look_ahead_ptr[8] << 24);
        if (next_sector_number != sector_number + 1) {
          break;
        }
        ++sector_number;
        ++write_batch_size;
      }
      ethl_set_queue_length(ethernet_window_size);
      process_ethernet_write_sectors_job(ptr, write_batch_size);
      ptr += 9 * write_batch_size;
      i += write_batch_size - 1;
      write_batch_size = 0;
      break;
    }

    case 0x04: // read sectors
      ethl_set_queue_length(256);
      process_ethernet_read_sectors_job(ptr);
      ptr += 7;
      break;

    case 0x0f: // read flash
      ptr += 7;
      log_debug("read flash job not implemented for ethernet");
      exit(-1);
      break;
    case 0x11: // read mem
      ptr += 9;
      log_debug("read mem job not implemented for ethernet");
      exit(-1);
      break;
    default:
      log_error("Queue cmd %d not implemented", *ptr);
      exit(-1);
    }
  }

  wait_all_acks();
}

void queue_execute(void)
{
  if (ethernet_mode) {
    process_jobs_ethernet();
  }
  else {
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
  }
  queue_addr = 0xc001;
  queue_jobs = 0;
}

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
    if (!ethernet_mode) {
      if (0)
        log_debug("executing write queue with %d sectors in the queue (write_buffer_offset=$%08x)", write_sector_count,
            write_buffer_offset);
      push_ram(0x50000, write_buffer_offset, &write_data_buffer[0]);
    }

    // XXX - Sort sector number order and merge consecutive writes into
    // multi-sector writes would be a good idea here.
    for (int i = 0; i < write_sector_count; i++) {
      queue_physical_write_sector(write_sector_numbers[i], 0x50000 + (i << 9));
    }
    // printf("Execute write queue with %d entries\n", write_sector_count);
    queue_execute();

    // Reset write queue
    write_buffer_offset = 0;
    write_sector_count = 0;
  } while (0);
  return retVal;
}

void queue_write_sector(uint32_t sector_number, uint8_t *buffer)
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

void queue_read_flash(uint32_t flash_address, uint16_t sector_count)
{
  uint8_t job[7];
  job[0] = 0x0f;
  job[1] = sector_count >> 0;
  job[2] = sector_count >> 8;
  job[3] = flash_address >> 0;
  job[4] = flash_address >> 8;
  job[5] = flash_address >> 16;
  job[6] = flash_address >> 24;
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
int read_flash(const unsigned int flash_address, unsigned char *buffer)
{
  int retVal = 0;
  do {
    // Read 32KB at a time
    int batch_read_size = 64;

    queue_read_flash(flash_address, batch_read_size);
    queue_execute();

    bcopy(queue_read_data, buffer, 64 * 512);

  } while (0);
  if (retVal)
    printf("FAIL reading flash at $%08x\n", flash_address);
  return retVal;
}

int read_sector_from_device(const unsigned int sector_number, unsigned char *buffer)
{
  fseeko(fsdcard, sector_number * 512LL, SEEK_SET);
  fread(buffer, 512, 1, fsdcard);

  return 0;
}

// XXX - DO NOT USE A BUFFER THAT IS ON THE STACK OR BAD BAD THINGS WILL HAPPEN
int DIRTYMOCK(read_sector)(const unsigned int sector_number, unsigned char *buffer, int useCache, int readAhead)
{
  int retVal = 0;
  if (direct_sdcard_device)
    return read_sector_from_device(sector_number, buffer);

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
    log_error("failed to read sector %d", sector_number);
  return retVal;
}

unsigned char verify[512];

int write_sector_to_device(const unsigned int sector_number, unsigned char *buffer)
{
  fseeko(fsdcard, sector_number * 512LL, SEEK_SET);
  fwrite(buffer, 512, 1, fsdcard);

  return 0;
}

int DIRTYMOCK(write_sector)(const unsigned int sector_number, unsigned char *buffer)
{
  if (direct_sdcard_device)
    return write_sector_to_device(sector_number, buffer);

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
    log_error("failed to write sector %d", sector_number);
  return retVal;
}

int open_file_system(void)
{
  int retVal = 0;
  do {
    if (read_sector(0, mbr, CACHE_YES, 0)) {
      log_error("could not read MBR");
      retVal = -1;
      break;
    }

    for (int i = 0; i < 4; i++) {
      unsigned char *part_ent = &mbr[0x1be + (i * 0x10)];
      // dump_bytes(0,"partent",part_ent,16);
      if (part_ent[4] == 0x0c || part_ent[4] == 0x0b) {
        partition_start = part_ent[8] + (part_ent[9] << 8) + (part_ent[10] << 16) + (part_ent[11] << 24);
        partition_size = part_ent[12] + (part_ent[13] << 8) + (part_ent[14] << 16) + (part_ent[15] << 24);
        log_info("found FAT32 partition in partition slot %d : start sector=$%x, size=%d MB", i, partition_start,
            partition_size / 2048);
      }
      if (part_ent[4] == 0x41) {
        syspart_start = part_ent[8] + (part_ent[9] << 8) + (part_ent[10] << 16) + (part_ent[11] << 24);
        syspart_size = part_ent[12] + (part_ent[13] << 8) + (part_ent[14] << 16) + (part_ent[15] << 24);
        log_info("found MEGA65 system partition in partition slot %d : start sector=$%x, size=%d MB", i, syspart_start,
            syspart_size / 2048);
      }
    }

    if (partition_start == 0xffffffff && direct_sdcard_device) {
      if (strncmp((const char *)&mbr[0x52], "FAT32", 5) == 0) {
        partition_start = 0;
        partition_size = mbr[0x20] + (mbr[0x21] << 8) + (mbr[0x22] << 16) + (mbr[0x23] << 24);
        printf("Device is FAT32 partition, size=%d MB\n", partition_size / 2048);
      }
    }

    if (syspart_start && !nosys) {
      // Ok, so we know where the partition starts, so now find the FATs
      if (read_sector(syspart_start, syspart_sector0, CACHE_YES, 0)) {
        log_error("could not read system partition sector 0");
        retVal = -1;
        break;
      }
      if (strncmp("MEGA65SYS00", (char *)&syspart_sector0[0], 10)) {
        log_error("MEGA65 System Partition is missing MEGA65SYS00 marker");
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

    if (partition_start == 0xffffffff) {
      retVal = -1;
      break;
    }
    if (!partition_size) {
      retVal = -1;
      break;
    }

    // Ok, so we know where the partition starts, so now find the FATs
    if (read_sector(partition_start, fat_mbr, CACHE_YES, 0)) {
      log_error("could not read FAT MBR");
      retVal = -1;
      break;
    }

    if (fat_mbr[510] != 0x55) {
      log_error("invalid FAT MBR signature in sector %d ($%x)", partition_start, partition_start);
      retVal = -1;
      break;
    }
    if (fat_mbr[511] != 0xAA) {
      log_error("invalid FAT MBR signature in sector %d ($%x)", partition_start, partition_start);
      dump_bytes(0, "fat_mbr", fat_mbr, 512);
      retVal = -1;
      break;
    }
    if (fat_mbr[12] != 2) {
      log_error("FAT32 file system uses a sector size other than 512 bytes");
      retVal = -1;
      break;
    }
    if (fat_mbr[16] != 2) {
      log_error("FAT32 file system has more or less than 2 FATs");
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

    log_info("FAT32 file system has %dMB formatted capacity, first cluster = %d, %d sectors per FAT", data_sectors / 2048,
        first_cluster, sectors_per_fat);
    log_info("FATs begin at sector 0x%x and 0x%x", fat1_sector, fat2_sector);

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

BOOL name_match(struct m65dirent *de, char *name)
{
  return !strcasecmp(de->d_name, name) || !strcasecmp(de->d_longname, name);
}

unsigned char dir_sector_buffer[512];
unsigned int dir_sector = -1; // no dir
int dir_cluster = 0;
int dir_sector_in_cluster = 0;
int dir_sector_offset = 0;

BOOL vfatEntry = FALSE;
int vfat_entry_count = 0;
int vfat_dir_cluster = 0;
int vfat_dir_sector = 0;
int vfat_dir_sector_in_cluster = 0;
int vfat_dir_sector_offset = 0;

int fat_opendir(char *path, int show_errmsg)
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
        while (!fat_readdir(&d, FALSE)) {
          if (name_match(&d, path_seg)) {
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
              if (show_errmsg)
                log_error("%s is not a directory", path_seg);
              retVal = -1;
              break;
            }
          }
          if (retVal)
            break;
        }
        if (!found) {
          if (show_errmsg)
            log_error("could not find directory segment '%s'", path_seg);

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

    // log_debug("dir_cluster = $%x, dir_sector = $%x", dir_cluster, partition_start + dir_sector);

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

void copy_to_dnamechunk_from_offset(char *dnamechunk, int offset, int numuc2chars)
{
  for (int k = 0; k < numuc2chars; k++) {
    dnamechunk[k] = dir_sector_buffer[dir_sector_offset + offset + k * 2];
  }
}

void copy_vfat_chars_into_dname(char *dname, int seqnumber)
{
  // increment char-pointer to the seqnumber string chunk we'll copy across
  dname = dname + 13 * (seqnumber - 1);
  copy_to_dnamechunk_from_offset(dname, 0x01, 5);
  dname += 5;
  copy_to_dnamechunk_from_offset(dname, 0x0E, 6);
  dname += 6;
  copy_to_dnamechunk_from_offset(dname, 0x1C, 2);
}

void extract_out_dos8_3name(char *d_name)
{
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
        d_name[namelen++] = c;
      }
    }
    while (namelen && d_name[namelen - 1] == ' ')
      namelen--;

    // get the 3-byte extension
    if (dir_sector_buffer[dir_sector_offset + 8] && dir_sector_buffer[dir_sector_offset + 8] != ' ') {
      d_name[namelen++] = '.';
      for (int i = 0; i < 3; i++) {
        if (dir_sector_buffer[dir_sector_offset + 8 + i]) {
          int c = dir_sector_buffer[dir_sector_offset + 8 + i];
          if (extension_lowercase)
            c = tolower(c);
          d_name[namelen++] = c;
        }
      }
      while (namelen && d_name[namelen - 1] == ' ')
        namelen--;
    }
    d_name[namelen] = 0;
  }
}

int fat_readdir(struct m65dirent *d, int extend_dir_flag)
{
  int retVal = 0;
  int deletedEntry = 0;

  memset(d, 0, sizeof(struct m65dirent));

  vfatEntry = FALSE;
  vfat_entry_count = 0;

  do {
    retVal = advance_to_next_entry();

    if (extend_dir_flag && dir_sector == -1) { // did we exhaust the currently allocated clusters for these dir-entries?
      if (!extend_dir_cluster_chain())
        return -1;

      extend_dir_flag = FALSE;
      retVal = advance_to_next_entry();
    }

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
    if (dir_sector_buffer[dir_sector_offset + 0x0B] == 0x0F
        && dir_sector_buffer[dir_sector_offset] != 0x00 // assure this isn't an old vfat that has been wiped
        && dir_sector_buffer[dir_sector_offset] != 0xE5) {
      if (vfatEntry == FALSE) {
        vfat_dir_cluster = dir_cluster;
        vfat_dir_sector = dir_sector;
        vfat_dir_sector_in_cluster = dir_sector_in_cluster;
        vfat_dir_sector_offset = dir_sector_offset;
      }
      vfatEntry = TRUE;
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
        copy_vfat_chars_into_dname(d->d_longname, seqnumber);
        vfat_entry_count++;
        advance_to_next_entry();

        // if next dirent is not a vfat entry, break out
        if (dir_sector_buffer[dir_sector_offset + 0x0B] != 0x0F)
          break;
      } while (seqnumber != 1);
    }

    // gather debug info
    bcopy(&dir_sector_buffer[dir_sector_offset], d->de_raw, 32);
    d->de_cluster = dir_cluster;
    d->de_sector = partition_start + dir_sector;
    d->de_sector_offset = dir_sector_offset;

    assemble_time_from_raw(d->de_raw, d);

    // ignore any vfat files starting with '.' (such as mac osx '._*' metadata files)
    if (vfatEntry && d->d_longname[0] == '.') {
      // printf("._ vfat hide\n");
      d->d_name[0] = 0;
      return 0;
    }

    // ignored deleted vfat entries too (mac osx '._*' files are marked as deleted entries)
    if (deletedEntry) {
      d->d_name[0] = 0;
      return 0;
    }

    // if the DOS 8.3 entry is a deleted-entry (0xE5) then ignore
    if (dir_sector_buffer[dir_sector_offset] == 0xE5) {
      d->d_name[0] = 0;
      return 0;
    }

    // if the DOS 8.3 entry is a free-slot starts with (0x00) then mark it as such
    if (dir_sector_buffer[dir_sector_offset] == 0x00) {
      d->d_type = M65DT_FREESLOT;
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

    extract_out_dos8_3name(d->d_name);

    if (dirent_raw == 2 && d->d_name[0])
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
      log_error("cluster number too large. (cluster=%d, next_cluster=%d)", cluster, next_cluster);
      retVal = -1;
      break;
    }

    // Read in the sector of FAT1
    unsigned char fat_sector[512];
    if (read_sector(partition_start + fat1_sector + fat_sector_num, fat_sector, CACHE_YES, 0)) {
      log_error("failed to read sector $%x of first FAT", fat_sector_num);
      retVal = -1;
      break;
    }

    //    dump_bytes(0,"FAT sector",fat_sector,512);

    if (0)
      log_debug("marking cluster $%x in use by writing to offset $%x of FAT sector $%x", cluster, fat_sector_offset,
          fat_sector_num);

    // Set the bytes for this cluster to $0FFFFF8 to mark end of chain and in use
    fat_sector[fat_sector_offset + 0] = (next_cluster >> 0) & 0xff;
    fat_sector[fat_sector_offset + 1] = (next_cluster >> 8) & 0xff;
    fat_sector[fat_sector_offset + 2] = (next_cluster >> 16) & 0xff;
    fat_sector[fat_sector_offset + 3] = (next_cluster >> 24) & 0x0f;

    if (0)
      log_debug("Marking cluster in use in FAT1");

    // Write sector back to FAT1
    if (write_sector(partition_start + fat1_sector + fat_sector_num, fat_sector)) {
      log_error("failed to write updated FAT sector $%x to FAT1", fat_sector_num);
      retVal = -1;
      break;
    }

    if (0)
      printf("Marking cluster in use in FAT2\n");

    // Write sector back to FAT2
    if (write_sector(partition_start + fat2_sector + fat_sector_num, fat_sector)) {
      log_error("failed to write updated FAT sector $%x to FAT1", fat_sector_num);
      retVal = -1;
      break;
    }

    if (0)
      log_debug("done allocating cluster");

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
      log_error("cluster number too large");
      retVal = -1;
      break;
    }

    // Read in the sector of FAT1
    unsigned char fat_sector[512];
    if (read_sector(partition_start + fat1_sector + fat_sector_num, fat_sector, CACHE_YES, 0)) {
      log_error("failed to read sector $%x of first FAT", fat_sector_num);
      retVal = -1;
      break;
    }

    //    dump_bytes(0,"FAT sector",fat_sector,512);

    if (0)
      log_debug("marking cluster $%x in use by writing to offset $%x of FAT sector $%x", cluster, fat_sector_offset,
          fat_sector_num);

    // Set the bytes for this cluster to $0FFFFF8 to mark end of chain and in use
    fat_sector[fat_sector_offset + 0] = (value)&0xff;
    fat_sector[fat_sector_offset + 1] = (value >> 8) & 0xff;
    fat_sector[fat_sector_offset + 2] = (value >> 16) & 0xff;
    fat_sector[fat_sector_offset + 3] = (value >> 24) & 0xff;

    if (0)
      log_debug("marking cluster in use in FAT1");

    // Write sector back to FAT1
    if (write_sector(partition_start + fat1_sector + fat_sector_num, fat_sector)) {
      log_error("failed to write updated FAT sector $%x to FAT1", fat_sector_num);
      retVal = -1;
      break;
    }

    if (0)
      log_debug("marking cluster in use in FAT2");

    // Write sector back to FAT2
    if (write_sector(partition_start + fat2_sector + fat_sector_num, fat_sector)) {
      log_error("failed to write updated FAT sector $%x to FAT1", fat_sector_num);
      retVal = -1;
      break;
    }

    if (0)
      log_debug("done allocating cluster");

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
      log_error("cluster number too large");
      retVal = -1;
      break;
    }

    // Read in the sector of FAT1
    unsigned char fat_sector[512];
    if (read_sector(partition_start + fat1_sector + fat_sector_num, fat_sector, CACHE_YES, 0)) {
      log_error("failed to read sector $%x of first FAT", fat_sector_num);
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
    log_error("failed to read sector $%x of first FAT", i);
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
        log_error("failed to read sector $%x of first FAT", i);
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
  void *item;
  struct _llist *next;
} llist;

void llist_free(llist *lstitem)
{
  llist *next;
  while (lstitem != NULL) {
    free(lstitem->item);
    next = lstitem->next;
    free(lstitem);
    lstitem = next;
  }
}

llist *llist_new(void)
{
  llist *lst = (llist *)malloc(sizeof(llist));
  memset(lst, 0, sizeof(llist));
  return lst;
}

void llist_add(llist *lst, void *item, int compare(void *, void *))
{
  if (lst->item == NULL) {
    lst->item = item;
    return;
  }

  llist *prev = NULL;

  while (lst != NULL) {
    // we found a home for it?
    if (compare(lst->item, item) > 0) {
      llist *mvlst = llist_new();
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
  llist *lstnew = llist_new();
  lstnew->item = item;
  prev->next = lstnew;
}

int compare_dirents(void *s, void *d)
{
  struct m65dirent *src = (struct m65dirent *)s;
  struct m65dirent *dest = (struct m65dirent *)d;
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

int read_direntries(llist *lst, char *path)
{
  struct m65dirent de;

  if (fat_opendir(path, TRUE)) {
    return 0;
  }
  // log_debug("Opened directory, dir_sector=%d (absolute sector = %d)",dir_sector,partition_start+dir_sector);
  while (!fat_readdir(&de, FALSE)) {
    if (!de.d_name[0])
      continue;
    struct m65dirent *denew = (struct m65dirent *)malloc(sizeof(struct m65dirent));
    memcpy(denew, &de, sizeof(struct m65dirent));
    llist_add(lst, denew, compare_dirents);
  }

  return 1;
}

int contains_dir(llist *lst, char *path)
{
  while (lst != NULL && lst->item != NULL) {
    struct m65dirent *itm = (struct m65dirent *)lst->item;
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
int is_match(char *line, char *pattern, int case_sensitive)
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

void show_local_directory(char *searchpattern)
{
  DIR *d;
  struct dirent *dir;

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

void change_local_dir(char *path)
{
  if (chdir(path))
    printf("ERROR: Failed to change directory (%s)!\n", path);
}

void change_dir(char *path)
{
  if (path[0] == '/') {
    // absolute path
    if (fat_opendir(path, TRUE)) {
      fprintf(stderr, "ERROR: Could not open directory '%s'\n", path);
    }
    else
      strcpy(current_dir, path);
  }
  else {
    // relative path
    char temp_path[1024], src_copy[1024];
    {
      // Apply each path segment, handling . and .. appropriately
      snprintf(temp_path, 1024, "%s", current_dir);
      strcpy(src_copy, path);
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

    if (fat_opendir(temp_path, TRUE)) {
      fprintf(stderr, "ERROR: Could not open directory '%s'\n", temp_path);
    }
    else
      strcpy(current_dir, temp_path);
  }
}

int read_remote_dirents(llist *lst_dirents, char *path, char **psearchterm)
{
  if (!file_system_found)
    open_file_system();
  if (!file_system_found) {
    fprintf(stderr, "ERROR: Could not open file system.\n");
    return FALSE;
  }

  // check if it's an absolute path to a folder
  if (fat_opendir(path, FALSE) == 0) {
    // if so read direntries within it
    if (!read_direntries(lst_dirents, path))
      return FALSE;
  }
  // if not abs-path, then assume it's a file/dir/wildcard for the current working directory
  else {
    if (!read_direntries(lst_dirents, current_dir))
      return FALSE;

    // check if the user wants to 'dir' a sub-folder
    if (contains_dir(lst_dirents, path)) {
      llist_free(lst_dirents);
      lst_dirents = llist_new();

      if (!read_direntries(lst_dirents, path))
        return FALSE;
    }
    else if (strcmp(path, current_dir) != 0)
      *psearchterm = path;
  }

  return TRUE;
}

char *get_datetime_str(struct tm *tm)
{
  static char s[20]; /* strlen("2009-08-10 18:17:54") + 1 */
  strftime(s, 20, "%Y-%m-%d %H:%M:%S", tm);

  return s;
}

int show_directory(char *path)
{
  int dir_count = 0;
  int file_count = 0;

  if (!path || strlen(path) == 0)
    path = current_dir;

  llist *lst_dirents = llist_new();
  char *searchterm = NULL;

  int retVal = 0;

  do {
    if (!read_remote_dirents(lst_dirents, path, &searchterm)) {
      retVal = -1;
      break;
    }

    llist *cur = lst_dirents;
    while (cur != NULL && cur->item != NULL) {
      struct m65dirent *itm = (struct m65dirent *)cur->item;

      if (searchterm && !is_match(itm->d_name, searchterm, TRUE) && !is_match(itm->d_longname, searchterm, TRUE)) {
        cur = cur->next;
        continue;
      }

      if (itm->d_attr & 0x10) {
        dir_count++;
        printf("       <DIR> | %-20s | %-12s | %s\n", get_datetime_str(&itm->d_time), itm->d_name, itm->d_longname);
      }
      else if (itm->d_name[0] && itm->d_filelen >= 0) {
        file_count++;
        printf("%12d | %-20s | %-12s | %s\n", (int)itm->d_filelen, get_datetime_str(&itm->d_time), itm->d_name,
            itm->d_longname);
      }
      if (dirent_raw == 1) {
        dump_bytes(0, "dirent raw", itm->de_raw, 32);
        printf("type: $%02X, cluster=%ld ($%08lX), sector=%ld, sec_offset=$%04X\n"
               "  data_cluster=%ld ($%08lX)\n",
            itm->d_type, itm->de_cluster, itm->de_cluster, itm->de_sector, itm->de_sector_offset, itm->d_ino, itm->d_ino);
        // show dirent_raw and parse info here
      }
      cur = cur->next;
    }
  } while (0);
  printf("%d Dir(s), %d File(s)\n", dir_count, file_count);

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

    int next_clustermap_sector = partition_start + first_cluster_sector
                               + (clustermap_val - first_cluster) * sectors_per_cluster;
    printf("%d:  %d  ($%08X)  (sector=%d)\n", clustermap_idx, clustermap_val, clustermap_val, next_clustermap_sector);
  }
}

void show_cluster(int cluster_num)
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
  FILE *fsave = fopen(secdump_file, "wb");
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

  FILE *fload = fopen(secrestore_file, "rb");
  for (int sector = secrestore_start; sector < (secrestore_start + secrestore_count); sector++) {
    fread(dir_sector_buffer, 1, 512, fload);
    write_sector(sector, dir_sector_buffer);
    printf("\rLoading... (%d%%)", (sector - secrestore_start) * 100 / secrestore_count);
  }
  fclose(fload);
  execute_write_queue();
  printf("\rLoaded file \"%s\" at starting-sector %d.\n", secrestore_file, secrestore_start);
}

int parse_value(char *strval)
{
  int retval = 0;

  // hexadecimal value?
  if (strval[0] == '$') {
    sscanf(&strval[1], "%x", &retval);
  }
  else {
    sscanf(strval, "%d", &retval);
  }

  return retval;
}

void poke_sector(void)
{
  read_sector(poke_secnum, dir_sector_buffer, CACHE_YES, 0);
  dir_sector_buffer[poke_offset] = poke_value;
  write_sector(poke_secnum, dir_sector_buffer);
}

void parse_pokes(char *cmd)
{
  char *tok = strtok(cmd, " ");

  if (tok == NULL) {
    printf("ERROR: invalid arguments: Sector number not found\n");
    return;
  }
  poke_secnum = parse_value(tok);

  if ((tok = strtok(NULL, " ")) == NULL) {
    printf("ERROR: invalid arguments: Sector offset not found\n");
    return;
  }

  poke_offset = parse_value(tok);

  while ((tok = strtok(NULL, " ")) != NULL) {
    poke_value = parse_value(tok);
    poke_sector();

    poke_offset++;
    if (poke_offset >= 512) {
      poke_offset = 0;
      poke_secnum++;
    }
  }

  // Flush any pending sector writes out
  execute_write_queue();
}

int endswith(char *fname, char *ext)
{
  char *actual_ext = strrchr(fname, '.');
  if (!ext)
    return 0;

  if (strcmp(ext, actual_ext) == 0)
    return 1;

  return 0;
}

void perform_filehost_read(char *searchterm)
{
  if (username != NULL) {
    log_in_and_get_cookie(username, password);
  }

  read_filehost_struct(searchterm);
}

int get_first_sector_of_file(char *name)
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
  llist *lst_dirents = llist_new();
  char *searchterm = "MEGA6*.ROM";

  do {
    if (!file_system_found)
      open_file_system();
    if (!file_system_found) {
      fprintf(stderr, "ERROR: Could not open file system.\n");
      break;
    }

    if (!read_direntries(lst_dirents, "/"))
      break;

    llist *cur = lst_dirents;
    while (cur != NULL) {
      struct m65dirent *itm = (struct m65dirent *)cur->item;

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
        strncpy(version, (char *)show_buf + 0x16, 7);
        version[7] = '\0';
      }
      // open-rom
      else if (show_buf[0x10] == 'O' || show_buf[0x10] == 'V') {
        strncpy(version, (char *)show_buf + 0x10, 9);
        version[9] = '\0';
      }

      if (itm->d_name[0] && itm->d_filelen >= 0)
        printf("%11s - %s\n", itm->d_name, version);
      cur = cur->next;
    }
  } while (0);

  llist_free(lst_dirents);
}

void wrap_upload(char *fname)
{
  char *d81name;
  if (!(d81name = create_d81_for_prg(fname)))
    return;
  strcpy(fname, d81name);

  if (fname) {
    upload_file(fname, fname);
  }
  else {
    printf("ERROR: Unable to download file from filehost!\n");
  }
}

void perform_filehost_get(int num, char *destname)
{
  char *fname = download_file_from_filehost(num);

  if (endswith(fname, ".prg") || endswith(fname, ".PRG")) {
    char *d81name = create_d81_for_prg(fname);
    strcpy(fname, d81name);
  }

  if (destname != NULL && strcmp(destname, "-") == 0)
    return;

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
  char *name;
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

models_type *get_model(uint8_t model_id)
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

int get_model_id_from_core_file(char *corefile)
{
  FILE *f = fopen(corefile, "rb");

  unsigned char buffer[512];
  bzero(buffer, 512);
  int bytes = fread(buffer, 1, 512, f);

  fclose(f);

  if (bytes > 0x70)
    return buffer[0x70];

  return 0;
}

int check_model_id_field(char *corefile)
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

void write_tcl_script(models_type *mdl)
{
  char fpga[128];
  char qspi[128];
  char hwcfg[180];
  char hwcfgtype[180];
  sprintf(fpga, "[lindex [get_hw_devices %s] 0]", mdl->fpga_part);
  sprintf(qspi, "[lindex [get_cfgmem_parts {%s}] 0]", mdl->qspi_part);
  sprintf(hwcfg, "[ get_property PROGRAM.HW_CFGMEM %s]", fpga);
  sprintf(hwcfgtype, "[ get_property PROGRAM.HW_CFGMEM_TYPE %s]", fpga);

  FILE *f = fopen("write-flash.tcl", "wt");
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

int bit2mcs(int argc, char *argv[]);

BOOL initial_flashing_checks(void)
{
  if (ethernet_mode) {
    log_error("Flashing of .cor files not supported in Ethernet mode");
    return FALSE;
  }

  // assess if running in xemu. If so, exit
  if (xemu_flag) {
    printf("%d - This command is not available for xemu.\n", xemu_flag);
    return FALSE;
  }

  // Query m65-hardware to learn what the m65model type is
  // Based on model-type, assess if slotnum is valid
  uint8_t hardware_model_id = mega65_peek(0xFFD3629);
  models_type *mdl = get_model(hardware_model_id);
  if (slotnum >= mdl->slot_count) {
    printf("- Valid slots on your hardware range from 0 to %d.\n", mdl->slot_count - 1);
    return FALSE;
  }

  return TRUE;
}

void flash_core_to_slot(char *fname, int slotnum)
{
  uint8_t hardware_model_id = mega65_peek(0xFFD3629);
  models_type *mdl = get_model(hardware_model_id);

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
  char *argv[4] = { "progname", fname, "out.mcs", offset };
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
  char vivado_cmd[576];
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

void perform_flash(char *fname, int slotnum)
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
  char *fname = download_file_from_filehost(fhnum);

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
  printf("      Bytes per logical sector = %d\n", *(unsigned short *)&sector[0x0B]);
  printf("      Logical sectors per cluster = %d\n", sector[0x0D]);
  printf("      Count of reserved logical sectors before 1st FAT = %d\n", *(unsigned short *)&sector[0x0E]);
  printf("      Number of FATs = %d\n", sector[0x10]);
  printf("      Max no# of FAT12/16 root dir entries = %d\n", *(unsigned short *)&sector[0x11]);
  printf("      Total logical sector (0 for FAT32) = %d\n", *(unsigned short *)&sector[0x13]);
  printf("      Media Descriptor = 0x%02X\n", sector[0x15]);
  printf("      Logical sectors per FAT (0 for FAT32) = %d\n", *(unsigned short *)&sector[0x16]);
  printf("    }\n");
  printf("    Physical sectors per track (for INT 13h CHS geometry) = %d\n", *(unsigned short *)&sector[0x18]);
  printf("    Number of heads (for disks with INT 13h CHS geometry) = %d\n", *(unsigned short *)&sector[0x1A]);
  printf("    Count of hidden sectors preceding the partition of this FAT volume = %d\n", *(unsigned int *)&sector[0x1C]);
  printf("    Total logical sectors (if greater than 65535) = %d\n", *(unsigned int *)&sector[0x20]);
  printf("  }\n");
  printf("  Logical sectors per FAT = %d\n", *(unsigned int *)&sector[0x24]);
  printf("  Drive description / mirroring flags = 0x%02X 0x%02X\n", sector[0x28], sector[0x29]);
  printf("  Version = 0x%02X 0x%02X\n", sector[0x2A], sector[0x2B]);
  printf("  Cluster number of root directory start = %d\n", *(unsigned int *)&sector[0x2C]);
  printf("  Logical sector number of FS Information Sector = %d\n", *(unsigned short *)&sector[0x30]);
  printf("  First logical sector number of copy of 3 FAT boot sectors = %d\n", *(unsigned short *)&sector[0x32]);
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
    printf("- First sector LBA: %d (sector position)\n", *(unsigned int *)&sector[pt_ofs + 0x08]);
    printf("- Number of sectors: %d\n", *(unsigned int *)&sector[pt_ofs + 0x0C]);

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

int delete_single_file(char *name)
{
  struct m65dirent de;

  if (check_file_system_access() == -1)
    return -1;

  if (fat_opendir(current_dir, TRUE))
    return -1;

  if (!find_file_in_curdir(name, &de)) {
    printf("File %s does not exist.\n", name);
    return -1;
  }

  unsigned int first_cluster_of_file = get_first_cluster_of_file();

  // check if it is a DE_ATTRIB_FILE or DE_ATTRIB_DIR
  int attrib = dir_sector_buffer[dir_sector_offset + 0x0b];

  if (attrib == DE_ATTRIB_DIR) {
    printf("TODO: Unable to delete directories as yet. Raise a ticket\n");
    // NOTE: Will need to assess if directory is empty.
    //      If not empty, then tell use the dir has xxx dirs and xxx files, are they sure they want to delete?
    //      If yes, then will need a recursive strategy to delete files within dirs first before deleting dir
    return -1;
  }

  // remove dir-entry from FAT table (including any preceding vfat-lfn entries)
  wipe_direntries_of_current_file_or_dir();

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
  } while (current_cluster > 0 && current_cluster < FAT32_MIN_END_OF_CLUSTER_MARKER);

  // Flush any pending sector writes out
  execute_write_queue();

  printf("File '%s' successfully deleted\n", name);
  return 0;
}

int delete_file_or_dir(char *name)
{
  llist *lst_dirents = llist_new();
  char *searchterm = NULL; // ignore this for now (borrowed it from elsewhere)

  // if no wildcards in name, then just delete a single file
  if (!strstr(name, "*"))
    return delete_single_file(name);

  // if wildcard on delete, confirm with user first
  char inp[128];
  printf("Are you sure (y/n)? ");
  scanf("%s", inp);
#ifdef WINDOWS
  fflush(stdin);
#endif
  if (!(strcmp(inp, "Y") == 0 || strcmp(inp, "y") == 0))
    return FALSE;

  // handle wildcards
  if (!read_remote_dirents(lst_dirents, current_dir, &searchterm)) {
    return FALSE;
  }

  llist *cur = lst_dirents;

  while (cur != NULL) {
    struct m65dirent *itm = (struct m65dirent *)cur->item;

    if (!is_match(itm->d_name, name, 0) && !is_match(itm->d_longname, name, 0)) {
      cur = cur->next;
      continue;
    }

    if (itm->d_attr & 0x10) {
      ; // this is a DIR
    }
    else if (itm->d_name[0] && itm->d_filelen >= 0) {
      if (itm->d_longname[0])
        delete_single_file(itm->d_longname);
      else
        delete_single_file(itm->d_name);
    }

    cur = cur->next;
  }

  // Flush any pending sector writes out
  execute_write_queue();

  return TRUE;
}

// returns:
// -1 = problems opening file-system
// 0 = doesn't exist
// 1 = exists
int contains_file_or_dir(char *name)
{
  struct m65dirent de;

  if (check_file_system_access() == -1)
    return -1;

  if (fat_opendir(current_dir, TRUE))
    return -1;

  // printf("Opened directory, dir_sector=%d (absolute sector = %d)\n",dir_sector,partition_start+dir_sector);
  while (!fat_readdir(&de, FALSE)) {
    // if (de.d_name[0]) printf("'%s'   %d\n",de.d_name,(int)de.d_filelen);
    // else dump_bytes(0,"empty dirent",&dir_sector_buffer[dir_sector_offset],32);
    if (name_match(&de, name)) {
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

void wipe_direntries_of_current_file_or_dir(void)
{
  if (vfatEntry) {
    dir_cluster = vfat_dir_cluster;
    dir_sector = vfat_dir_sector;
    dir_sector_in_cluster = vfat_dir_sector_in_cluster;
    dir_sector_offset = vfat_dir_sector_offset
                      - 32; // the -32 is needed due to advance_to_next_entry() call doing a +32 initially

    read_sector(partition_start + dir_sector, dir_sector_buffer, CACHE_YES, 0);

    for (int i = 0; i < vfat_entry_count; i++) {
      advance_to_next_entry();

      unsigned char *dir = &dir_sector_buffer[dir_sector_offset];
      bzero(dir, 32);
      dir[0] = 0xE5;
      write_sector(partition_start + dir_sector, dir_sector_buffer);
    }
    advance_to_next_entry();
  }
  // delete the 8.3 entry
  unsigned char *dir = &dir_sector_buffer[dir_sector_offset];
  bzero(dir, 32);
  dir[0] = 0xE5;
  write_sector(partition_start + dir_sector, dir_sector_buffer);

  execute_write_queue();
}

int rename_file_or_dir(char *name, char *dest_name)
{
  int retVal = 0;
  do {

    if (!contains_file_or_dir(name)) {
      printf("ERROR: File %s does not exist.\n", name);
      return -1;
    }

    if (contains_file_or_dir(dest_name) == 1) {
      printf("ERROR: Cannot rename to \"%s\", as this file already exists.\n", dest_name);
      return -2;
    }

    char short_name[8 + 3 + 1];

    // Normalise dest_name into 8.3 format.
    BOOL needs_long_name = normalise_long_name(dest_name, short_name, current_dir);

    // need to call this again to set various global variable details for the found file properly
    contains_file_or_dir(name);

    // check if it is a DE_ATTRIB_FILE or DE_ATTRIB_DIR
    int attrib = dir_sector_buffer[dir_sector_offset + 0x0b];

    // Calculate checksum of 8.3 name
    unsigned char lfn_csum = lfn_checksum((unsigned char *)short_name);

    unsigned int first_cluster_of_file = calc_first_cluster_of_file();
    unsigned int size = calc_size_of_file();

    wipe_direntries_of_current_file_or_dir();

    int direntries_needed = 1 + calculate_needed_direntries_for_vfat(dest_name);

    if (!find_contiguous_free_direntries(direntries_needed)) {
      printf("ERROR: Unable to locate %d contiguous free dir entries\n", direntries_needed);
      return -1;
    }

    if (!create_directory_entry_for_item(dest_name, short_name, lfn_csum, needs_long_name, attrib)) {
      printf("ERROR: Failed to create dir entry for file\n");
      return -1;
    }

    write_cluster_number_into_direntry(first_cluster_of_file);
    write_file_size_into_direntry(size);

    write_sector(partition_start + dir_sector, dir_sector_buffer);

    // Flush any pending sector writes out
    execute_write_queue();

  } while (0);

  return retVal;
}

// #ifdef USE_LFN

BOOL is_long_name_needed(char *long_name, char *short_name)
{
  int needs_long_name = FALSE;
  int dot_count = 0;
  int base_len = 0;
  int ext_len = 0;

  char *lastdot = strrchr(long_name, '.');

  for (int i = 0; long_name[i]; i++) {
    if (long_name[i] == '.') {
      if (i == 0) { // ignore dot as start of name
        needs_long_name = TRUE;
        continue;
      }
      if (lastdot == &long_name[i]) {
        dot_count++;
        ext_len = 0;
      }
      else
        needs_long_name = TRUE;
    }
    else {
      if (long_name[i] == ' ')
        continue;
      if (toupper(long_name[i]) != long_name[i])
        needs_long_name = TRUE;
      if (dot_count == 0) {
        if (base_len < 8) {
          short_name[base_len] = toupper(long_name[i]);
          base_len++;
        }
        else {
          needs_long_name = TRUE;
        }
      }
      else if (dot_count == 1) {
        if (ext_len < 3) {
          short_name[8 + ext_len] = toupper(long_name[i]);
          ext_len++;
        }
        else {
          needs_long_name = TRUE;
        }
      }
    }
  }

  return needs_long_name;
}

int num_digits(int i)
{
  int count = 0;
  do {
    i /= 10;
    count++;
  } while (i != 0);

  return count;
}

void put_tilde_number_in_shortname(char *short_name, int i)
{
  int length_of_number = num_digits(i);
  int ofs = 7;
  char temp[9];

  // 0123456789A
  // VIZ     D81

  // locate first space in first 8 chars (the ~1 will be placed in there)
  while (short_name[ofs] == ' ')
    ofs--;
  ofs++;
  if (ofs > 7 - length_of_number)
    ofs = 7 - length_of_number;

  sprintf(temp, "~%d", i);
  bcopy(temp, &short_name[ofs], strlen(temp));
  // printf("  considering short-name '%s'...\n", short_name);
}

BOOL is_tilde_needed(char *longname)
{
  int basename_len = strlen(longname);
  int suffix_len = 0;

  // if there's a dot at the start of the longname, then we must have a tilde
  if (longname[0] == '.')
    return TRUE;

  char *pdot = strrchr(longname, '.');

  if (pdot) {
    basename_len = (int)(pdot - longname);
    suffix_len = strlen(pdot) - 1;
  }

  if (basename_len <= 8 && suffix_len <= 3)
    return FALSE;

  return TRUE;
}

void get_dotted_shortname(char *short_name_with_dot, char *short_name)
{
  int i = 0;
  for (i = 0; i < 8; i++) {
    if (short_name[i] != ' ') {
      short_name_with_dot[i] = short_name[i];
    }
    else
      break;
  }

  short_name_with_dot[i] = '.';
  i++;

  for (int k = 8; k < 11; k++) {
    if (short_name[k] != ' ') {
      short_name_with_dot[i] = short_name[k];
      i++;
    }
    else {
      if (k == 8) { // if we don't have any extension, remove the dot
        i--;
      }

      break;
    }
  }

  short_name_with_dot[i] = '\0';
}

// returns: 0 = doesn't need long name, 1 = needs long name
int normalise_long_name(char *long_name, char *short_name, char *dir_name)
{
  struct m65dirent de;
  int needs_long_name = 0;
  char short_name_with_dot[13] = { 0 };

  strcpy(short_name, "           ");

  // Handle . and .. special cases
  if (!strcmp(long_name, ".")) {
    bcopy(".          ", short_name, 8 + 3);
    return 0;
  }
  if (!strcmp(long_name, "..")) {
    bcopy("..         ", short_name, 8 + 3);
    return 0;
  }

  needs_long_name = is_long_name_needed(long_name, short_name);

  if (needs_long_name && is_tilde_needed(long_name)) {
    // Put ~X suffix on base name.
    // XXX Needs to be unique in the sub-directory
    for (int i = 1;; i++) { // endless loop till we find an available short-name
      put_tilde_number_in_shortname(short_name, i);

      get_dotted_shortname(short_name_with_dot, short_name);

      // iterate through the directory looking for
      if (!find_file_in_curdir(short_name_with_dot, &de)) {
        // This name permutation is unused presently, so let's use it
        break;
      }
      if (!strcasecmp(long_name, de.d_longname)) {
        // TODO: assess if longfile matches too. If so, then break out too
        // (since an upload of same file should override it)
        break;
      }
    } // end while
  }

  if (needs_long_name && !is_tilde_needed(long_name)) {
    get_dotted_shortname(short_name_with_dot, short_name);
  }

  if (needs_long_name)
    printf("- Using DOS8.3 name of '%s'\n", short_name_with_dot);

  return needs_long_name;
}

unsigned char lfn_checksum(const unsigned char *pFCBName)
{
  int i;
  unsigned char sum = 0;

  for (i = 11; i; i--)
    sum = ((sum & 1) << 7) + (sum >> 1) + *pFCBName++;

  return sum;
}
// #endif

void wipe_cluster(int cluster)
{
  unsigned char buffer[512];
  int sector = partition_start + first_cluster_sector + (cluster - first_cluster) * sectors_per_cluster;

  memset(buffer, 0, 512);

  for (int k = 0; k < sectors_per_cluster; k++) {
    write_sector(sector + k, buffer);
  }
}

int extend_dir_cluster_chain(void)
{
  int next_cluster = find_free_cluster(dir_cluster);
  if (allocate_cluster(next_cluster)) {
    printf("ERROR: Could not allocate cluster $%x (for directory)\n", next_cluster);
    return FALSE;
  }
  if (chain_cluster(dir_cluster, next_cluster)) {
    printf("ERROR: Could not chain cluster $%x to $%x (for directory)\n", dir_cluster, next_cluster);
    return FALSE;
  }

  // empty the new cluster (to assure no other data resides in there that could be mistake for dir-entries)
  wipe_cluster(next_cluster);
  execute_write_queue();

  // update vars
  dir_cluster = next_cluster;
  dir_sector = first_cluster_sector + (dir_cluster - first_cluster) * sectors_per_cluster;
  dir_sector_offset = -32;
  dir_sector_in_cluster = 0;

  read_sector(partition_start + dir_sector, dir_sector_buffer, CACHE_YES, 0);

  return TRUE;
}

int calculate_needed_direntries_for_vfat(char *filename)
{
  int length = strlen(filename);

  if (length > 255) {
    printf("ERROR: Long file names over 255 characters are not allowed");
    exit(-1);
  }

  int vfat_entries = (length - 1) / 13 + 1; // each LFN entry can contain a max of 13 characters

  return vfat_entries;
}

void write_cluster_number_into_direntry(int a_cluster)
{
  dir_sector_buffer[dir_sector_offset + 0x1A] = (a_cluster >> 0) & 0xff;
  dir_sector_buffer[dir_sector_offset + 0x1B] = (a_cluster >> 8) & 0xff;
  dir_sector_buffer[dir_sector_offset + 0x14] = (a_cluster >> 16) & 0xff;
  dir_sector_buffer[dir_sector_offset + 0x15] = (a_cluster >> 24) & 0xff;
}

void write_file_size_into_direntry(unsigned int size)
{
  dir_sector_buffer[dir_sector_offset + 0x1C] = (size >> 0) & 0xff;
  dir_sector_buffer[dir_sector_offset + 0x1D] = (size >> 8) & 0xff;
  dir_sector_buffer[dir_sector_offset + 0x1E] = (size >> 16) & 0xff;
  dir_sector_buffer[dir_sector_offset + 0x1F] = (size >> 24) & 0xff;
}

BOOL create_direntry_with_attrib(char *dest_name, int attrib)
{
  char short_name[8 + 3 + 1];

  if (fat_opendir(current_dir, TRUE)) {
    return FALSE;
  }

  // Normalise dest_name into 8.3 format.
  BOOL needs_long_name = normalise_long_name(dest_name, short_name, current_dir);

  // Calculate checksum of 8.3 name
  unsigned char lfn_csum = lfn_checksum((unsigned char *)short_name);

  int direntries_needed = 1;
  if (needs_long_name)
    direntries_needed = 1 + calculate_needed_direntries_for_vfat(dest_name);

  if (!find_contiguous_free_direntries(direntries_needed)) {
    printf("ERROR: Unable to locate %d contiguous free dir entries\n", direntries_needed);
    return FALSE;
  }

  if (!create_directory_entry_for_item(dest_name, short_name, lfn_csum, needs_long_name, attrib)) {
    printf("ERROR: Failed to create dir entry for file\n");
    return FALSE;
  }

  return TRUE;
}

int upload_single_file(char *name, char *dest_name)
{
  struct m65dirent de;
  int retVal = 0;
  do {
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

    BOOL file_exists = find_file_in_curdir(dest_name, &de);

    if (file_exists) {
      // assess how many contiguous clusters it consumes right now.
      int num_clusters = get_cluster_count(dest_name);
      int clusters_needed = (st.st_size - 1) / (512 * sectors_per_cluster) + 1;

      if (num_clusters != clusters_needed) {
        delete_file_or_dir(dest_name);
        file_exists = FALSE;
      }
    }

    if (!file_exists) {
      // File does not (yet) exist, get ready to create it
      printf("%s does not yet exist on the file system -- searching for empty directory slot to create it in.\n", dest_name);

      if (!create_direntry_with_attrib(dest_name, DE_ATTRIB_FILE)) {
        printf("ERROR: Failed to create dir entry for file\n");
        return -1;
      }
    }

    if (dir_sector == -1) {
      printf("ERROR: Drive is full.\n");
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

      int a_cluster = find_contiguous_clusters((st.st_size - 1) / (512 * sectors_per_cluster) + 1);
      // E.g. If size = 4096 bytes, it fits in 1 cluster.
      //      If size = 4097 bytes, it fits in 2 clusters
      //      If size = 8192 bytes, it fits in 2 clusters
      //      If size = 8193 bytes, it fits in 3 clusters

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
      write_cluster_number_into_direntry(a_cluster);

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
    unsigned long long last_status_output = 0;
    unsigned int sector_number;
    FILE *f = fopen(name, "rb");

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
        if (next_cluster == 0 || next_cluster >= FAT32_MIN_END_OF_CLUSTER_MARKER) {
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
        if (next_cluster <= 0) {
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
      unsigned long long now = gettime_ms();
      if (now - last_status_output > 100) {
        printf("\rUploaded %lld bytes.", (long long)st.st_size - remaining_length);
        fflush(stdout);
        last_status_output = now;
      }

      if (write_sector(sector_number, buffer)) {
        printf("ERROR: Failed to write to sector %d\n", sector_number);
        retVal = -1;
        break;
      }
      //      printf("T+%lld : after write.\n",gettime_us()-start_usec);

      sector_in_cluster++;
      remaining_length -= 512;
    }

    fclose(f);

    // XXX check for orphan clusters at the end, and if present, free them.

    // Write file size into directory entry
    write_file_size_into_direntry((unsigned int)st.st_size);

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

int upload_file(char *name, char *dest_name)
{
  DIR *d;
  struct dirent *dir;

  // if no wildcards in name, then just upload a single file
  if (!strstr(name, "*"))
    return upload_single_file(name, dest_name);

  // check for wildcards in name
  // list directory first
  d = opendir(".");
  if (d) {
    while ((dir = readdir(d)) != NULL) {
      if (!is_match(dir->d_name, name, 1))
        continue;

      struct stat file_stats;
      if (!stat(dir->d_name, &file_stats)) {
        if (!S_ISDIR(file_stats.st_mode)) {
          printf("\nUploading \"%s\"...\n", dir->d_name);
          upload_single_file(dir->d_name, dir->d_name);
        }
      }
    }
  }
  closedir(d);
  return 0;
}

void assemble_time_into_raw_at_offset(unsigned char *buffer, int offs, struct tm *tm)
{
  buffer[0x00 + offs] = (tm->tm_sec >> 1) & 0x1F; // 2 second resolution
  buffer[0x00 + offs] |= (tm->tm_min & 0x7) << 5;

  buffer[0x00 + offs + 1] = (tm->tm_min >> 3) & 0x7;
  buffer[0x00 + offs + 1] |= (tm->tm_hour) << 3;

  buffer[0x00 + offs + 2] = tm->tm_mday & 0x1f;
  buffer[0x00 + offs + 2] |= ((tm->tm_mon + 1) & 0x7) << 5;

  buffer[0x00 + offs + 3] = ((tm->tm_mon + 1) >> 3) & 0x1;
  buffer[0x00 + offs + 3] |= (tm->tm_year - 80) << 1;
}

void assemble_time_into_raw(unsigned char *buffer, struct tm *tm)
{
  // create time+date
  assemble_time_into_raw_at_offset(buffer, 0x0e, tm);

  // last-modifiied time+date
  assemble_time_into_raw_at_offset(buffer, 0x16, tm);
}

// Must be a single path segment.  Creating sub-directories requires multiple chdir/cd + mkdir calls
int create_dir(char *dest_name)
{
  int parent_cluster = 2;
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

    // assure dir_cluster points to the first cluster of the current directory's direntries)
    if (fat_opendir(current_dir, TRUE)) {
      return FALSE;
    }

    parent_cluster = dir_cluster;

    BOOL file_exists = contains_file_or_dir(dest_name);
    if (file_exists) {
      fprintf(stderr, "ERROR: File or directory '%s' already exists.\n", dest_name);
      retVal = -1;
      break;
    }

    if (!file_exists) {
      // File does not (yet) exist, get ready to create it
      printf("%s does not yet exist on the file system -- searching for empty directory slot to create it in.\n", dest_name);

      if (!create_direntry_with_attrib(dest_name, DE_ATTRIB_DIR)) {
        printf("ERROR: Failed to create dir entry for file\n");
        return -1;
      }
    }
    if (dir_sector == -1) {
      printf("ERROR: Drive is full.\n");
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
      write_cluster_number_into_direntry(a_cluster);

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
        if (next_cluster == 0 || next_cluster >= FAT32_MIN_END_OF_CLUSTER_MARKER) {
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
        if (next_cluster <= 0) {
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
        struct tm *tm = localtime(&t);

        for (int i = 0; i < 11; i++)
          buffer[0x00 + i] = ' ';
        buffer[0x00 + 0] = '.';
        buffer[0x00 + 0xb] = 0x10; // directory

        assemble_time_into_raw(&buffer[0x00], tm);

        buffer[0x00 + 0x1A] = (first_cluster_of_file >> 0) & 0xff;
        buffer[0x00 + 0x1B] = (first_cluster_of_file >> 8) & 0xff;
        buffer[0x00 + 0x14] = (first_cluster_of_file >> 16) & 0xff;
        buffer[0x00 + 0x15] = (first_cluster_of_file >> 24) & 0xff;

        for (int i = 0; i < 11; i++)
          buffer[0x20 + i] = ' ';
        buffer[0x20 + 0] = '.';
        buffer[0x20 + 1] = '.';
        buffer[0x20 + 0xb] = 0x10; // directory

        assemble_time_into_raw(&buffer[0x20], tm);

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

int download_slot(int slot_number, char *dest_name)
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

    FILE *f = fopen(dest_name, "wb");
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

int download_flashslot(int slot_number, char *dest_name)
{
  int retVal = 0;
  do {

    if (slot_number < 0 || slot_number >= 8) {
      printf("ERROR: Invalid flash slot number (valid range is 0 -- 7)\n");
      retVal = -1;
      break;
    }

    FILE *f = fopen(dest_name, "wb");
    if (!f) {
      printf("ERROR: Could not open file '%s' for writing\n", dest_name);
      retVal = -1;
      break;
    }
    printf("Saving flash slot %d into '%s'\n", slot_number, dest_name);

    for (int i = 0; i < 8192 * 1024; i += 512 * 64) {
      unsigned char sector[512 * 64];
      if (read_flash(slot_number * 8192 * 1024 + i, sector)) {
        printf("ERROR: Could not read sector %d/%d of freeze slot %d (absolute sector %d)\n", i, syspart_slot_size,
            slot_number, slot_number * 8192 * 1024 + i);
        retVal = -1;
        break;
      }
      fwrite(sector, 512 * 64, 1, f);
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

  if (fat_opendir(current_dir, TRUE)) {
    return FALSE;
  }

  //    printf("Opened directory, dir_sector=%d (absolute sector = %d)\n",dir_sector,partition_start+dir_sector);

  return TRUE;
}

BOOL find_file_in_curdir(char *filename, struct m65dirent *de)
{
  if (!safe_open_dir())
    return FALSE;

  while (!fat_readdir(de, FALSE)) {
    // if (de->d_name[0]) printf("%13s   %d\n",de->d_name,(int)de->d_filelen);
    //      else dump_bytes(0,"empty dirent",&dir_sector_buffer[dir_sector_offset],32);
    if (name_match(de, filename)) {
      // Found file, so will replace it
      //	printf("%s already exists on the file system, beginning at cluster %d\n",name,(int)de->d_ino);
      return TRUE;
    }
  }
  return FALSE;
}

char *find_long_name_in_curdir(char *filename)
{
  static struct m65dirent de;
  if (!safe_open_dir())
    return FALSE;

  while (!fat_readdir(&de, FALSE)) {
    if (name_match(&de, filename)) {
      return de.d_longname;
    }
  }
  return NULL;
}

char *get_current_short_name(void)
{
  static struct m65dirent de;
  dir_sector_offset -= 32;
  fat_readdir(&de, FALSE);
  return de.d_name;
}

unsigned int calc_first_cluster_of_file(void)
{
  return (dir_sector_buffer[dir_sector_offset + 0x1A] << 0) | (dir_sector_buffer[dir_sector_offset + 0x1B] << 8)
       | (dir_sector_buffer[dir_sector_offset + 0x14] << 16) | (dir_sector_buffer[dir_sector_offset + 0x15] << 24);
}

unsigned int calc_size_of_file(void)
{
  return (dir_sector_buffer[dir_sector_offset + 0x1C] << 0) | (dir_sector_buffer[dir_sector_offset + 0x1D] << 8)
       | (dir_sector_buffer[dir_sector_offset + 0x1E] << 16) | (dir_sector_buffer[dir_sector_offset + 0x1F] << 24);
}

BOOL create_directory_entry_for_shortname(char *short_name, int attrib)
{
  // Create directory entry, and write sector back to SD card
  unsigned char dir[32];
  bzero(dir, 32);

  // Write name
  for (int i = 0; i < 11; i++)
    dir[i] = short_name[i];

  // Set file(0x20)/dir(0x10) attributes (only archive bit)
  dir[0xb] = attrib;

  // Store create time and date
  time_t t = time(0);
  struct tm *tm = localtime(&t);
  assemble_time_into_raw(dir, tm);

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

BOOL read_next_direntry_and_assure_is_free(struct m65dirent *de)
{
  if (fat_readdir(de, TRUE) != 0) {
    printf("ERROR: Unable to read next direntry\n");
    return FALSE;
  }

  if (vfatEntry || de->d_name[0] || de->d_type != M65DT_FREESLOT || de->de_raw[0] == 0xE5) {
    return FALSE;
  }

  return TRUE;
}

BOOL find_contiguous_free_direntries(int direntries_needed)
{
  BOOL found_start_free = FALSE;
  int start_dir_cluster;
  int start_dir_sector;
  int start_dir_sector_in_cluster;
  int start_dir_sector_offset;
  int count = 0;

  if (fat_opendir(current_dir, TRUE)) {
    return FALSE;
  }

  while (count < direntries_needed) {
    struct m65dirent de;
    if (read_next_direntry_and_assure_is_free(&de)) {
      count++;
      if (!found_start_free) {
        found_start_free = TRUE;
        start_dir_cluster = dir_cluster;
        start_dir_sector = dir_sector;
        start_dir_sector_in_cluster = dir_sector_in_cluster;
        start_dir_sector_offset = dir_sector_offset;
      }
      if (de.de_raw[0] == 0x00) { // if first byte of direntry is 00, it implies that no genuine direntries exist beyond this
        count = direntries_needed;
        break;
      }
    }
    else { // this dir-entry is not free?
      found_start_free = FALSE;
      count = 0;
    }
  }

  if (count == direntries_needed) {
    dir_cluster = start_dir_cluster;
    dir_sector = start_dir_sector;
    dir_sector_in_cluster = start_dir_sector_in_cluster;
    dir_sector_offset = start_dir_sector_offset
                      - 32; // the -32 is needed due to advance_to_next_entry() call doing a +32 initially

    read_sector(partition_start + dir_sector, dir_sector_buffer, CACHE_YES, 0);

    return TRUE;
  }

  return FALSE;
}

void copy_from_dnamechunk_to_offset(char *dnamechunk, unsigned char *dir, int offset, int numuc2chars)
{
  for (int k = 0; k < numuc2chars; k++) {
    if (dnamechunk == NULL) {
      dir[offset + k * 2] = 0xff;
      dir[offset + k * 2 + 1] = 0xff;
    }
    else if (dnamechunk[k] == 0) { // last char in string?
      dir[offset + k * 2] = (unsigned char)dnamechunk[k];
      dir[offset + k * 2 + 1] = 0;
      dnamechunk = NULL;
    }
    else {
      dir[offset + k * 2] = (unsigned char)dnamechunk[k];
      dir[offset + k * 2 + 1] = 0;
    }
  }
}

void copy_vfat_chars_into_direntry(char *dname, unsigned char *dir, int seqnumber)
{
  // increment char-pointer to the seqnumber string chunk we'll copy across
  dname = dname + 13 * (seqnumber - 1);
  int len = strlen(dname);
  copy_from_dnamechunk_to_offset(dname, dir, 0x01, 5);
  dname += 5;
  len -= 5;
  if (len < 0)
    dname = NULL;
  copy_from_dnamechunk_to_offset(dname, dir, 0x0E, 6);

  if (dname != NULL)
    dname += 6;
  len -= 6;
  if (len < 0)
    dname = NULL;
  copy_from_dnamechunk_to_offset(dname, dir, 0x1C, 2);
}

BOOL write_vfat_direntry_chunk(int vfat_seq_id, char *filename, unsigned char lfn_csum)
{
  unsigned char dir[32];
  bzero(dir, 32);

  // write sequence id
  dir[0] = vfat_seq_id;
  if (vfat_seq_id == calculate_needed_direntries_for_vfat(filename))
    dir[0] |= 0x40;

  copy_vfat_chars_into_direntry(filename, dir, vfat_seq_id);

  dir[0x0b] = 0x0f; // marker attribute for a vfat chunk
  dir[0x0d] = lfn_csum;

  // Copy back into directory sector, and write it
  bcopy(dir, &dir_sector_buffer[dir_sector_offset], 32);
  if (write_sector(partition_start + dir_sector, dir_sector_buffer)) {
    printf("Failed to write updated directory sector.\n");
    return FALSE;
  }

  return TRUE;
}

BOOL create_directory_entry_for_item(
    char *dest_name, char *short_name, unsigned char lfn_csum, BOOL needs_long_name, int attrib)
{
  struct m65dirent de;

  // write out vfat entries
  if (needs_long_name) {
    int vfat_seq_id = calculate_needed_direntries_for_vfat(dest_name);

    for (; vfat_seq_id > 0; vfat_seq_id--) {
      if (!read_next_direntry_and_assure_is_free(&de)) {
        printf("ERROR: direntry at sector=%ld, offset=$%04X is not free (but we thought it should have been)\n",
            de.de_sector, de.de_sector_offset);
        return FALSE;
      }

      if (!write_vfat_direntry_chunk(vfat_seq_id, dest_name, lfn_csum))
        return FALSE;

      execute_write_queue();
    }
  }

  // finally, write the short-name
  if (!read_next_direntry_and_assure_is_free(&de))
    return FALSE;

  create_directory_entry_for_shortname(short_name, attrib);

  execute_write_queue();

  return TRUE;
}

char *get_file_extension(char *filename)
{
  int i = strlen(filename) - 1;
  do {
    char *c = filename + i;
    if (*c == '.')
      return c;
    i--;
  } while (i >= 0);

  return NULL;
}

BOOL is_d81_file(char *filename)
{
  char *ext = get_file_extension(filename);

  if (!strcmp(ext, ".d81") || !strcmp(ext, ".D81"))
    return TRUE;

  return FALSE;
}

int is_fragmented(char *filename)
{
  if (!safe_open_dir())
    exit(-1);

  struct m65dirent de;
  if (!find_file_in_curdir(filename, &de)) {
    printf("?  FILE NOT FOUND ERROR FOR \"%s\"\n", filename);
    exit(-1);
  }

  unsigned int current_cluster = calc_first_cluster_of_file();
  unsigned int next_cluster;
  while ((next_cluster = chained_cluster(current_cluster)) < FAT32_MIN_END_OF_CLUSTER_MARKER) {
    if (next_cluster != current_cluster + 1)
      return 1;
    current_cluster = next_cluster;
  }

  return 0;
}

int queue_mount_file(char *filename)
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
  strcpy((char *)(job + 1), filename);
  len++;
  job[len] = 0; // add null-terminator at end
  len++;
  queue_add_job(job, len);

  return TRUE;
}

void petscify_text(char *text)
{
  for (int k = 0; k < strlen(text); k++) {
    char c = text[k] & 0xdf;
    // switch lower and upper-case
    if (c >= 0x41 && c <= 0x5a)
      text[k] = text[k] ^ 0x20;
  }
}

unsigned char peek(unsigned long addr)
{
  if (ethernet_mode) {
    wait_all_acks();
    const int packet_size = 12;
    uint8_t payload[packet_size];
    memcpy(payload, ethernet_request_string, 4); // 'mreq' magic string
    // bytes [4] and [5] will be filled with packet seq numbers
    payload[6] = 0x11; // read mem command
    payload[7] = 0;    // number of bytes minus one
    payload[8] = addr & 0xff;
    payload[9] = (addr >> 8) & 0xff;
    payload[10] = (addr >> 16) & 0xff;
    payload[11] = (addr >> 24) & 0xff;
    ethl_send_packet(payload, packet_size);
    wait_all_acks();
    if (memory_read_buffer_len != 1) {
      log_error("Error reading memory data via Ethernet");
      exit(-1);
    }
    return memory_read_buffer[0];
  }
  else {
    return mega65_peek(addr);
  }
}

void poke(unsigned long addr, unsigned char value)
{
  if (ethernet_mode) {
    log_error("Serial monitor memory write access requested in Ethernet mode");
    exit(-1);
  }
  char cmd[16];
  sprintf(cmd, "s%lx %x\r", addr, value);
  slow_write(fd, cmd, strlen(cmd));
}

void determine_ethernet_window_size(void)
{
  uint8_t hardware_model_id = peek(0xFFD3629);
  log_info("Hardware model id: $%02X - %s", hardware_model_id, get_model(hardware_model_id)->name);

  switch (hardware_model_id) {
  case 0x01:
  case 0x03:
  case 0x21:
    ethernet_window_size = 28; // xc7a200t_0 models have more Ethernet receive buffers
    break;
  default:
    ethernet_window_size = 3;
  }
  log_info("Ethernet window size: %d packets", ethernet_window_size);
}

void request_remotesd_version(void)
{
  poke(0xc001, 0x13);
  poke(0xc000, 0x01);
}

void request_quit(void)
{
  if (ethernet_mode) {
    wait_all_acks();
    const int packet_size = 7;
    uint8_t payload[packet_size];
    memcpy(payload, ethernet_request_string, 4); // 'mreq' magic string
    // bytes [4] and [5] will be filled with packet seq numbers
    payload[6] = 0xff; // quit command
    ethl_send_packet(payload, packet_size);
    wait_all_acks();
  }
  else {
    poke(0xc001, 0xff);
    poke(0xc000, 0x01);
  }
}

void mount_file(char *filename)
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

int get_cluster_count(char *filename)
{
  struct m65dirent de;

  if (!safe_open_dir())
    return 0;

  if (!find_file_in_curdir(filename, &de)) {
    return 0;
  }
  int file_cluster = de.d_ino;
  int count = 0;
  do {
    count++;
    file_cluster = chained_cluster(file_cluster);
  } while (file_cluster > 0 && file_cluster < FAT32_MIN_END_OF_CLUSTER_MARKER);

  if (file_cluster < 0) {
    return -1;
  }

  return count;
}

int download_single_file(char *dest_name, char *local_name, int showClusters)
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
    unsigned long long last_status_output = 0;
    unsigned int sector_number;
    FILE *f = NULL;

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
        if (next_cluster == 0 || next_cluster >= FAT32_MIN_END_OF_CLUSTER_MARKER) {
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
        // so the extra read-ahead reduces the round-trip time for scheduling each successive job
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
      unsigned long long now = gettime_ms();
      if (!showClusters && !quietFlag && (now - last_status_output > 100)) {
        printf("\rDownloaded %lld bytes.", (long long)de.d_filelen - remaining_bytes);
        last_status_output = now;
        fflush(stdout);
      }

      //      printf("T+%lld : after write.\n",gettime_us()-start_usec);

      sector_in_cluster++;
      remaining_bytes -= 512;
      if (remaining_bytes < 0)
        remaining_bytes = 0;
    }

    if (f)
      fclose(f);

    int next_cluster = chained_cluster(file_cluster);
    if (!quietFlag)
      printf("Next cluster = $%x\n", next_cluster);
    while (next_cluster < 0xffffff0) {
      next_cluster = chained_cluster(next_cluster);
      if (!quietFlag)
        printf("Next cluster = $%x\n", next_cluster);
    }

    if (time(0) == upload_start)
      upload_start = time(0) - 1;
    if (!showClusters && !quietFlag) {
      printf("\rDownloaded %lld bytes in %lld seconds (%.1fKB/sec)\n", (long long)de.d_filelen,
          (long long)time(0) - upload_start, de.d_filelen * 1.0 / 1024 / (time(0) - upload_start));
    }
    else {
      if (!quietFlag)
        printf("\n");
    }

  } while (0);

  return retVal;
}

int download_file(char *name, char *local_name, int showClusters)
{
  llist *lst_dirents = llist_new();
  char *searchterm = NULL; // ignore this for now (borrowed it from elsewhere)

  // don't bother with wildcards if a unique local name has been specified (assume that this is just for a single file)
  if (strcmp(name, local_name))
    return download_single_file(name, local_name, showClusters);

  // if no wildcards in name, then just download a single file
  if (!strstr(name, "*"))
    return download_single_file(name, local_name, showClusters);

  // handle wildcards
  if (!read_remote_dirents(lst_dirents, current_dir, &searchterm)) {
    return FALSE;
  }

  llist *cur = lst_dirents;

  while (cur != NULL) {
    struct m65dirent *itm = (struct m65dirent *)cur->item;

    if (!is_match(itm->d_name, name, 1) && !is_match(itm->d_longname, name, 1)) {
      cur = cur->next;
      continue;
    }

    if (itm->d_attr & 0x10)
      ; // this is a DIR
    else if (itm->d_name[0] && itm->d_filelen >= 0) {
      char *name = itm->d_name;
      if (itm->d_longname[0])
        name = itm->d_longname;
      printf("Downloading \"%s\"...\n", name);
      download_single_file(name, name, showClusters);
    }

    cur = cur->next;
  }

  return 0;
}
