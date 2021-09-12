#include <stdio.h>
#include <stdlib.h>

float quantise_gap(float gap)
{
  if (gap > 0.7 && gap <= 1.25)
    gap = 1.0;
  if (gap > 1.25 && gap <= 1.75)
    gap = 1.5;
  if (gap > 1.75 && gap < 2.25)
    gap = 2.0;

  // Give invalid gaps pseudo sensible values
  if (gap <= 0.7 )
    gap = 1.0;
  if (gap >= 2.25)
    gap = 2.0;
  
  return gap;
}

int last_pulse = 0;
float last_gap = 0;
int last_bit = 0;
unsigned char byte = 0;
int bits = 0;
int byte_count = 0;
int bytes_emitted=0;
int sync_count=0;
int field_ofs=0;
unsigned char data_field[1024];

#define MAX_SIGNALS 64
#define MAX_SAMPLES 65536
float traces[MAX_SIGNALS][MAX_SAMPLES];
int sample_counts[MAX_SIGNALS]={0};
float max_time=0;

// CRC16 algorithm from:
// https://github.com/psbhlw/floppy-disk-ripper/blob/master/fdrc/mfm.cpp
// GPL3+, Copyright (C) 2014, psb^hlw, ts-labs.
// crc16 table
unsigned short crc_ccitt[256];
unsigned short crc=0;

// crc16 init table
void crc16_init()
{
    for (int i = 0; i < 256; i++)
    {
      unsigned short w = i << 8;
      for (int a = 0; a < 8; a++)
	w = (w << 1) ^ ((w & 0x8000) ? 0x1021 : 0);
      crc_ccitt[i] = w;
    }
}

// calc crc16 for 1 byte
unsigned short crc16(unsigned short crc, unsigned short b)
{
    crc = (crc << 8) ^ crc_ccitt[((crc >> 8) & 0xff) ^ b];
    return crc;
}


void describe_data(void)
{
  switch(data_field[0]) {
  case 0xfe:
    // Sector header
    fprintf(stdout,"SECTOR HEADER: Track=%d, Side=%d, Sector=%d, Size=%d (%d bytes) ",
	    data_field[1],data_field[2],data_field[3],
	    128<<(data_field[4]));

    unsigned short crc=0xffff;
    for(int i=0;i<3;i++) crc=crc16(crc,0xa1);
    for(int i=0;i<7;i++) {
      crc=crc16(crc,data_field[i]);
    }
    if (crc) printf("CRC FAIL!\n");
    else printf("CRC ok\n");
    break;
  case 0xfb:
    // Sector data
    crc=0xffff;
    printf("SECTOR DATA:\n");
    for(int i=0;i<512;i+=16) {
      printf("  %04x :",i);
      for(int j=0;j<16;j++) {
	printf(" %02x",data_field[1+i+j]);
      }
      printf("    ");
      for(int j=0;j<16;j++) {
	unsigned char c=data_field[1+i+j];
	// De-PETSCII the data
	if (c>=0xc0&&c<0xdb) c-=0x60;
	if ((c>='a'&&c<='z')||(c>='A'&&c<='Z'))
	  c^=0x20;
	if (c>=' '&&c<0x7f) printf("%c",c); else printf(".");
      }
      printf("\n");
    }
    for(int i=0;i<3;i++) crc=crc16(crc,0xa1);
    unsigned short crc_calc;
    for(int i=0;i<1+512+2;i++) {
      if (i==1+512) crc_calc=crc;
      crc=crc16(crc,data_field[i]);
    }
    if (crc) {
      unsigned int fdc_crc=(data_field[1+512]<<8)+data_field[1+513];
      printf("CRC FAIL!  (included field = $%04x, calculated as $%04x)\n",
	     fdc_crc,crc_calc);
      for(int s=0;s<4;s++) {
	crc=0xffff;
	for(int i=0;i<s;i++) crc=crc16(crc,0xa1);
	for(int i=1;i<1+512+2;i++) {
	  if (i==1+512) crc_calc=crc;
	  crc=crc16(crc,data_field[i]);
	  if (crc==fdc_crc) printf("CRC matched at i=%d, with %d sync marks\n",i,s);
	}
      }
      
    }
    else printf("CRC ok\n");
    break;
  default:
    fprintf(stdout,"WARNING: Unknown data field type $%02x\n",data_field[0]);
    break;
  }
}

void emit_bit(int b)
{
  //    printf("  bit %d\n",b);
  last_bit = b;
  byte = (byte << 1) | b;
  bits++;
  if (bits == 8) {
    if (byte_count < 16)
      byte_count++;
    else {
      //      printf("\n");
      byte_count = 0;
    }
    if (sync_count==3) {
      printf("Data field type $%02x\n",byte);
      sync_count=0;
      field_ofs=1;
      data_field[0]=byte;
    } else  {
      printf(" $%02x", byte);
      if (field_ofs<1024) data_field[field_ofs++]=byte;
    }
    bytes_emitted++;
    byte = 0;
    bits = 0;
  }
}

float recent_gaps[4];
float sync_gaps[4] = { 2.0, 1.5, 2.0, 1.5 };

int found_sync3=0;

void mfm_decode(float gap)
{
  gap = quantise_gap(gap);

  //  printf("%.1f\n",gap);

  // Look at recent gaps to see if it is a sync mark
  for (int i = 0; i < 3; i++)
    recent_gaps[i] = recent_gaps[i + 1];
  recent_gaps[3] = gap;

  int i;
  for (i = 0; i < 4; i++)
    if (recent_gaps[i] != sync_gaps[i])
      break;
  if (i == 4) {
    //    if (byte_count)
    //      printf("\n");
    if (bytes_emitted) {
      describe_data();
      printf("(%d bytes since last sync)\n",bytes_emitted);
      sync_count=0;
    }
    sync_count++;
    if (sync_count==3) { printf("SYNC MARK (3x $A1)\n"); found_sync3++; }
    printf("Sync $A1 x #%d\n",sync_count);
    bits = 0;
    byte = 0;
    byte_count = 0;
    bytes_emitted = 0;
    return;
  } 

  if (!last_gap) {
    if (gap == 1.0) {
      emit_bit(1);
      emit_bit(1);
    }
    else if (gap == 1.5) {
      emit_bit(0);
      emit_bit(1);
    }
    else if (gap >= 2.0) {
      emit_bit(1);
      emit_bit(0);
      emit_bit(1);
    }
  }
  else {
    if (last_bit == 1) {
      if (gap == 1.0)
        emit_bit(1);
      else if (gap == 1.5) {
        emit_bit(0);
        emit_bit(0);
      }
      else if (gap >= 2.0) {
        emit_bit(0);
        emit_bit(1);
      }
    }
    else {
      // last bit was a 0
      if (gap == 1.0)
        emit_bit(0);
      else if (gap == 1.5) {
        emit_bit(1);
      }
      else if (gap == 2.0) {
        emit_bit(0);
        emit_bit(1);
      }
    }
  }

  last_gap = gap;
}

struct precomp_rule {
  float before,me,after;
  float shift;
};

float absf(float f)
{
  if (f<0) return -f;
  return f;
}

#define RULE_COUNT 13
struct precomp_rule rules[RULE_COUNT]
={
  { 1.0,1.0,1.5,+0.10 }, //
  { 1.0,1.0,2.0,+0.15 }, //

  { 1.0,1.5,1.0,-0.20 }, //
  { 1.0,1.5,1.5,-0.15 },
  { 1.0,1.5,2.0,-0.05 },

  { 1.0,2.0,1.0,-0.25 }, //
  { 1.0,2.0,1.5,-0.20 }, 
  
  { 1.5,1.0,1.0,+0.10 },
  { 1.5,1.0,2.0,+0.20 }, //

  { 1.5,1.5,1.0,-0.05 }, //
  { 1.5,2.0,1.0,-0.15 }, //
  
  { 2.0,1.0,1.00,+0.15},
  { 2.0,1.0,1.50,+0.20}  //
};


int main(int argc, char** argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: mfm-decode <MEGA65 FDC read capture ...>\n");
    exit(-1);
  }

  for(int i=0;i<MAX_SIGNALS;i++) {
    sample_counts[i]=0;
  }    
  
  for(int arg=1;arg<argc;arg++) {
    FILE* f = fopen(argv[arg], "r");
    unsigned char buffer[65536];
    int count = fread(buffer, 1, 65536, f);
    printf("Read %d bytes\n", count);
    
    crc16_init();
    
    int i;
    
    // Check if data looks like it is a $D6AC capture
    int a=buffer[0]>>2;
    int b=buffer[1]>>2;
    int c=buffer[2]>>2;
    int d=buffer[3]>>2;
    if (a==63) { b+=64; c+=64; d+=64; }
    if (b==63) { c+=64; d+=64; }
    if (c==63) { d+=64; }
    if ((argc<3)&&b>=a&&c>=b&&d>=c&&buffer[0]!=buffer[3]&&buffer[0]!=buffer[2]) {
      fprintf(stderr,"NOTE: File appears to be $D6AC capture\n");
      
      int last_counter=(a-1)&0x1f;
      
      for(i=0;i<count;i++) {
	int counter_val=(buffer[i]>>2)&0x1f;
	if (counter_val!=(last_counter+1)) {
	  fprintf(stderr,"WARNING: Byte %d : counter=%d, expected %d\n",
		  i,counter_val,last_counter+1);
	}
	last_counter=counter_val;
	if (last_counter==31) last_counter=-1;
	
	switch(buffer[i]&3) {
	case 0: mfm_decode(1.0); break;
	case 1: mfm_decode(1.5); break;
	case 2: mfm_decode(2.0); break;
	case 3: mfm_decode(1.0); break; // invalidly short or long gap, just lie and call it a short gap
	}
      }
      exit(-1);
    }
    
    if (
	((((buffer[1]>>4)+1)&0xf)==(buffer[3]>>4))
	&&
	((((buffer[3]>>4)+1)&0xf)==(buffer[5]>>4))
	&&
	((((buffer[5]>>4)+1)&0xf)==(buffer[7]>>4))
	&&
	((((buffer[7]>>4)+1)&0xf)==(buffer[9]>>4))) {
      fprintf(stderr,"NOTE: Auto-detect $D699/$D69A log.\n");
      
      int last_count=buffer[1]>>4;
      
      for (i = 0; i < count; i+=2) {
	int this_count=buffer[i+1]>>4;
	if (this_count!=last_count) {
	  fprintf(stderr,"ERROR: Saw count %d instead of %d at offset %d\n",
		  this_count,last_count,i);
	  last_count=this_count+1;	
	} else 
	  last_count++;
	if (last_count>0xf) last_count=0;
	
	int gap_len=buffer[i]+((buffer[i+1]&0xf)<<8);
	float qgap=gap_len*1.0/162.0;
	// printf("  gap=%.1f (%d)\n",qgap,gap_len);
	mfm_decode(qgap);
      }
      
      exit(-1);
    }
    
    
    fprintf(stderr,"NOTE: Assuming raw $D6A0 capture.\n");
    
    /* Work out divisor.
       Rate is provided as rate/40.5MHz
       File sample rate is 40.5Mz/3 = 13.5MHz
       
    */
    float rate=29;
    // Obtain data rate from filename if present
    sscanf(argv[arg],"rate%f",&rate);
    float divisor=2*rate/3;
    fprintf(stderr,"Rate = %f\n",rate);

    last_pulse=0;
    found_sync3=0;
    
    for (i = 1; i < count; i++) {
      if ((!(buffer[i - 1] & 0x10)) && (buffer[i] & 0x10)) {
	if (last_pulse) // ignore pseudo-pulse caused by start of file
	  {
	    float gap=i-last_pulse;
	    gap/=divisor;
	    printf("%.2f\n",gap);

	    if (found_sync3==1) {
	      printf("Harmonising at Sync3 after %d samples\n",
		     sample_counts[arg]);
	      sample_counts[arg]=0;
	      found_sync3++;
	    }
	    
	    // Log the trace
	    if (sample_counts[arg]) {
	      // Calculate cumulative time
	      traces[arg][sample_counts[arg]++]=gap+traces[arg][sample_counts[arg]-1];
	    } else {
	      traces[arg][sample_counts[arg]++]=gap;
	    }
	    if (traces[arg][sample_counts[arg]-1]>max_time)
	      max_time=traces[arg][sample_counts[arg]-1];	      
	    
	    mfm_decode(gap);
	  } 
	last_pulse = i;
      }
    }
    
    printf("\n");
  }

  FILE *f=fopen("gaps.vcd","w");
  if (!f) return -1;
  
  fprintf(f,"$date\n"
	  "   Mon Feb 17 15:29:53 2020\n"
	  "\n"
	  "$end\n"
	  "$version\n"
	  "   MEGA65 Floppy Decode Trace Tool.\n"
	  "$end\n"
	  "$comment\n"
	  "   No comment.\n"
	  "$end\n"
	  "$timescale 1us $end\n"
	  "$scope module logic $end\n");
    
  for(int arg=1;arg<argc+2;arg++) {
    if (arg<argc)
      fprintf(f,"$var wire 1 %c %s $end\n",
	      '@'+arg,argv[arg]);
    else if (arg==argc) 
      fprintf(f,"$var wire 1 %c model $end\n",
	      '@'+arg,argv[arg]);
    else
      fprintf(f,"$var wire 1 %c modelerr $end\n",
	      '@'+arg,argv[arg]);
  }
  
  fprintf(f,"$upscope $end\n"
	  "$enddefinitions $end\n"
	  "$dumpvars\n");
  for(int arg=1;arg<argc+2;arg++) fprintf(f,"0%c\n",'@'+arg);
  fprintf(f,"$end\n"
	  "\n");

  printf("Determining average time base for fastest rate...\n");
  float sum=0;
  int count=0;
  for(int i=1;i<sample_counts[1];i++)
    {
      float diff=traces[1][i]-traces[1][i-1];
      if (diff>=0.91&&diff<=1.09) { sum+=diff; count++; }      
    }
  printf("%d samples for slowest rate.\n",sample_counts[argc-1]);
  if (count) printf("Renormalising %d samples : 1.0 step on average = %.2f to 1.0\n",sample_counts[1],sum/count);
  // Renormalise trace 1 to be exactly 1.0 per MFM bit
  for(int i=1;i<sample_counts[1];i++) traces[1][i]/=sum/count;      

  
  // Generate modeled distorted high-data-rate signal
  // Rule #1: short then long makes the short late, and the long early (draws them together)
  // Rule #2: long then short makes the long early and short late (pushes them apart)
  // Rule #3: Double early and double late make no difference
  // Rule #4: Presumably early + late = ontime ("The Deutsche Bahn Rule")
  printf("Surrounding gaps:\n");  
  for(int i=0;i<sample_counts[argc-1];i++)
    {
      float gap_before=1.0;
      float gap_me=1.0;
      float gap_after=1.0;
      if (i>1) {
	gap_before=traces[argc-1][i-1]-traces[argc-1][i-2];
      }
      if (i) {
	gap_me=traces[argc-1][i]-traces[argc-1][i-1];
      }
      if (i<(sample_counts[argc-1]-1)) {
	gap_after=traces[argc-1][i+1]-traces[argc-1][i];
      }
      gap_before=quantise_gap(gap_before);
      gap_me=quantise_gap(gap_me);
      gap_after=quantise_gap(gap_after);

      float gap_munged=gap_me;

      for(int j=0;j<RULE_COUNT;j++) {
	if (gap_before==rules[j].before
	    &&gap_me==rules[j].me
	    &&gap_after==rules[j].after)
	  gap_munged+=rules[j].shift;
	    
      }

      traces[argc][i]=gap_munged;
      float ref_gap=1.0;
      if (i) {
        ref_gap=traces[1][i]-traces[1][i-1];
	//	traces[argc+1][i]=quantise_gap(ref_gap)-gap_munged;
	traces[argc+1][i]=ref_gap-gap_munged;
	traces[argc][i]+=traces[argc][i-1];
      }            
      printf("#%-5d : %.2f : %.2f : %.2f : %.2f : M=%.2f vs R=%.2f (%.2f)",
	     i,traces[argc-1][i], gap_before,gap_me,gap_after,
	     gap_munged,
	     ref_gap, traces[argc-1][i]);
      if (absf(traces[argc+1][i])>=0.06) printf("  E=%.2f",traces[argc+1][i]);
      printf("\n");
    }
  
  float time=0;
  int ofs[MAX_SIGNALS]={0};
  int asserted[MAX_SIGNALS]={0};
  for(int arg=1;arg<argc+2;arg++) {
    for(int i=0;i<sample_counts[arg];i++)
      printf("#%d : %.2f : %.2f\n",arg,
	     traces[arg][i],
	     i?traces[arg][i]-traces[arg][i-1]:traces[arg][i]);
  }
  
  for(time=0;time<(max_time+0.01);time+=0.01) {
    for(int arg=1;arg<argc+2;arg++) {
      if (ofs[arg]<sample_counts[arg]) {
	if (asserted[arg]) {
	  fprintf(f,"#%d\n0%c\n",(int)(time*100),'@'+arg);
	  asserted[arg]=0;
	  ofs[arg]++;
	} else if (traces[arg][ofs[arg]]<=time) {
	  fprintf(f,"#%d\n1%c\n",(int)(time*100),'@'+arg);
	  asserted[arg]=1;	  
	}
      }
    }
  }
  fclose(f);

  f=fopen("gaps.csv","w");
  if (!f) return -1;
  for(int i=0;i<sample_counts[1];i++) {
    fprintf(f,"%5d, ",i);
    for(int arg=1;arg<argc+2;arg++) {
      fprintf(f,"%.2f, ",traces[arg][i]-(i?traces[arg][i-1]:0));
    }
    fprintf(f,"\n");
  }
  fclose(f);
  
  return 0;
}
