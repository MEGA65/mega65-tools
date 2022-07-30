// Copyright (C) 2022 MEGA65
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include "m65common.h"
#include "dirtymock.h"

extern const char *version_string;

#define MAX_MB 8
#define BYTES_IN_MEGABYTE (1024 * 1024)
#define MAGIC_STR "MEGA65BITSTREAM0"
#define MAGIC_LEN strlen(MAGIC_STR)
#define MAX_M65_TARGET_NAME_LEN 128
#define MAX_M65_PART_NAME_LEN 12

#define ARG_M65TARGETNAME argv[1]
#define ARG_BITSTREAMPATH argv[2]
#define ARG_CORENAME argv[3]
#define ARG_COREVERSION argv[4]
#define ARG_COREPATH argv[5]

#define CORE_HEADER_SIZE 4096

static unsigned char bitstream_data[MAX_MB * BYTES_IN_MEGABYTE];

typedef struct {
  char name[MAX_M65_TARGET_NAME_LEN];
  int max_core_mb_size;
  char fpga_part[MAX_M65_PART_NAME_LEN + 1U];
} m65target_info;

// clang-format off
static m65target_info m65targetgroups[] = {
  // Mega65 Target Name/s                           MB  FPGA Part
  { "nexys4|nexys4ddr|nexys4ddrwidget|megaphoner1", 4, "7a100tcsg324" },
  { "mega65r2",                                     4, "7a100tfgg484" },
  { "mega65r1|mega65r3",                            8, "7a200tfbg484" },
  { "wukonga100t",                                  4, "7a100tfgg676" }
};
// clang-format on
// NOTE: Append future revision details to end of this array

static int m65targetgroup_count = sizeof(m65targetgroups) / sizeof(m65target_info);

typedef struct {
  char name[MAX_M65_TARGET_NAME_LEN];
  int model_id;
} m65target_to_model_id;

// clang-format off
static m65target_to_model_id map_m65target_to_model_id[] = {
  // Mega65 Target Name   // Model ID
  { "mega65r1",           0x01 },
  { "mega65r2",           0x02 },
  { "mega65r3",           0x03 },
  { "megaphoner1",        0x21 },
  { "nexys4",             0x40 }, // aka 'nexys4psram'
  { "nexys4ddr",          0x41 },
  { "nexys4ddrwidget",    0x42 },
  { "wukonga100t",        0xFD }
};
// clang-format on

static int m65target_to_model_id_count = sizeof(map_m65target_to_model_id) / sizeof(m65target_to_model_id);

int get_model_id(const char *m65targetname)
{
  for (int k = 0; k < m65target_to_model_id_count; k++) {
    if (strcmp(map_m65target_to_model_id[k].name, m65targetname) == 0) {
      return map_m65target_to_model_id[k].model_id;
    }
  }
  printf("ERROR: Failed to find model id for m65target of '%s'...", m65targetname);
  exit(1);
}

#pragma pack(push, 1)
typedef union {
  char data[CORE_HEADER_SIZE];
  struct {
    char magic[16];
    char core_name[32];
    char core_version[32];
    char m65_target[32];
    unsigned char model_id;
    unsigned char banner_present;
    unsigned char embed_file_count;
    unsigned long embed_file_offset;
  };
} header_info;
#pragma pack(pop)

void split_out_and_print_m65target_names(m65target_info *m65target)
{
  char temp[MAX_M65_TARGET_NAME_LEN]; // strtok() will butcher the string, so make a copy of it
  strcpy(temp, m65target->name);

  char *possible_m65target = strtok(temp, "|");
  do {
    fprintf(stderr, "  %-15s %s\n", possible_m65target, m65target->fpga_part);
  } while ((possible_m65target = strtok(NULL, "|")) != NULL);
}

void show_mega65_target_name_list(void)
{
  fprintf(stderr, "  %-15s %s\n", "m65target", "FPGA part");
  fprintf(stderr, "  %-15s %s\n", "---------", "---------");

  for (int idx = 0; idx < m65targetgroup_count; idx++) {
    // split out synonym names based on '|' symbol?
    split_out_and_print_m65target_names(&m65targetgroups[idx]);
  }
}

void show_help(void)
{
  fprintf(stderr,
      "MEGA65 bitstream to core file converter\n"
      "---------------------------------------\n"
      "Version: %s\n\n"
      "Usage: <m65target> <foo.bit> <core name> <core version> <out.cor> [<file to embed> ...]\n"
      "\n"
      "Note: 1st argument specifies your Mega65 target name, which can be either:\n\n",
      version_string);

  show_mega65_target_name_list();

  fprintf(stderr, "\n"
                  "The tool can optionally embed files after the core for quick excess or\n"
                  "SD-Card population. You can list multiple files or you can give a\n"
                  "filename prefixed with a @ to specify a special list file. In it you\n"
                  "can list the filenames of files to embed that reside in the *same* path\n"
                  "as the list file. WARNING: there is no duplicate protection!\n"
                  "\n");
}

int is_match_on_m65targetname_string(const char *m65target, const char *m65targetstring)
{
  char temp[MAX_M65_TARGET_NAME_LEN]; // strtok() will butcher the string, so make a copy of it
  strcpy(temp, m65targetstring);

  char *possible_m65target = strtok(temp, "|");
  do {
    if (strcmp(m65target, possible_m65target) == 0)
      return 1;
  } while ((possible_m65target = strtok(NULL, "|")) != NULL);

  return 0;
}

m65target_info *find_m65targetinfo_from_m65targetname(const char *m65targetname)
{
  for (int idx = 0; idx < m65targetgroup_count; idx++) {
    if (is_match_on_m65targetname_string(m65targetname, m65targetgroups[idx].name)) {
      return &m65targetgroups[idx];
    }
  }

  fprintf(stderr, "ERROR: 1st argument must specify valid Mega65 target name.\n"
                  "       Valid values are:\n");

  show_mega65_target_name_list();
  fprintf(stderr, "\n");

  exit(-1);
}

m65target_info *find_m65targetinfo_from_fpga_part(char *fpga_part)
{
  if (strncmp(fpga_part, "xc", 2) == 0)
    fpga_part += 2;
  for (int idx = 0; idx < m65targetgroup_count; idx++) {
    if (strncmp(fpga_part, m65targetgroups[idx].fpga_part, MAX_M65_PART_NAME_LEN) == 0) {
      printf("This bitstream's FPGA part is suitable for the following mega65 targets: \"%s\" (FPGA part: %s)\n",
          m65targetgroups[idx].name, m65targetgroups[idx].fpga_part);
      return &m65targetgroups[idx];
    }
  }

  printf("This bitstream's FPGA part is for an unknown mega65 target: \"???\" (FPGA part: %s)\"\n", fpga_part);
  return NULL;
}

void show_warning_if_multiple_m65targets(m65target_info *m65target)
{
  if (strchr(m65target->name, '|') != NULL) {
    fprintf(stderr, "WARNING: Can-not distinguish between these multiple mega65 targets based on FPGA part.\n"
                    "         Please make your own external confirmations that you have used the correct bitstream for your "
                    "mega65 target.\n");
  }
}

void xilinx_error_exit(int fieldid, char *str, ...)
{
  va_list args;
  va_start(args, str);
  vprintf("ERROR: Xilinx header - Field %d - %s\n", args);

  va_end(args);
  exit(-1);
}

void assert_field_length_equals(int fieldid, short int expected, short int actual)
{
  if (actual != expected)
    xilinx_error_exit(fieldid, "has incorrect length of 0x%02X (expected 0x%02X)", actual, expected);
}

void assert_magic_char_equals(int fieldid, char expected, char actual)
{
  if (actual != expected)
    xilinx_error_exit(fieldid, "has incorrect magic char of '%c' (expected '%c')", actual, expected);
}

// NOTE: set expected to 0x00 if you don't want to test actual value against expected
int get_field_length(unsigned char **ptr, int *fieldid, short int expected)
{
  int fieldlength = ((int)**(ptr) << 8) + (int)*(*(ptr) + 1); // big-endian
  if (expected != 0x00)
    assert_field_length_equals(*fieldid, expected, fieldlength);
  *ptr += 2;

  return fieldlength;
}

void check_magic_header_field(unsigned char **ptr, int *fieldid, unsigned char *expected)
{
  int fieldlength = get_field_length(ptr, fieldid, 0x09);
  unsigned char *actual = *ptr;
  if (memcmp(expected, *ptr, fieldlength) != 0) {
    xilinx_error_exit(*fieldid,
        "Xilinx header - Field %d - has invalid magic header\n"
        "  expected: [%02X] [%02X] [%02X] [%02X] [%02X] [%02X] [%02X] [%02X] [%02X]\n",
        "    actual: [%02X] [%02X] [%02X] [%02X] [%02X] [%02X] [%02X] [%02X] [%02X]", expected[0], expected[1], expected[2],
        expected[3], expected[4], expected[5], expected[6], expected[7], expected[8], expected[9], actual[0], actual[1],
        actual[2], actual[3], actual[4], actual[5], actual[6], actual[7], actual[8], actual[9]);
  }
  printf("Xilinx header - Field %d - verified ok (magic header)\n", *fieldid);

  *ptr += fieldlength;
  *fieldid = *fieldid + 1;
}

char check_magic_char(unsigned char **ptr, int *fieldid, char expected)
{
  get_field_length(ptr, fieldid, 0x01);
  unsigned char actual = **ptr;

  assert_magic_char_equals(*fieldid, expected, actual);

  return actual;
}

char check_magic_char_field(unsigned char **ptr, int *fieldid, char expected)
{
  char actual = check_magic_char(ptr, fieldid, expected);

  printf("Xilinx header - Field %d - verified ok (char='%c')\n", *fieldid, actual);

  *ptr = *ptr + 1;
  *fieldid = *fieldid + 1;

  return actual;
}

char *check_string_field(unsigned char **ptr, int *fieldid)
{
  int fieldlength = get_field_length(ptr, fieldid, 0x00);
  char *actual = (char *)*ptr;
  if ((*ptr)[fieldlength - 1] != 0)
    xilinx_error_exit(*fieldid, "failed to find null terminator in string");
  else
    printf("Xilinx header - Field %d - verified ok (\"%s\")\n", *fieldid, actual);

  *ptr += fieldlength;
  *fieldid = *fieldid + 1;

  return actual;
}

char *locate_fpga_part_id_in_bitstream(unsigned char *bitstream)
{
  // based on description of xilinx file format at:
  // - http://www.pldtool.com/pdf/fmt_xilinxbit.pdf
  unsigned char *ptr = bitstream;
  int fieldid = 1;

  // xilinx magic header
  unsigned char expected_magic_header[] = { 0x0f, 0xf0, 0x0f, 0xf0, 0x0f, 0xf0, 0x0f, 0xf0, 0x00 };
  check_magic_header_field(&ptr, &fieldid, expected_magic_header);

  // letter 'a'
  check_magic_char_field(&ptr, &fieldid, 'a');

  // design name
  check_string_field(&ptr, &fieldid);

  // letter 'b'
  char actual = *ptr;
  assert_magic_char_equals(fieldid, 'b', actual);
  ptr++;

  // fpga part id
  char *fpga_part_id = check_string_field(&ptr, &fieldid);

  return fpga_part_id;
}

int check_bitstream_header_has_suitable_fpga_part_for_m65target(m65target_info *m65target, unsigned char *bitstream)
{
  char *fpga_part = locate_fpga_part_id_in_bitstream(bitstream);

  m65target_info *discovered_m65target = find_m65targetinfo_from_fpga_part(fpga_part);

  if (discovered_m65target == NULL)
    return 0;

  if (strcmp(discovered_m65target->fpga_part, m65target->fpga_part) != 0)
    return 0;

  show_warning_if_multiple_m65targets(m65target);
  return 1;
}

int read_bitstream_file(const char *filename)
{
  FILE *bf = fopen(filename, "rb");
  if (!bf) {
    fprintf(stderr, "ERROR: Could not read bitstream file '%s'\n", filename);
    exit(-3);
  }

  int bit_size = fread(bitstream_data, 1, MAX_MB * BYTES_IN_MEGABYTE, bf);
  fclose(bf);

  printf("Bitstream file is %d bytes long.\n", bit_size);

  return bit_size;
}

int check_bitstream_file(m65target_info *m65target, int bit_size)
{
  if (bit_size < 1024) // NOTE: Why exactly 1024 bytes? Why not just the same as BITSTREAM_HEADER_SIZE (120 bytes?)
  {
    fprintf(stderr, "ERROR: Bitstream file too small (must be >1K)\n");
    return -2;
  }

  if (!check_bitstream_header_has_suitable_fpga_part_for_m65target(m65target, bitstream_data)) {
    fprintf(stderr,
        "ERROR: Provided bitstream is for a different mega65 target to the one you specified: \"%s\" (FPGA part: %s)\n",
        m65target->name, m65target->fpga_part);
    return -4;
  }

  if (bit_size > (m65target->max_core_mb_size * BYTES_IN_MEGABYTE - CORE_HEADER_SIZE)) {
    fprintf(stderr, "ERROR: Bitstream file too large (must be no bigger than (%dMB - 4K)\n", m65target->max_core_mb_size);
    return -2;
  }

  return 0;
}

void write_core_file(int core_len, unsigned char *core_file, char *core_filename)
{
  FILE *of = fopen(core_filename, "wb");
  if (!of) {
    fprintf(stderr, "ERROR: Could not create core file '%s'\n", core_filename);
    exit(-3);
  }
  if (fwrite(core_file, core_len, 1, of) != 1) {
    fprintf(stderr, "ERROR: Could not write all data to '%s'\n", core_filename);
    exit(-1);
  }

  fclose(of);

  fprintf(stderr, "Core file written: \"%s\"\n", core_filename);
  return;
}

void build_core_file(const int bit_size, int *core_len, unsigned char *core_file, const char *core_name,
    const char *core_version, const char *m65target_name, const char *core_filename)
{

  // Write core file name and version
  header_info header_block;

  memset(core_file, 0, 8192 * 1024);

  memset(header_block.data, 0, CORE_HEADER_SIZE);

  for (int i = 0; i < 16; i++)
    header_block.magic[i] = MAGIC_STR[i];

  strcpy(header_block.core_name, core_name);
  strcpy(header_block.core_version, core_version);
  strcpy(header_block.m65_target, m65target_name);
  header_block.model_id = get_model_id(m65target_name);

  bcopy(header_block.data, core_file, CORE_HEADER_SIZE);
  *core_len = CORE_HEADER_SIZE;
  if (bit_size + (*core_len) >= 8192 * 1024) {
    fprintf(stderr, "ERROR: Bitstream + header > 8MB\n");
    exit(-1);
  }
  bcopy(bitstream_data, &core_file[*core_len], bit_size);
  *core_len += bit_size;
}

unsigned long htoc64l(unsigned long v)
{
  unsigned char c[4];

  c[0] = (v >> 0) & 0xff;
  c[1] = (v >> 8) & 0xff;
  c[2] = (v >> 16) & 0xff;
  c[3] = (v >> 24) & 0xff;
  return *(unsigned long *)c;
}

unsigned long c64tohl(unsigned long v)
{
  unsigned char *c = (unsigned char *)&v;
  unsigned long o = 0;
  o = (c[0] << 0) + (c[1] << 8) + (c[2] << 16) + (c[3] << 24);
  return o;
}

unsigned char file_data[1024 * 1024];
unsigned int last_file_offset = 0;
int banner_present = 0;
void embed_file(int *core_len, unsigned char *core_file, char *filename)
{
  header_info *header_block = (header_info *)core_file;
  if (!header_block->embed_file_offset) {
    fprintf(stderr, "INFO: Embedding first file. Setting embed_file_offset to current COR file length.\n");
    header_block->embed_file_offset = htoc64l(*core_len);
    last_file_offset = *core_len;
  }

  char *basename = strrchr(filename, '/');
  if (basename == NULL) {
    // no slash in filename
    basename = filename;
  }
  else
    // skip slash
    basename += 1;
  if (strlen(basename) > 31) {
    fprintf(stderr, "ERROR: Base name of '%s' too long. Must be <32 chars.\n", filename);
    exit(-1);
  }
  else if (strlen(basename) == 0) {
    fprintf(stderr, "ERROR: Cannot embed directory '%s'.\n", filename);
    exit(-1);
  }

  /* Each embedded file is represented by:
      4 bytes = address of next embedded file record (or 0 if last)
      4 bytes = length of this embedded file
     32 bytes = filename
  */

  FILE *f = fopen(filename, "rb");
  if (!f) {
    fprintf(stderr, "ERROR: Could not read file '%s'\n", filename);
    exit(-1);
  }
  int file_len = fread(file_data, 1, 1024 * 1024, f);
  fclose(f);

  fprintf(stderr, "INFO: Writing file '%s' at offset $%06x, len=%d\n", basename, last_file_offset, file_len);

  // XXX - And banner file if present gets put in the last 32KB of the slot so that a future update to HYPPO can
  // read it there instantly on boot.
  if (!strcmp(basename, "BANNER.M65")) {
    if (file_len > 32 * 1024) {
      fprintf(stderr, "ERROR: BANNER.M65 file must be <= 32KB\n");
      exit(-1);
    }
    fprintf(stderr, "INFO: Embedding banner file in last 32KB of slot.\n");
    header_block->banner_present = 1;
    banner_present = 1;
    bcopy(file_data, &core_file[(8192 - 32) * 1024], file_len);
  }

  // Write embedded file
  unsigned int this_offset = last_file_offset;
  unsigned int next_offset = this_offset + 4 + 4 + 32 + file_len;

  if (next_offset >= 8192 * 1024 - 4) {
    fprintf(stderr, "ERROR: COR files must be less than 8MB\n");
    exit(-1);
  }

  core_file[this_offset + 0] = next_offset >> 0;
  core_file[this_offset + 1] = next_offset >> 8;
  core_file[this_offset + 2] = next_offset >> 16;
  core_file[this_offset + 3] = next_offset >> 24;

  core_file[this_offset + 4] = file_len >> 0;
  core_file[this_offset + 5] = file_len >> 8;
  core_file[this_offset + 6] = file_len >> 16;
  core_file[this_offset + 7] = file_len >> 24;

  bcopy(basename, &core_file[this_offset + 4 + 4], 32);

  bcopy(file_data, &core_file[this_offset + 4 + 4 + 32], file_len);

  header_block->embed_file_count++;

  last_file_offset = next_offset;
  *core_len = last_file_offset;

  return;
}

void embed_file_list(int *core_len, unsigned char *core_file, char *filename)
{
  char *filepath = NULL, *seek = NULL, *next = NULL;
  FILE *listfile = NULL;
  char file_data[2048], embedfile[1024];
  int file_len;

  filepath = strdup(filename);
  seek = strrchr(filepath, '/');
  if (seek == NULL) {
    // no path, set to empty string
    seek = filepath;
    *filepath = 0;
  }
  else
    // set last path sep +1 to 0
    *(seek + 1) = 0;

  listfile = fopen(filename, "rb");
  if (!listfile) {
    fprintf(stderr, "ERROR: Could not open list file '%s'\n", filename);
    exit(-1);
  }
  file_len = fread(file_data, 1, 2048, listfile);
  fclose(listfile);

  seek = file_data;
  do {
    next = strchr(seek, '\n');
    if (next != NULL) {
      if (*(next - 1) == '\r') // check if there is a \r to strip (deft has windows)
        *(next - 1) = 0;
      else
        *next = 0;
    }
    if (strlen(seek) > 0) {
      strncpy(embedfile, filepath, 768);
      strncat(embedfile, seek, 256);
      // fprintf(stderr, "DEBUG FILE-LIST: %s\n", embedfile);
      embed_file(core_len, core_file, embedfile);
    }
    seek = next + 1;
  } while (next != NULL && seek < file_data + file_len);

  free(filepath);
}

char *find_fpga_part_from_m65targetname(const char *m65targetname)
{
  m65target_info *m65target = find_m65targetinfo_from_m65targetname(m65targetname);
  return m65target->fpga_part;
}

unsigned char core_file[8192 * 1024];
int core_len = 0;

int DIRTYMOCK(main)(int argc, char **argv)
{
  int err;

  if (argc < 6) {
    show_help();
    exit(-1);
  }

  m65target_info *m65target = find_m65targetinfo_from_m65targetname(ARG_M65TARGETNAME);

  int bit_size = read_bitstream_file(ARG_BITSTREAMPATH);

  err = check_bitstream_file(m65target, bit_size);
  if (err != 0)
    return err;

  build_core_file(bit_size, &core_len, core_file, ARG_CORENAME, ARG_COREVERSION, ARG_M65TARGETNAME, ARG_BITSTREAMPATH);
  for (int i = 6; i < argc; i++) {
    //    fprintf(stderr,"Embedding file '%s'\n",argv[i]);
    if (argv[i][0] == '@')
      embed_file_list(&core_len, core_file, argv[i] + 1);
    else
      embed_file(&core_len, core_file, argv[i]);
  }

  if (banner_present) {
    if (core_len >= (8192 - 32) * 1024 - 4) {
      fprintf(stderr, "ERROR: Insufficient room to place BANNER.M65 at end of slot.\n");
      exit(-1);
    }
    core_len = 8192 * 1024;
  }
  else
    // Leave 4 extra zero bytes at end for end of embedded file chain
    core_len += 4;

  write_core_file(core_len, core_file, ARG_COREPATH);

  return 0;
}
