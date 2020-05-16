#include <stdio.h>
#include <stdlib.h>

float quantise_gap(float gap)
{
  if (gap>0.7&&gap<=1.25) gap=1.0;
  if (gap>1.25&&gap<=1.75) gap=1.5;
  if (gap>1.75&&gap<2.25) gap=2.0;
  if (gap<1.0||gap>2.0) gap=99;
  return gap;
}

int last_pulse=0;
float last_gap=0;
int last_bit=0;
unsigned char byte=0;
int bits=0;
int byte_count=0;


void emit_bit(int b)
{
  //    printf("  bit %d\n",b);
  last_bit=b;
  byte=(byte<<1)|b;
  bits++;
  if (bits==8) {
    if (byte_count<16)
      byte_count++;
    else {
      printf("\n"); byte_count=0;
    }
    printf(" $%02x",byte);
    byte=0;
    bits=0;
  }
}

float recent_gaps[4];
float sync_gaps[4]={2.0,1.5,2.0,1.5};

void mfm_decode(float gap)
{
  gap=quantise_gap(gap);

  //  printf("%.1f\n",gap);
  
  // Look at recent gaps to see if it is a sync mark
  for(int i=0;i<3;i++)
    recent_gaps[i]=recent_gaps[i+1];
  recent_gaps[3]=gap;

  int i;
  for(i=0;i<4;i++)
    if (recent_gaps[i]!=sync_gaps[i]) break;
  if (i==4) {
    if (byte_count) printf("\n");
    printf("Sync $A1\n");
    bits=0; byte=0;
    byte_count=0;
    return;
  }
  
  
  if (!last_gap)
    {
      if (gap==1.0) { emit_bit(1); emit_bit(1); }
      else if (gap==1.5) { emit_bit(0); emit_bit(1); }
      else if (gap==2.0) { emit_bit(1); emit_bit(0); emit_bit(1); }      
    }
  else {
    if (last_bit==1) {
      if (gap==1.0) emit_bit(1);
      else if (gap==1.5) { emit_bit(0); emit_bit(0); }
      else if (gap==2.0) { emit_bit(0); emit_bit(1); }
    } else {
      // last bit was a 0
      if (gap==1.0) emit_bit(0);
      else if (gap==1.5) { emit_bit(1); }
      else if (gap==2.0) { emit_bit(0); emit_bit(1); }
    }
  }

  last_gap=gap;
  
}

int main(int argc,char **argv)
{
  if (argc!=2) {
    fprintf(stderr,"usage: mfm-decode <MEGA65 FDC read capture>\n");
    exit(-1);
  }

  FILE *f=fopen(argv[1],"r");
  unsigned char buffer[65536];
  int count=fread(buffer,1,65536,f);
  printf("Read %d bytes\n",count);

  int i;

  for(i=1;i<count;i++) {
    if ((!(buffer[i-1]&0x10))&&(buffer[i]&0x10)) {
      if (last_pulse) // ignore pseudo-pulse caused by start of file      
	mfm_decode((i-last_pulse)/25.0/2.7);
      
      last_pulse=i;      
    }
  }

  printf("\n");
  return 0;
  
}
