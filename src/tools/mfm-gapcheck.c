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
  if (argc<2||argc>3) {
    fprintf(stderr,"usage: mfm-gapcheck <MEGA65 quantised interval read capture from $D6AC> [synthesised raw MFM output]\n");
    exit(-1);
  }

  FILE *f=fopen(argv[1],"r");
  FILE *o=NULL;
  unsigned char buffer[655360];
  int count=fread(buffer,1,655360,f);
  printf("Read %d bytes\n",count);

  int i;

  int last_counter=0;
  int interval=0;

  if (argc>2) o=fopen(argv[2],"w");
  
  for(i=0;i<count;i++) {
    if ((buffer[i]&0xfc)!=last_counter) {
      last_counter=buffer[i]&0xfc;
      interval=buffer[i]&3;
      switch(interval) {
      case 0: mfm_decode(1.0); break;
      case 1: mfm_decode(1.5); break;
      case 2: mfm_decode(2.0); break;
      case 3:
	printf("Bad interval = '11'\n");
	break;
      }
      if (o) {
	// Write out MFM log for mfmsimulate target
	int cycles = 66 + interval*33;
	for(int j=0;j<cycles;j++) {
	  if (j<8) fputc(0xcf,o); else fputc(0xdf,o);
	}
      }
      
    }
  }

  if (o) fclose(o);
  
  printf("\n");
  return 0;
  
}
