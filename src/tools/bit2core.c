#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_MB 8
#define BYTES_IN_MEGABYTE (1024*1024)
#define MAGIC_STR "MEGA65BITSTREAM0"
#define MAGIC_LEN strlen(MAGIC_STR)

const int CORE_HEADER_SIZE = 4096;
const int BITSTREAM_HEADER_SIZE = 120;

unsigned char bitstream[MAX_MB*BYTES_IN_MEGABYTE];

typedef struct {
  char name[8];
  int num;
  int max_core_mb_size;
} rev_info;

void show_help(void)
{
  fprintf(stderr, "MEGA65 bitstream to core file converter v0.0.2.\n"
                  "---------------------------------------\n"
                  "Usage: <r2|r3> <foo.bit> <core name> <core version> <out.cor>\n"
                  "\n"
                  "Note: 1st parameter must be either:\n"
                  "  r2 (bitstream for Mega65 revision2 board, for max core size of 4MB)\n"
                  "  r3 (bitstream for Mega65 revision3 board, for max core size of 8MB)\n");
}


rev_info parse_revision_arg(const char* rev)
{
  rev_info revs[] =
  {
    // name num MB
    { "r2",  2, 4 },
    { "r3",  3, 8 }
  };
  // NOTE: Append future revision details to end of this array

  int revcnt = sizeof(revs) / sizeof(rev_info);

  for (int idx = 0; idx < revcnt; idx++) {
    if (strcmp(rev, revs[idx].name) == 0)
      return revs[idx];
  }

  fprintf(stderr, "ERROR: 1st argument must specify board revision.\n"
                  "       Valid values are: ");
  for (int idx = 0; idx < revcnt; idx++) {
    if (idx != 0)
      fprintf(stderr, ", ");
    fprintf(stderr, "%s", revs[idx].name);
  }
  fprintf(stderr, "\n");

  exit(-1);
}


int read_bitstream_file(rev_info rev, const char* filename, unsigned char* data)
{
  FILE *bf = fopen(filename, "rb");
  if (!bf) {
    fprintf(stderr, "ERROR: Could not read bitstream file '%s'\n", filename);
    exit(-3);
  }

  int bit_size = fread(data, 1, MAX_MB*BYTES_IN_MEGABYTE, bf);
  fclose(bf);

  printf("Bitstream file is %d bytes long.\n", bit_size);

  if (bit_size < 1024 ||
      bit_size > (rev.max_core_mb_size * BYTES_IN_MEGABYTE - CORE_HEADER_SIZE)) {
    fprintf(stderr,"ERROR: Bitstream file must be >1K and no bigger than (%dMB - 4K)\n", rev.max_core_mb_size);
    exit(-2);
  }

  return bit_size;
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
  fwrite(&bitstream[BITSTREAM_HEADER_SIZE], bit_size - BITSTREAM_HEADER_SIZE, 1, of);
  fclose(of);

  printf("Core file written.\n");
}


int main(int argc,char **argv)
{
  rev_info rev = { { 0 } };

  if (argc != 6) {
    show_help();
    exit(-1);
  }

  rev = parse_revision_arg(argv[1]);

  int bit_size = read_bitstream_file(rev, argv[2], bitstream);

  write_core_file(bit_size, argv[3], argv[4], argv[5]);

  return 0;
} 
