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
  if (gap < 1.0 || gap > 2.0)
    gap = 99;
  return gap;
}

int last_pulse = 0;
float last_gap = 0;
int last_bit = 0;
unsigned char byte = 0;
int bits = 0;
int byte_count = 0;
int bytes_emitted=0;

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
      printf("\n");
      byte_count = 0;
    }
    printf(" $%02x", byte);
    bytes_emitted++;
    byte = 0;
    bits = 0;
  }
}

float recent_gaps[4];
float sync_gaps[4] = { 2.0, 1.5, 2.0, 1.5 };

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
    if (byte_count)
      printf("\n");
    if (bytes_emitted) printf("(%d bytes since last sync)\n",bytes_emitted);
    printf("Sync $A1\n");
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

int main(int argc, char** argv)
{
  if (argc != 2) {
    fprintf(stderr, "usage: mfm-decode <MEGA65 FDC read capture>\n");
    exit(-1);
  }

  FILE* f = fopen(argv[1], "r");
  unsigned char buffer[65536];
  int count = fread(buffer, 1, 65536, f);
  printf("Read %d bytes\n", count);

  int i;

  // Check if data looks like it is a $D6AC capture
  int a=buffer[0]>>2;
  int b=buffer[1]>>2;
  int c=buffer[2]>>2;
  int d=buffer[3]>>2;
  if (a==63) { b+=64; c+=64; d+=64; }
  if (b==63) { c+=64; d+=64; }
  if (c==63) { d+=64; }
  if (b>=a&&c>=b&&d>=c&&buffer[0]!=buffer[3]&&buffer[0]!=buffer[2]) {
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
		this_count,count,i);
      }
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
  for (i = 1; i < count; i++) {
    if ((!(buffer[i - 1] & 0x10)) && (buffer[i] & 0x10)) {
      if (last_pulse) // ignore pseudo-pulse caused by start of file
        mfm_decode((i - last_pulse) / 25.0 / 2.7);

      last_pulse = i;
    }
  }

  printf("\n");
  return 0;
}
