#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int show_gaps = 1;
int show_bits = 1;
int show_quantised_gaps = 1;
int show_post_correction = 0;

// MEGA65 floppies contain a track info block that is always written at DD data rate.
// When the TIB is read, the FDC switches to the indicated rate and encoding
float rate = 81 + 1; // DD 720KB
// float rate=40+1; // HD 1.44MB

int rll_encoding = 0;

float quantise_gap_mfm(float gap)
{
  if (gap > 0.7 && gap <= 1.25)
    gap = 1.0;
  if (gap > 1.25 && gap <= 1.75)
    gap = 1.5;
  if (gap > 1.75 && gap < 2.25)
    gap = 2.0;

  // Give invalid gaps pseudo sensible values
  if (gap <= 0.7)
    gap = 1.0;
  if (gap >= 2.25)
    gap = 2.0;

  return gap;
}

float quantise_gap_rll27(float gap)
{
  //  printf("gap=%f\n",gap);
  //  if (gap <= 2.5)
  //    gap = 2.0;
  //  else
  if (gap < 3.5)
    gap = 3;
  else if (gap < 4.5)
    gap = 4;
  else if (gap < 5.5)
    gap = 5;
  else if (gap < 6.5)
    gap = 6;
  else if (gap < 7.5)
    gap = 7;
  else
    gap = 8;

  // RLL2,7 uses convention of counting the gap length, excluding the pulse
  // which is assumed to take up one pulse
  gap -= 1;

  return gap;
}

float quantise_gap(float gap)
{
  if (rll_encoding)
    return quantise_gap_rll27(gap);
  else
    return quantise_gap_mfm(gap);
}

int q_gap(float gap)
{
  //  printf("qgap=%f\n",gap);
  // int a = gap;
  int b = gap + 0.1;
  return b;
}

int last_pulse = 0;
float last_gap = 0;
int last_bit = 0;
unsigned char byte = 0;
int bits = 0;
int byte_count = 0;
int bytes_emitted = 0;
int sync_count = 0;
int field_ofs = 0;
unsigned char data_field[1024];

#define MAX_GAPS 65536 * 4
float corrected_deltas[MAX_GAPS];
float uncorrected_deltas[MAX_GAPS];
int cdelta_count = 0;
int ucdelta_count = 0;
int start_data = 0;

#define MAX_SIGNALS 64
#define MAX_SAMPLES 65536 * 4
float traces[MAX_SIGNALS][MAX_SAMPLES];
int sample_counts[MAX_SIGNALS] = { 0 };
float max_time = 0;

// CRC16 algorithm from:
// https://github.com/psbhlw/floppy-disk-ripper/blob/master/fdrc/mfm.cpp
// GPL3+, Copyright (C) 2014, psb^hlw, ts-labs.
// crc16 table
unsigned short crc_ccitt[256];
unsigned short crc = 0;

// crc16 init table
void crc16_init()
{
  for (int i = 0; i < 256; i++) {
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
  unsigned short crc_calc;
  switch (data_field[0]) {
  case 0x65:
    if (field_ofs > 6) {
      // MEGA65 Track Information Block
      fprintf(stdout, "\nTRACK INFO BLOCK: Track=%d, Divisor=%d (%.2fMHz), Encoding=$%02x\n", data_field[1], data_field[2],
          40.5 / data_field[2], data_field[3]);
      if ((data_field[3] & 0x0f) == 0x01) {
        // RLL
        rate = data_field[2];
        rll_encoding = 1;
      }
      else {
        // MFM
        rate = data_field[2];
        rll_encoding = 0;
      }
      start_data = ucdelta_count;

      unsigned short crc = 0xffff;
      printf("CRC Calc over:");
      for (int i = 0; i < 3; i++) {
        crc = crc16(crc, 0xa1);
        printf(" $%02x", 0xa1);
      }
      for (int i = 0; i < 7; i++) {
        if (i == 5)
          crc_calc = crc;
        crc = crc16(crc, data_field[i]);
        printf(" $%02x", data_field[i]);
      }
      printf("\n");
      if (crc)
        printf("CRC FAIL! Saw $%02x%02x, Calculated $%04x\n", data_field[5], data_field[6], crc_calc);
      else
        printf("CRC ok\n");
    }
    break;
  case 0xfe:
    // Sector header
    fprintf(stdout, "\nSECTOR HEADER: Track=%d, Side=%d, Sector=%d, Size=%d (%d bytes) ", data_field[1], data_field[2],
        data_field[3], data_field[4], 128 << (data_field[4]));

    unsigned short crc = 0xffff;
    for (int i = 0; i < 3; i++)
      crc = crc16(crc, 0xa1);
    for (int i = 0; i < 7; i++) {
      if (i == 5)
        crc_calc = crc;
      crc = crc16(crc, data_field[i]);
    }
    if (crc)
      printf("CRC FAIL! Saw $%02x%02x, Calculated $%04x\n", data_field[5], data_field[6], crc_calc);
    else
      printf("CRC ok\n");
    break;
  case 0xfb:
    // Sector data
    crc = 0xffff;
    printf("\nSECTOR DATA:\n");
    for (int i = 0; i < 512; i += 16) {
      printf("  %04x :", i);
      for (int j = 0; j < 16; j++) {
        printf(" %02x", data_field[1 + i + j]);
      }
      printf("    ");
      for (int j = 0; j < 16; j++) {
        unsigned char c = data_field[1 + i + j];
        // De-PETSCII the data
        if (c >= 0xc0 && c < 0xdb)
          c -= 0x60;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
          c ^= 0x20;
        if (c >= ' ' && c < 0x7f)
          printf("%c", c);
        else
          printf(".");
      }
      printf("\n");
    }
    for (int i = 0; i < 3; i++)
      crc = crc16(crc, 0xa1);
    unsigned short crc_calc;
    for (int i = 0; i < 1 + 512 + 2; i++) {
      if (i == 1 + 512)
        crc_calc = crc;
      crc = crc16(crc, data_field[i]);
    }
    if (crc) {
      unsigned int fdc_crc = (data_field[1 + 512] << 8) + data_field[1 + 513];
      printf("CRC FAIL!  (included field = $%04x, calculated as $%04x)\n", fdc_crc, crc_calc);
      for (int s = 0; s < 4; s++) {
        crc = 0xffff;
        for (int i = 0; i < s; i++)
          crc = crc16(crc, 0xa1);
        for (int i = 1; i < 1 + 512 + 2; i++) {
          if (i == 1 + 512)
            crc_calc = crc;
          crc = crc16(crc, data_field[i]);
          if (crc == fdc_crc)
            printf("CRC matched at i=%d, with %d sync marks\n", i, s);
        }
      }
    }
    else
      printf("CRC ok\n");
    // Clear sector between operations
    bzero(data_field, 512);
    break;
  default:
    fprintf(stdout, "WARNING: Unknown data field type $%02x\n", data_field[0]);
    break;
  }
}

void emit_bit(int b)
{
  if (show_bits)
    printf("  bit %d\n", b);
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
    if (sync_count == 3) {
      printf("Data field type $%02x\n", byte);
      sync_count = 0;
      field_ofs = 1;
      data_field[0] = byte;
    }
    else {
      printf(" $%02x", byte);
      if (field_ofs < 1024)
        data_field[field_ofs++] = byte;
      if (data_field[0] == 0x65 && field_ofs == 7)
        describe_data();
    }
    bytes_emitted++;
    byte = 0;
    bits = 0;
  }
}

int skip_bits = 0;
char buffered_bits[16];
int buffered_bit_count = 0;

void buffer_bit(int bit)
{
  if (0)
    fprintf(stdout, "buffered bits before stuffing next: %d:  %s\n", buffered_bit_count, buffered_bits);

  buffered_bits[buffered_bit_count] = 0;
  if (skip_bits) {
    skip_bits--;
    return;
  }
  if (buffered_bit_count < 16) {
    buffered_bits[buffered_bit_count++] = bit;
    buffered_bits[buffered_bit_count] = 0;
  }
}

// $4E = ...000100 00001000 0100
//                6        4    5

// $FE = 1000 1000 1000 0100
//

// 0001000100001001

//    010  10 0010

void bit_buffer_shuffle(int n)
{
  for (int i = 0; i < (buffered_bit_count - n); i++)
    buffered_bits[i] = buffered_bits[i + n];
  buffered_bit_count -= n;
  buffered_bits[buffered_bit_count] = 0;
}

void rll27_decode_gap(float gap)
{
  /*
    Input    Encoded

    11       1000
    10       0100
    000      100100
    010      000100
    011      001000
    0011     00001000
    0010     00100100
  */

  for (int i = 0; i < gap; i++)
    buffer_bit('0');
  buffer_bit('1');

  buffered_bits[buffered_bit_count] = 0;
  //  printf("buffered bits: [%s]\n",buffered_bits);

  if (buffered_bit_count >= 8) {
    if (!strncmp("00001000", buffered_bits, 8)) {
      emit_bit(0);
      emit_bit(0);
      emit_bit(1);
      emit_bit(1);
      bit_buffer_shuffle(8);
    }
    else if (!strncmp("00100100", buffered_bits, 8)) {
      emit_bit(0);
      emit_bit(0);
      emit_bit(1);
      emit_bit(0);
      bit_buffer_shuffle(8);
    }
  }
  if (buffered_bit_count >= 6) {
    if (!strncmp("100100", buffered_bits, 6)) {
      emit_bit(0);
      emit_bit(0);
      emit_bit(0);
      bit_buffer_shuffle(6);
    }
    else if (!strncmp("000100", buffered_bits, 6)) {
      emit_bit(0);
      emit_bit(1);
      emit_bit(0);
      bit_buffer_shuffle(6);
    }
    else if (!strncmp("001000", buffered_bits, 6)) {
      emit_bit(0);
      emit_bit(1);
      emit_bit(1);
      bit_buffer_shuffle(6);
    }
  }
  if (buffered_bit_count >= 4) {
    if (!strncmp("1000", buffered_bits, 4)) {
      emit_bit(1);
      emit_bit(1);
      bit_buffer_shuffle(4);
    }
    else if (!strncmp("0100", buffered_bits, 4)) {
      emit_bit(1);
      emit_bit(0);
      bit_buffer_shuffle(4);
    }
  }
}

float recent_gaps[4];
float sync_gaps_mfm[4] = { 2.0, 1.5, 2.0, 1.5 };
float sync_gaps_rll27[2] = { 7.0, 2.0 };

int reset_delta = 0;
int found_sync3 = 0;

float previous_partial_gap = 0;

float mfm_decode(float gap)
{
  float gap_in = gap + previous_partial_gap;

  if (gap_in < 0.7) {
    previous_partial_gap = gap;
    printf("Accumulating short gap %.2f\n", gap);
    return 0;
  }

  if (previous_partial_gap) {
    printf("Accumulated gap = %.2f + %.2f = %.2f\n", gap, previous_partial_gap, gap_in);
  }
  gap = quantise_gap(gap_in);
  previous_partial_gap = 0;

  if (show_quantised_gaps)
    printf("%.2f (%.2f)\n", gap, gap_in);

  // Look at recent gaps to see if it is a sync mark
  for (int i = 0; i < 3; i++)
    recent_gaps[i] = recent_gaps[i + 1];
  recent_gaps[3] = gap;

  int i;
  if (!rll_encoding) {
    for (i = 0; i < 4; i++)
      if (recent_gaps[i] != sync_gaps_mfm[i])
        break;
  }
  else {
    for (i = 2; i < 4; i++)
      if (recent_gaps[i] != sync_gaps_rll27[i - 2])
        break;
  }
  if (i == 4) {
    //    if (byte_count)
    //      printf("\n");
    if (bytes_emitted) {
      describe_data();
      printf("(%d bytes since last sync)\n", bytes_emitted);
      sync_count = 0;
    }
    sync_count++;
    if (sync_count == 3) {
      printf("SYNC MARK (3x $A1)\n");
      found_sync3++;
    }
    printf("Sync $A1 x #%d\n", sync_count);
    bits = 0;
    byte = 0;
    byte_count = 0;
    bytes_emitted = 0;
    buffered_bits[0] = '1';
    buffered_bits[1] = 0;
    buffered_bit_count = 1;
    reset_delta = 1;
    if (rll_encoding)
      skip_bits = 3;
    return gap;
  }

  if (rll_encoding) {
    rll27_decode_gap(gap);
  }
  else {
    // MFM
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
  }

  last_gap = gap;
  return gap;
}

struct precomp_rule {
  float before, me, after;
  float shift;
};

float absf(float f)
{
  if (f < 0)
    return -f;
  return f;
}

#define RULE_COUNT 13
// clang-format off
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
// clang-format on

// Frequency of a gap following the two previously indicate gap values
unsigned int gap_freqs[256][256][256];

int main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: mfm-decode <MEGA65 FDC read capture ...>\n");
    exit(-1);
  }

  for (int i = 0; i < MAX_SIGNALS; i++) {
    sample_counts[i] = 0;
  }

  for (int arg = 1; arg < argc; arg++) {
    FILE *f = fopen(argv[arg], "r");
    unsigned char buffer[MAX_SAMPLES];
    int count = fread(buffer, 1, MAX_SAMPLES, f);
    printf("Read %d bytes\n", count);

    crc16_init();

    int i;

    fprintf(stderr, "NOTE: Assuming DMA floppy gap capture.\n");
    fprintf(stderr, "      %d samples.\n", count);

    // Obtain data rate from filename if present
    sscanf(argv[arg], "rate%f", &rate);
    fprintf(stderr, "Rate = %f\n", rate);

    last_pulse = 0;
    found_sync3 = 0;

    float divisor = rate;
    int pulse_adjust = 0;
    int last_pulse_uncorrected = 9;
    int early = 0;
    int late = 0;
    int n = 0;

    FILE *raw = fopen("rawgaps.csv", "w");
    float recent[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    int recent_q[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    float esum = 0;

    int current_pulse = 0;

    for (i = 1; i < count; i++) {
      pulse_adjust = 0;
      float gap = buffer[i];

      current_pulse += buffer[i];

      if (show_gaps)
        printf(" $%03x(%3d) ", (int)(gap * 3.0 / 2), (int)(gap * 3.0 / 2));
      gap /= divisor;

      n++;

      float v[8] = { current_pulse / divisor - recent[0] - recent_q[1] - recent_q[2] - recent_q[3] - recent_q[4]
                         - recent_q[5] - recent_q[6] - recent_q[7],
        current_pulse / divisor - recent[1] - recent_q[2] - recent_q[3] - recent_q[4] - recent_q[5] - recent_q[6]
            - recent_q[7],
        current_pulse / divisor - recent[2] - recent_q[3] - recent_q[4] - recent_q[5] - recent_q[6] - recent_q[7],

        current_pulse / divisor - recent[3] - recent_q[4] - recent_q[5] - recent_q[6] - recent_q[7],
        current_pulse / divisor - recent[4] - recent_q[5] - recent_q[6] - recent_q[7],
        current_pulse / divisor - recent[5] - recent_q[6] - recent_q[7], current_pulse / divisor - recent[6] - recent_q[7],
        current_pulse / divisor - recent[7] };

      // Rule 1: Fall-back is to average the registration against the past five pulses
      float avg = 0;
      for (int i = 0; i < 8; i++)
        avg += v[i];
      avg /= 8;
      float e1, e2;
      e1 = gap - quantise_gap(gap) - 1;

      // Rule 2: If the gap is an integer number of gaps vs any of the past five pulses,
      // then use the one that had the most hits
      int best_count = 0;
      int best_int = 0;
      int counts[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
      for (int i = 0; i < 8; i++) {
        if (absf(v[i]) - ((int)absf(v[i])) < 0.02) {
          int bin = (int)absf(v[i]);
          if (bin >= 0 && bin < 10)
            counts[bin]++;
        }
      }
      for (int i = 3; i <= 8; i++) {
        if (counts[i] > best_count) {
          best_count = counts[i];
          best_int = i;
        }
      }
      if (best_count > 0)
        avg = best_int;

      e2 = avg - quantise_gap(gap) - 1;
      if (n >= 186)
        esum += absf(e2 * 100) * absf(e2 * 100);

      fprintf(raw,
          "%-4d,% -9.2f"
          ",% -5.2f"
          ",% -5.2f"
          ",%5d"
          ",% -7.2f,% -7.2f,% -7.2f,% -7.2f,% -7.2f,% -7.2f,% -7.2f,% -7.2f"
          ",% -7.2f,% -7.2f,% -7.2f"
          "\n",
          n, current_pulse / divisor, (current_pulse - last_pulse) / divisor, avg, current_pulse - last_pulse, v[0], v[1],
          v[2], v[3], v[4], v[5], v[6], v[7], e1, e2, esum);
      for (int r = 0; r < (8 - 1); r++)
        recent[r] = recent[r + 1];
      for (int r = 0; r < (8 - 1); r++)
        recent_q[r] = recent_q[r + 1];
      recent[7] = current_pulse / divisor;
      recent_q[7] = quantise_gap(gap) + 1;

      if (show_gaps)
        printf("%.2f (%d-%d=%d)\n", gap, current_pulse, last_pulse, current_pulse - last_pulse);

      if (found_sync3 == 1) {
        printf("Harmonising at Sync3 after %d samples\n", sample_counts[arg]);
        sample_counts[arg] = 0;
        found_sync3++;
      }

      // Log the trace
      if (sample_counts[arg]) {
        // Calculate cumulative time
        traces[arg][sample_counts[arg]] = gap + traces[arg][sample_counts[arg] - 1];
      }
      else {
        traces[arg][sample_counts[arg]] = gap;
      }
      sample_counts[arg]++;
      if (traces[arg][sample_counts[arg] - 1] > max_time)
        max_time = traces[arg][sample_counts[arg] - 1];

      float quantised = mfm_decode(gap);
      {
        float uncorrected_gap = current_pulse - last_pulse_uncorrected;
        uncorrected_gap /= divisor;
        float uc_delta = quantise_gap(uncorrected_gap) - uncorrected_gap + 1;
        if (show_quantised_gaps)
          printf("     uncorrected gap=%.2f, delta=%.2f\n", uncorrected_gap, uc_delta);
        uncorrected_deltas[ucdelta_count++] = uc_delta;
      }
      if (rll_encoding) {
        float delta = (gap - 1) - quantised;
        if (reset_delta) {
          delta = 0;
          reset_delta = 0;
        }
        if (delta > 0 && delta <= 0.5) {
          // Pulse is a bit late, so adjust last_pulse backwards a bit
          pulse_adjust = (int)(delta * divisor);
          //		printf("rewind last_pulse by %d\n",pulse_adjust);
        }
        if (delta < 0 && delta >= -0.5) {
          // Pulse is a bit late, so adjust last_pulse backwards a bit
          pulse_adjust = (int)(delta * divisor);
          //		printf("advance last_pulse by %d\n",-pulse_adjust);
        }
        if (delta < 0)
          early++;
        if (delta > 0)
          late++;
        if (late > 5) {
          if (show_post_correction)
            printf("     LATE\n");
          late = 0;
          pulse_adjust--;
        }
        if (early > 5) {
          if (show_post_correction)
            printf("     EARLY\n");
          early = 0;
          pulse_adjust++;
        }

        if (show_post_correction)
          printf("     post-correction delta=%.2f\n", delta);
        corrected_deltas[ucdelta_count] = delta;
      }
      last_pulse = current_pulse - pulse_adjust;
      last_pulse_uncorrected = current_pulse;
    }

    fclose(raw);

    printf("\n");

    // Record the gap lengths based on the previous two gaps
    // to help tune our write pre-comp logic
    bzero(gap_freqs, sizeof(unsigned int) * 256 * 256 * 256);
    unsigned int cs[256];
    bzero(cs, sizeof(unsigned int) * 256);
    for (i = 2; i < count; i++) {
      gap_freqs[buffer[i - 2]][buffer[i - 1]][buffer[i]]++;
      cs[buffer[i]]++;
    }
    for (int c = 0; c < 256; c++) {
      if (cs[c])
        printf("%3d : %d\n", c, cs[c]);
    }

    unsigned int buckets[10][10][10];
    bzero(buckets, sizeof(unsigned int) * 10 * 10 * 10);
    unsigned int bc[10];
    bzero(bc, sizeof(bc));

    rll_encoding = 1;
    rate = rate / 2; // because values are shifted right one

    for (int c = 0; c < 256; c++) {
      //	if (!cs[c]) { skip=1; continue; }
      for (int a = 0; a < 256; a++) {
        for (int b = 0; b < 256; b++) {
          if (gap_freqs[a][b][c]) {
            //	      printf("%d,%d,%d\n",
            //		     q_gap(quantise_gap(a/rate)),q_gap(quantise_gap(b/rate)),q_gap(quantise_gap(c/rate)));
            buckets[q_gap(quantise_gap(a / rate))][q_gap(quantise_gap(b / rate))][q_gap(quantise_gap(c / rate))]
                += gap_freqs[a][b][c];
            bc[q_gap(quantise_gap(c / rate))] += gap_freqs[a][b][c];
          }
        }
      }
      //	printf("\n");
    }

    // int skip = 0;
    for (int c = 0; c < 10; c++) {
      if (bc[c]) {
        printf("%d (%6d) : ", c, bc[c]);

        int tally[10][10];

        for (int c0 = 0; c0 < 256; c0++) {
          int cc = q_gap(quantise_gap(c0 / rate));
          bzero(tally, sizeof(tally));
          for (int a = 0; a < 256; a++) {
            for (int b = 0; b < 256; b++) {
              int aa = q_gap(quantise_gap(a / rate));
              int bb = q_gap(quantise_gap(b / rate));
              if (cc == c) {
                if (gap_freqs[a][b][c0]) {
                  tally[aa][bb] += gap_freqs[a][b][c0];
                }
              }
            }
          }

          if (cc == c) {
            printf("\n      c0=%d : ", c0);
            for (int a = 0; a < 10; a++) {
              for (int b = 0; b < 10; b++) {
                if (tally[a][b])
                  printf(" %d@%d,%d", tally[a][b], a, b);
              }
            }
          }
        }

        printf("\n");
      }
    }
  }
  return 0;

  FILE *f = fopen("gaps.vcd", "w");
  if (!f)
    return -1;

  fprintf(f, "$date\n"
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

  for (int arg = 1; arg < argc + 2; arg++) {
    if (arg < argc)
      fprintf(f, "$var wire 1 %c %s $end\n", '@' + arg, argv[arg]);
    else if (arg == argc)
      fprintf(f, "$var wire 1 %c model $end\n", '@' + arg);
    else
      fprintf(f, "$var wire 1 %c modelerr $end\n", '@' + arg);
  }

  fprintf(f, "$upscope $end\n"
             "$enddefinitions $end\n"
             "$dumpvars\n");
  for (int arg = 1; arg < argc + 2; arg++)
    fprintf(f, "0%c\n", '@' + arg);
  fprintf(f, "$end\n"
             "\n");

  printf("Determining average time base for fastest rate...\n");
  float sum = 0;
  int count = 0;
  for (int i = 1; i < sample_counts[1]; i++) {
    float diff = traces[1][i] - traces[1][i - 1];
    if (diff >= 0.91 && diff <= 1.09) {
      sum += diff;
      count++;
    }
  }
  printf("%d samples for slowest rate.\n", sample_counts[argc - 1]);
  if (count)
    printf("Renormalising %d samples : 1.0 step on average = %.2f to 1.0\n", sample_counts[1], sum / count);
  // Renormalise trace 1 to be exactly 1.0 per MFM bit
  for (int i = 1; i < sample_counts[1]; i++)
    traces[1][i] /= sum / count;

  // Generate modeled distorted high-data-rate signal
  // Rule #1: short then long makes the short late, and the long early (draws them together)
  // Rule #2: long then short makes the long early and short late (pushes them apart)
  // Rule #3: Double early and double late make no difference
  // Rule #4: Presumably early + late = ontime ("The Deutsche Bahn Rule")
  printf("Surrounding gaps:\n");
  for (int i = 0; i < sample_counts[argc - 1]; i++) {
    float gap_before = 1.0;
    float gap_me = 1.0;
    float gap_after = 1.0;
    if (i > 1) {
      gap_before = traces[argc - 1][i - 1] - traces[argc - 1][i - 2];
    }
    if (i) {
      gap_me = traces[argc - 1][i] - traces[argc - 1][i - 1];
    }
    if (i < (sample_counts[argc - 1] - 1)) {
      gap_after = traces[argc - 1][i + 1] - traces[argc - 1][i];
    }
    gap_before = quantise_gap(gap_before);
    gap_me = quantise_gap(gap_me);
    gap_after = quantise_gap(gap_after);

    float gap_munged = gap_me;

    for (int j = 0; j < RULE_COUNT; j++) {
      if (gap_before == rules[j].before && gap_me == rules[j].me && gap_after == rules[j].after)
        gap_munged += rules[j].shift;
    }

    traces[argc][i] = gap_munged;
    float ref_gap = 1.0;
    if (i) {
      ref_gap = traces[1][i] - traces[1][i - 1];
      //	traces[argc+1][i]=quantise_gap(ref_gap)-gap_munged;
      traces[argc + 1][i] = ref_gap - gap_munged;
      traces[argc][i] += traces[argc][i - 1];
    }
    printf("#%-5d : %.2f : %.2f : %.2f : %.2f : M=%.2f vs R=%.2f (%.2f)", i, traces[argc - 1][i], gap_before, gap_me,
        gap_after, gap_munged, ref_gap, traces[argc - 1][i]);
    if (absf(traces[argc + 1][i]) >= 0.06)
      printf("  E=%.2f", traces[argc + 1][i]);
    printf("\n");
  }

  float time = 0;
  int ofs[MAX_SIGNALS] = { 0 };
  int asserted[MAX_SIGNALS] = { 0 };
  for (int arg = 1; arg < argc + 2; arg++) {
    for (int i = 0; i < sample_counts[arg]; i++)
      printf("#%d : %.2f : %.2f\n", arg, traces[arg][i], i ? traces[arg][i] - traces[arg][i - 1] : traces[arg][i]);
  }

  for (time = 0; time < (max_time + 0.01); time += 0.01) {
    for (int arg = 1; arg < argc + 2; arg++) {
      if (ofs[arg] < sample_counts[arg]) {
        if (asserted[arg]) {
          fprintf(f, "#%d\n0%c\n", (int)(time * 100), '@' + arg);
          asserted[arg] = 0;
          ofs[arg]++;
        }
        else if (traces[arg][ofs[arg]] <= time) {
          fprintf(f, "#%d\n1%c\n", (int)(time * 100), '@' + arg);
          asserted[arg] = 1;
        }
      }
    }
  }
  fclose(f);

  f = fopen("gaps.csv", "w");
  if (!f)
    return -1;
  for (int i = 0; i < sample_counts[1]; i++) {
    fprintf(f, "%5d, ", i);
    for (int arg = 1; arg < argc + 2; arg++) {
      fprintf(f, "%.2f, ", traces[arg][i] - (i ? traces[arg][i - 1] : 0));
    }
    fprintf(f, "\n");
  }
  fclose(f);

  f = fopen("deltacompare.csv", "w");
  float ucsum = 0, csum = 0, ucmax = 0, cmax = 0;
  for (int i = start_data; i < ucdelta_count; i++) {
    if (absf(uncorrected_deltas[i]) > ucmax)
      ucmax = uncorrected_deltas[i];
    if (absf(corrected_deltas[i]) > cmax)
      cmax = corrected_deltas[i];
    ucsum += absf(uncorrected_deltas[i]);
    csum += absf(corrected_deltas[i]);
    fprintf(f, "%-4d,% -5.2f,% -5.2f,% -5.2f,% -6.2f,% -5.2f,% -6.2f\n", i, uncorrected_deltas[i], corrected_deltas[i], cmax,
        ucmax, csum, ucsum);
  }
  fclose(f);

  return 0;
}
