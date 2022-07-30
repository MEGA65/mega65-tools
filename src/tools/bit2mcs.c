#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void error(char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  printf("Error: ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  exit(1);
}

#ifdef INCLUDE_BIT2MCS
int bit2mcs(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
  unsigned int loadAddr = 0;
  FILE *infile;
  FILE *outfile;
  int numBytes, i;
  int c;
  unsigned char lineData[16];
  unsigned int chksum;

  if (argc < 3 || argc > 4) {
    printf("bit2mcs - Converts XILINX bitstream files to flashable files\n"
           "Usage: bit2mcs <input file> <output file> [offset_in_hex]\n"
           "  Bitstream Example:\n"
           "    bit2mcs mega65.bit mega65.mcs\n\n"
           "  Core-to-Slot Example:\n"
           "    bit2mcs mega65r3.cor m65r3slot1.mcs 800000\n\n");
    exit(1);
  }

  if (argc == 4) {
    sscanf(argv[3], "%X", &loadAddr);
  }

  infile = fopen(argv[1], "rb");

  if (infile == NULL) {
    error("cannot open input file %s", argv[1]);
  }
  // if old-style bitstream, then seek 120-bytes into the file before reading
  if (argc == 3) {
    fseek(infile, 120, SEEK_SET);
  }
  outfile = fopen(argv[2], "wt");
  if (outfile == NULL) {
    error("cannot open output file %s", argv[2]);
  }
  while (1) {
    if ((loadAddr & 0xFFFF) == 0) {
      fprintf(outfile, ":02000004");
      fprintf(outfile, "%04X", loadAddr >> 16);
      chksum = 0x02 + 0x04 + ((loadAddr >> 24) & 0xFF) + ((loadAddr >> 16) & 0xFF);
      fprintf(outfile, "%02X\n", (-chksum) & 0xFF);
    }
    chksum = 0;
    for (numBytes = 0; numBytes < 16; numBytes++) {
      c = fgetc(infile);
      if (c == EOF) {
        break;
      }
      lineData[numBytes] = c;
      chksum += c;
    }
    if (numBytes == 0) {
      break;
    }
    fprintf(outfile, ":%02X%04X00", numBytes, loadAddr & 0xFFFF);
    for (i = 0; i < numBytes; i++) {
      fprintf(outfile, "%02X", lineData[i]);
    }
    chksum += numBytes;
    chksum += ((loadAddr >> 8) & 0xFF) + ((loadAddr >> 0) & 0xFF);
    fprintf(outfile, "%02X\n", (-chksum) & 0xFF);
    loadAddr += numBytes;
    if (c == EOF) {
      break;
    }
  }
  fprintf(outfile, ":00000001FF\n");
  fclose(infile);
  fclose(outfile);
  return 0;
}
