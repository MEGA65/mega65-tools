#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <strings.h>

int main(int argc,char **argv)
{
  if (argc!=3) {
    fprintf(stderr,"usage: fontmunge <VGA8x16 in> <MEGA65 interleaved 8x16 out>\n");
    exit(-1);
  }

  FILE *in=fopen(argv[1],"rb");
  if (!in) {
    fprintf(stderr,"ERROR: Could not read '%s'\n",argv[1]);
    exit(-1);
  }
  unsigned char buf[4096];
  int n=fread(buf,1,4096,in);
  fclose(in);
  fprintf(stderr,"INFO: Read %d bytes\n",n);

  // Map ASCII to C64 screen code numbers
  unsigned char pbuf[4096];
  bcopy(buf,pbuf,4096);
  bcopy(&buf[65*16],&pbuf[1*16],26*16); // Letters
  // Make reverse chars
  for(int i=0;i<2048;i++) pbuf[2048+i]=pbuf[i]^0xff;
  
  // Do the interleaving step
  unsigned char obuf[4096];
  for(int i=0;i<4096;i++) {
    if (i&1)
      obuf[(i>>1)+2048]=pbuf[i];
    else
      obuf[(i>>1)]=pbuf[i];    
  }

  FILE *of=fopen(argv[2],"wb");
  if (!of) {
    fprintf(stderr,"ERROR: Could not write to '%s'\n",argv[2]);
    exit(-1);
  }  
  fwrite(obuf,1,4096,of);
  fclose(of);
}
