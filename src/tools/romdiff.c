#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

unsigned char ref[128*1024];
unsigned char new[128*1024];
unsigned char diff[4*128*1024];
int diff_len=0;

int costs[128*1024];
unsigned char tokens[128*1024][128];
int token_lens[128*1024];

int main(int argc,char **argv)
{
  if (argc!=4) {
    fprintf(stderr,"usage: romdiff <reference ROM> <new ROM> <output file>\n");
    exit(-1);
  }

  // Reset dynamic programming grid
  for(int i=0;i<128*1024;i++) {
    costs[i]=999999999;
    token_lens[i]=0;
  }
  
  FILE *f;

  f=fopen(argv[1],"rb");
  if (!f) {
    fprintf(stderr,"ERROR: Could not read reference ROM file '%s'\n",argv[1]);
    perror("fopen");
    exit(-1);
  }
  if (fread(ref,128*1024,1,f)!=1) {
    fprintf(stderr,"ERROR: Could not read 128KB from reference ROM file '%s'\n",argv[1]);
    exit(-1);
  }
  fclose(f);

  f=fopen(argv[2],"rb");
  if (!f) {
    fprintf(stderr,"ERROR: Could not read new ROM file '%s'\n",argv[2]);
    perror("fopen");
    exit(-1);
  }
  if (fread(new,128*1024,1,f)!=1) {
    fprintf(stderr,"ERROR: Could not read 128KB from new ROM file '%s'\n",argv[2]);
    exit(-1);
  }
  fclose(f);

  // From the end of the new file, working backwards, find the various matches that
  // are possible that start here (including this byte).  We do it backwards, so that
  // we can do dynamic programming optimisation to find the smallest diff.
  // The decoder will only need to interpret the various tokens as it goes.
  /*
    Token types:
    $00 $nn = single literal byte
    $01 $nn $nn = two literal bytes
    $02-$7F $xx $xx = Exact match 1 to 63 bytes, followed by 17-bit address
    $80-$FF $xx $xx <bitmap> <replacement bytes> = Approximate match 1 to 64 bytes.  Followed by bitmap of which bytes
              need to be replaced, followed by the byte values to replace
  */    

  for(int i=(128*1024-1);i>=0;i--) {
    // Calculate cost of single byte
    if (i==(128*1024-1)) costs[i]=2;
    else costs[i]=costs[i+1]+2;
    tokens[i][0]=0x00;
    tokens[i][1]=new[i];
    token_lens[i]=2;

  }

  fprintf(stderr,"Total size of diff = %d bytes.\n",costs[0]);
  return 0;
  
}
