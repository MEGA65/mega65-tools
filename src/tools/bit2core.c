#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_MB 8
#define BYTES_IN_MEGABYTE (1024*1024)
#define MAGIC_STR "MEGA65BITSTREAM0"
#define MAGIC_LEN strlen(MAGIC_STR)
#define MAX_M65_TARGET_NAME_LEN  128

#define ARG_M65TARGETNAME argv[1]
#define ARG_BITSTREAMPATH argv[2]
#define ARG_CORENAME      argv[3]
#define ARG_COREVERSION   argv[4]
#define ARG_COREPATH      argv[5]

static const int CORE_HEADER_SIZE = 4096;
static const int BITSTREAM_HEADER_SIZE = 120;
static const int BITSTREAM_HEADER_FPGA_PART_LOC = 0x4C;

static unsigned char bitstream_data[MAX_MB*BYTES_IN_MEGABYTE];

typedef struct {
  char name[MAX_M65_TARGET_NAME_LEN];
  int max_core_mb_size;
  char fpga_part[13];
} m65target_info;

static m65target_info m65targets[] =
{
  // Mega65 Target Name             MB  FPGA Part
  { "nexys4|nexys4ddr|megaphoner1", 4, "7a100tcsg324" },
  { "mega65r2",                     4, "7a100tfgg484" },
  { "mega65r3",                     8, "7a200tfbg484" }
};
// NOTE: Append future revision details to end of this array

static int m65target_count = sizeof(m65targets) / sizeof(m65target_info);


void split_out_and_print_m65target_names(m65target_info* m65target)
{
  char temp[MAX_M65_TARGET_NAME_LEN]; // strtok() will butcher the string, so make a copy of it
  strcpy(temp, m65target->name);

  char* possible_m65target = strtok(temp, "|");
  do
  {
    fprintf(stderr, "  %-15s %s\n", possible_m65target, m65target->fpga_part);
  }
  while((possible_m65target = strtok(NULL, "|")) != NULL);
}


void show_mega65_target_name_list(void)
{
  fprintf(stderr, "  %-15s %s\n", "m65target", "FPGA part");
  fprintf(stderr, "  %-15s %s\n", "---------", "---------");

  for (int idx = 0; idx < m65target_count; idx++)
  {
    // split out synonym names based on '|' symbol?
    split_out_and_print_m65target_names(&m65targets[idx]);
  }
}

void show_help(void)
{
  fprintf(stderr, "MEGA65 bitstream to core file converter v0.0.3.\n"
                  "---------------------------------------\n"
                  "Usage: <m65target> <foo.bit> <core name> <core version> <out.cor>\n"
                  "\n"
                  "Note: 1st argument specifies your Mega65 target name, which can be either:\n\n");

  show_mega65_target_name_list();
}

int is_match_on_m65targetname_string(const char* m65target, const char* m65targetstring)
{
  char temp[MAX_M65_TARGET_NAME_LEN]; // strtok() will butcher the string, so make a copy of it
  strcpy(temp, m65targetstring);

  char* possible_m65target = strtok(temp, "|");
  do
  {
    if (strcmp(m65target, possible_m65target) == 0)
      return 1;
  }
  while((possible_m65target = strtok(NULL, "|")) != NULL);

  return 0;
}

m65target_info* find_m65target_from_m65targetname(const char* m65targetname)
{
  for (int idx = 0; idx < m65target_count; idx++) {
    if (is_match_on_m65targetname_string(m65targetname, m65targets[idx].name))
    {
      return &m65targets[idx];
    }
  }

  fprintf(stderr, "ERROR: 1st argument must specify valid Mega65 target name.\n"
                  "       Valid values are:\n");

  show_mega65_target_name_list();
  fprintf(stderr, "\n");

  exit(-1);
}

m65target_info* find_m65target_from_fpga_part(char* fpga_part)
{
  for (int idx = 0; idx < m65target_count; idx++)
  {
    if (strcmp(fpga_part, m65targets[idx].fpga_part) == 0)
    {
      printf("This bitstream's FPGA part is suitable for the following mega65 targets: \"%s\" (FPGA part: %s)\n", m65targets[idx].name, m65targets[idx].fpga_part);
      return &m65targets[idx];
    }
  }

  printf("This bitstream's FPGA part is for an unknown mega65 target: \"???\" (FPGA part: %s)\"\n", fpga_part);
  return NULL;
}

void show_warning_if_multiple_m65targets(m65target_info* m65target)
{
  if (strchr(m65target->name, '|') != NULL)
  {
    fprintf(stderr, "WARNING: Can-not distinguish between these multiple mega65 targets based on FPGA part.\n"
                    "         Please make your own external confirmations that you have used the correct bitstream for your mega65 target.\n");
  }
}

int check_bitstream_header_has_suitable_fpga_part_for_m65target(m65target_info* m65target, unsigned char* bitstream)
{
  char* fpga_part = (char*)&bitstream[BITSTREAM_HEADER_FPGA_PART_LOC];

  m65target_info* discovered_m65target = find_m65target_from_fpga_part(fpga_part);

  if (discovered_m65target == NULL)
    return 0;

  if (strcmp(discovered_m65target->fpga_part, m65target->fpga_part) != 0)
    return 0;

  show_warning_if_multiple_m65targets(m65target);
  return 1;
}

int read_bitstream_file(const char* filename)
{
  FILE *bf = fopen(filename, "rb");
  if (!bf) {
    fprintf(stderr, "ERROR: Could not read bitstream file '%s'\n", filename);
    exit(-3);
  }

  int bit_size = fread(bitstream_data, 1, MAX_MB*BYTES_IN_MEGABYTE, bf);
  fclose(bf);

  printf("Bitstream file is %d bytes long.\n", bit_size);

  return bit_size;
}

int check_bitstream_file(m65target_info* m65target, int bit_size)
{
  if (bit_size < 1024)  // NOTE: Why exactly 1024 bytes? Why not just the same as BITSTREAM_HEADER_SIZE (120 bytes?)
  {
    fprintf(stderr, "ERROR: Bitstream file too small (must be >1K)\n");
    return -2;
  }

  if (!check_bitstream_header_has_suitable_fpga_part_for_m65target(m65target, bitstream_data))
  {
    fprintf(stderr, "ERROR: Provided bitstream is for a different mega65 target to the one you specified: \"%s\" (FPGA part: %s)\n",
        m65target->name, m65target->fpga_part);
    return -4;
  }

  if (bit_size > (m65target->max_core_mb_size * BYTES_IN_MEGABYTE - CORE_HEADER_SIZE)) {
    fprintf(stderr, "ERROR: Bitstream file too large (must be no bigger than (%dMB - 4K)\n", m65target->max_core_mb_size);
    return -2;
  }

  return 0;
}


void write_core_file(const int bit_size, const char* core_name, const char* core_version, const char* core_filename)
{
  FILE *of = fopen(core_filename, "wb");
  if (!of) {
    fprintf(stderr, "ERROR: Could not create core file '%s'\n", core_filename);
    exit(-3);
  }

  // Write magic bytes (16 bytes long)
  fprintf(of, MAGIC_STR);

  // Write core file name and version
  char header_block[CORE_HEADER_SIZE - MAGIC_LEN];
  memset(header_block, 0, CORE_HEADER_SIZE - MAGIC_LEN);

  for(int i = 0; (i < 32) && core_name[i]; i++)
    header_block[i] = core_name[i];

  for(int i = 0; (i<32) && core_version[i]; i++)
    header_block[32 + i] = core_version[i];

  fwrite(header_block, CORE_HEADER_SIZE - MAGIC_LEN, 1, of);
  fwrite(bitstream_data, bit_size, 1, of);
  fclose(of);

  fprintf(stderr, "Core file written: \"%s\"\n", core_filename);
}


int main(int argc,char **argv)
{
  int err;

  if (argc != 6)
  {
    show_help();
    exit(-1);
  }

  m65target_info* m65target = find_m65target_from_m65targetname(ARG_M65TARGETNAME);

  int bit_size = read_bitstream_file(ARG_BITSTREAMPATH);

  err = check_bitstream_file(m65target, bit_size);
  if (err != 0)
    return err;

  write_core_file(bit_size, ARG_CORENAME, ARG_COREVERSION, ARG_COREPATH);

  return 0;
} 
