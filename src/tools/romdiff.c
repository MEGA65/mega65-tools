#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <ctype.h>

#define FILE_SIZE 128 * 1024
// Use just a little part for testing
//#define FILE_SIZE 8*1024

#define EFFORT (FILE_SIZE * 1.0 * FILE_SIZE) / 2.0

unsigned char ref[FILE_SIZE];
unsigned char new[FILE_SIZE];
unsigned char diff[4 * FILE_SIZE];
int diff_len = 0;
unsigned char out[2 * FILE_SIZE];
#define OUT_LITERAL1 0x01
#define OUT_EXACT_MATCH 0x02
#define OUT_APPROX_PATCH 0x03
#define OUT_APPROX_MATCH 0x04

unsigned char out_origin[FILE_SIZE];

int costs[FILE_SIZE];
int next_pos[FILE_SIZE];
unsigned char tokens[FILE_SIZE][128];
int token_lens[FILE_SIZE];

int decode_diff(unsigned char* ref, unsigned char* diff, int diff_len, unsigned char* out)
{
  int out_ofs = 0;
  for (int ofs = 0; ofs < diff_len;) {
    int bad = 0;
    for (int i = 0; i < token_lens[out_ofs]; i++)
      if (diff[ofs + i] != tokens[out_ofs][i])
        bad++;
    if (bad) {
      fprintf(stderr, "ofs=%d : %02x %02x %02x %02x %02x ... vs out_ofs=%d : %02x %02x %02x %02x %02x (n=%d)\n", ofs,
          diff[ofs + 0], diff[ofs + 1], diff[ofs + 2], diff[ofs + 3], diff[ofs + 4], out_ofs, tokens[out_ofs][0],
          tokens[out_ofs][1], tokens[out_ofs][2], tokens[out_ofs][3], tokens[out_ofs][4], token_lens[out_ofs]);
    }

    if (out_ofs >= FILE_SIZE) {
      fprintf(stderr, "End of output produced early after ingesting %d bytes of the %d byte stream.\n", ofs, diff_len);
      return 0;
    }
    if (diff[ofs] == 0x00) {
      // Single byte literal
      out[out_ofs] = ref[out_ofs] ^ diff[ofs + 1];
      out_origin[out_ofs] = OUT_LITERAL1;
      out_ofs++;
      ofs += 2;
    }
    else if (diff[ofs] == 0x01) {
      // Double byte literal
      // XXX Not yet used
      fprintf(stderr, "ERROR: Unsupported token $01 encountered\n");
      exit(-3);
    }
    else if (diff[ofs] < 0x80) {
      // Exact match of 1 -- 63 bytes
      int count = (diff[ofs] - 2) >> 1;
      count++;
      int addr = diff[ofs] & 1;
      addr <<= 16;
      addr |= (diff[ofs + 1]);
      addr |= (diff[ofs + 2]) << 8;
      ofs += 3;
      bcopy(&ref[addr], &out[out_ofs], count);
      //	fprintf(stderr,"Copying %d EXACT match bytes from $%05x to $%05x\n",
      //		count,addr,out_ofs);
      for (int i = 0; i < count; i++)
        out_origin[out_ofs + i] = OUT_EXACT_MATCH;

      out_ofs += count;
    }
    else {
      // Approximate match of 1 -- 64 bytes
      int count = ((diff[ofs] & 0x7f)) >> 1;
      count++;
      int addr = diff[ofs] & 1;
      int bitmap_len = count >> 3;
      if (count & 0x7)
        bitmap_len++;
      addr <<= 16;
      addr |= (diff[ofs + 1]);
      addr |= (diff[ofs + 2]) << 8;
      ofs += 3;
      bcopy(&ref[addr], &out[out_ofs], count);
      //	fprintf(stderr,"Copying %d approx match bytes from $%05x to $%05x\n",
      //		count,addr,out_ofs);
      int bitmap_ofs = ofs;
      ofs += bitmap_len;
      // Patch any mis-matched bytes
      for (int i = 0; i < count; i++) {
        if (diff[bitmap_ofs + (i >> 3)] & (1 << (i & 7))) {
          out[out_ofs + i] ^= diff[ofs];
          out_origin[out_ofs + i] = OUT_APPROX_PATCH;
          ofs++;
        }
        else
          out_origin[out_ofs + i] = OUT_APPROX_MATCH;
      }
      out_ofs += count;
    }
  }

  if (out_ofs != FILE_SIZE) {
    fprintf(stderr, "Ran out of input stream after writing %d bytes\n", out_ofs);
    return -1;
  }
  else
    return 0;
}

char* describe_origin(unsigned char o)
{
  switch (o) {
  case OUT_LITERAL1:
    return "LITERAL BYTE";
  case OUT_EXACT_MATCH:
    return "EXACT MATCH";
  case OUT_APPROX_MATCH:
    return "APPROX MATCHING BYTE";
  case OUT_APPROX_PATCH:
    return "APPROX PATCH BYTE";
  default:
    return "UNKNOWN";
  }
}

char normalised[256];
char* normalise(char* s)
{
  int ofs = 0;
  for (int i = 0; s[i]; i++) {
    if (s[i] == '/' || s[i] == '\\')
      ofs = 0;
    else {
      normalised[ofs++] = toupper(s[i]);
    }
  }
  normalised[ofs] = 0;
  return normalised;
}

int main(int argc, char** argv)
{
  if (argc != 4) {
    fprintf(stderr, "usage: romdiff <reference ROM> <new ROM> <output file>\n");
    exit(-1);
  }

  fprintf(stderr, "Generating patch of %s, using %s as the reference.\n", argv[2], argv[1]);

  // Reset dynamic programming grid
  for (int i = 0; i < FILE_SIZE; i++) {
    costs[i] = 999999999;
    next_pos[i] = 999999999;
    token_lens[i] = 0;
  }

  FILE* f;

  f = fopen(argv[1], "rb");
  if (!f) {
    fprintf(stderr, "ERROR: Could not read reference ROM file '%s'\n", argv[1]);
    perror("fopen");
    exit(-1);
  }
  if (fread(ref, FILE_SIZE, 1, f) != 1) {
    fprintf(stderr, "ERROR: Could not read 128KB from reference ROM file '%s'\n", argv[1]);
    exit(-1);
  }
  fclose(f);

  f = fopen(argv[2], "rb");
  if (!f) {
    fprintf(stderr, "ERROR: Could not read new ROM file '%s'\n", argv[2]);
    perror("fopen");
    exit(-1);
  }
  if (fread(new, FILE_SIZE, 1, f) != 1) {
    fprintf(stderr, "ERROR: Could not read 128KB from new ROM file '%s'\n", argv[2]);
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

  int output_size_so_far = 0;

  long long effort_done = 0;

  for (int i = (FILE_SIZE - 1); i >= 0; i--) {
    // Calculate cost of single byte

    double progress = effort_done / (EFFORT);
    progress *= 100;

    fprintf(stderr, "\r$%05x : %d bytes (%.1f%% of original size) : %.1f%% done.        ", FILE_SIZE - i, output_size_so_far,
        output_size_so_far * 100.0 / (FILE_SIZE - i), progress);

    effort_done += i;

    fflush(stderr);

    // Try encoding the byte as an XOR literal
    if (i == (FILE_SIZE - 1))
      costs[i] = 2;
    else
      costs[i] = costs[i + 1] + 2;
    next_pos[i] = i + 1;
    tokens[i][0] = 0x00;
    tokens[i][1] = new[i] ^ ref[i];
    token_lens[i] = 2;

    int best_len = 0;
    int best_addr = 0;
    for (int j = 0; j < FILE_SIZE; j++) {
      int mlen = 0;
      while (mlen < 62) {
        if (i + mlen >= FILE_SIZE)
          break;
        if (j + mlen >= FILE_SIZE)
          break;
        if (new[i + mlen] != ref[j + mlen])
          break;
        mlen++;
      }
      if (mlen > best_len) {
        best_len = mlen;
        best_addr = j;
        //	if (best_len==64) break;
      }

#if 0
      fprintf(stderr,"new[i]=%02x, ref[j]=%02x, ",
	      new[i],ref[j]);
      fprintf(stderr,"new[i+mlen=%d]=%02x, ref[j+mlen=%d]=%02x, i=%d,j=%d,mlen=%d\n",
	      i+mlen,new[i+mlen],j+mlen,ref[j+mlen],i,j,mlen);
#endif

      // Also model approximate matches
      if (mlen > 0) {
        int diffs = 0;
        int enc_len = 3;
        //	fprintf(stderr,"\n3+");
        enc_len += (mlen >> 3);
        if (mlen & 7)
          enc_len++;
        //	fprintf(stderr,"%d+",enc_len-3);
        for (int k = mlen; k < 64 && (i + k) < FILE_SIZE && (j + k) < FILE_SIZE; k++) {
          if (new[i + k] != ref[j + k]) {
            diffs++;
            enc_len++;
            // fprintf(stderr,"d(%d)",k);
          }
          if ((k & 7) == 0) {
            enc_len++;
            // fprintf(stderr,".<%d>",k);
          }

          if ((enc_len + costs[i + k]) < costs[i]) {
            if (0)
              fprintf(stderr, "$%05x -> $%05x : mlen=%d, approx_len=%d, src_addr=$%05x, diffs=%d, cost=%d, old cost=%d\n", i,
                  i + k, mlen, k, j, diffs, enc_len + costs[i + k], costs[i]);
            // Approximate match helps here
            costs[i] = costs[i + k] + enc_len;
            next_pos[i] = i + k + 1;
            tokens[i][0] = 0x80 + ((k + 1 - 1) << 1) + (j >> 16);
            tokens[i][1] = j >> 0;
            tokens[i][2] = j >> 8;
            token_lens[i] = 3;

            output_size_so_far = costs[i];

            // Setup bitmap for diffs
            int bitmap_len = (k + 1) / 8;
            if ((k + 1) & 7)
              bitmap_len++;
            for (int n = 0; n < bitmap_len; n++)
              tokens[i][3 + n] = 0x00;
            token_lens[i] += bitmap_len;
            // Now write diffs
            int diffs_hit = 0;
            for (int l = 0; l <= k; l++) {
              if (ref[j + l] != new[i + l]) {
                // Set bitmap bit
                tokens[i][3 + (l >> 3)] |= (1 << (l & 7));
                // Copy literals from reference
                // We XOR so that there is no copyright material leaked
                tokens[i][token_lens[i]++] = ref[j + l] ^ new[i + l];
                diffs_hit++;
                //		fprintf(stderr,"(%d, i=%d,j=%d, l=%d, r=%02x, n=%02x)",l,i,j,l,ref[j+l],new[i+l]);
              }
            }
            if (enc_len != token_lens[i]) {
              fprintf(stderr,
                  "ERROR: Modeled cost of %d for %d bytes, but incurred cost of %d bytes. Bitmap len=%d, diffs_hit=%d\n",
                  enc_len, k + 1, token_lens[i], bitmap_len, diffs_hit);
              exit(-1);
            }
          }
        }
      }
    }

    for (int len = 1; len <= best_len; len++) {
      if (costs[i] > (costs[i + len] + 3)) {
        if (0)
          fprintf(stderr, "$%05x -> $%05x : mlen=%d, exact, cost=%d, old cost=%d\n", i, i + len, len,
              (i + len < FILE_SIZE) ? costs[i + len] + 3 : 3, costs[i]);
        if (i + len == FILE_SIZE)
          costs[i] = 3;
        else
          costs[i] = costs[i + len] + 3;
        next_pos[i] = i + len;
        tokens[i][0] = 0x02 + ((len - 1) << 1) + (best_addr >> 16);
        tokens[i][1] = best_addr >> 0;
        tokens[i][2] = best_addr >> 8;
        token_lens[i] = 3;
      }
    }
    output_size_so_far = costs[i];
  }

  fprintf(stderr, "\rTotal size of diff = %d bytes.\n", costs[0]);
  int steps = 0;
  for (int ofs = 0; ofs < FILE_SIZE;) {
    if (next_pos[ofs] > FILE_SIZE) {
      fprintf(stderr, "ERROR: Position $%05x points to illegal position %d (out of bounds)\n", ofs, next_pos[ofs]);
      exit(-1);
    }
    if (next_pos[ofs] <= ofs) {
      fprintf(stderr, "ERROR: Position $%05x points to illegal position %d (backwards)\n", ofs, next_pos[ofs]);
      exit(-1);
    }

    bcopy(tokens[ofs], &diff[diff_len], token_lens[ofs]);
    diff_len += token_lens[ofs];

    ofs = next_pos[ofs];
    steps++;
  }
  fprintf(stderr, "ROM encoded using %d steps. Output stream = %d bytes.\n", steps, diff_len);

  f = fopen(argv[3], "wb");
  if (!f) {
    fprintf(stderr, "ERROR: Could not write to output file '%s'\n", argv[3]);
    exit(-3);
  }
  // Write header and reference file name
  unsigned char header[256];
  bzero(header, 256);
  snprintf((char*)header, 256, "MEGA65ROMPATCH01.00");
  snprintf((char*)&header[32], 256, "%s", normalise(argv[1]));
  snprintf((char*)&header[32 + 64], 256, "%s", normalise(argv[2]));
  fwrite(header, 256, 1, f);
  // Write diff
  fwrite(diff, diff_len, 1, f);
  fclose(f);

  decode_diff(ref, diff, diff_len, out);

  if (bcmp(out, new, FILE_SIZE)) {
    fprintf(stderr, "ERROR: Verify error while testing encoded data stream.\n");
    for (int i = 0; i < FILE_SIZE; i++) {
      if (out[i] != new[i]) {
        fprintf(stderr, "  mismatch at $%05x: saw $%02x, but should be $%02x, origin=%s\n", i, out[i], new[i],
            describe_origin(out_origin[i]));
      }
    }
  }

  return 0;
}
