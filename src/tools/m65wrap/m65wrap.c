/*
 * m65wrap - Wrap a C64-mode PRG so it can be started from C65/MEGA65 mode.
 *
 * The wrapper switches to the C64 memory configuration, relocates the
 * original PRG body back to $0801, reinitialises the C64 KERNAL state, and
 * jumps directly to the address from the original BASIC SYS statement.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "c65toc64wrapper.h"

/*
 * Offsets within src/utilities/c65toc64wrapper.prg.  The byte array and its
 * length are generated at build time from that PRG by the existing bin2c
 * host utility, so c65toc64wrapper.asm remains the source of truth.
 */
#define C65TOC64WRAPPER_JMP_OFFSET 0x00a7u
#define C65TOC64WRAPPER_DMA_COUNT_OFFSET 0x00b0u
#define C65TOC64WRAPPER_LEGACY_DMA_COUNT 0xdfffu
#define C65TOC64WRAPPER_EXPECTED_SIZE 0x00bau
#define C65TOC64WRAPPER_SIZE ((size_t)c65toc64wrapper_len)

#define C64_BASIC_LOAD_ADDR 0x0801u
#define BASIC_TOKEN_REM 0x8fu
#define BASIC_TOKEN_SYS 0x9eu
#define BASIC_TOKEN_MUL 0xacu
#define BASIC_TOKEN_PI 0xffu

/* Exact BASIC V2 PI constant represented by ROM bytes $82 $49 $0f $da $a1. */
#define C64_BASIC_PI 3.1415926525

struct file_buffer {
  uint8_t *data;
  size_t size;
};

static void usage(const char *argv0)
{
  fprintf(stderr,
      "Usage: %s -o OUTPUT.PRG INPUT.PRG\n"
      "       %s -i INPUT.PRG\n"
      "\n"
      "Wrap a $0801 C64-mode PRG for launching from C65/MEGA65 mode.\n"
      "The input BASIC stub must contain a statically resolvable SYS.\n"
      "Supported SYS expressions are decimal literals and N*PI (BASIC pi).\n",
      argv0, argv0);
}

static uint16_t read_le16(const uint8_t *p)
{
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void write_le16(uint8_t *p, uint16_t value)
{
  p[0] = (uint8_t)(value & 0xffu);
  p[1] = (uint8_t)(value >> 8);
}

static int validate_compiled_wrapper(void)
{
  if (C65TOC64WRAPPER_SIZE != C65TOC64WRAPPER_EXPECTED_SIZE) {
    fprintf(stderr, "ERROR: Compiled wrapper has unexpected size %lu (expected %u).\n",
        (unsigned long)C65TOC64WRAPPER_SIZE, (unsigned)C65TOC64WRAPPER_EXPECTED_SIZE);
    return -1;
  }

  if (read_le16(c65toc64wrapper) != 0x2001u) {
    fprintf(stderr, "ERROR: Compiled wrapper no longer loads at $2001.\n");
    return -1;
  }

  if (c65toc64wrapper[C65TOC64WRAPPER_JMP_OFFSET - 1] != 0x4cu
      || read_le16(c65toc64wrapper + C65TOC64WRAPPER_JMP_OFFSET) != 0x080du) {
    fprintf(stderr,
        "ERROR: Compiled wrapper layout changed around the final JMP; update m65wrap patch offsets.\n");
    return -1;
  }

  if (read_le16(c65toc64wrapper + C65TOC64WRAPPER_DMA_COUNT_OFFSET)
      != C65TOC64WRAPPER_LEGACY_DMA_COUNT) {
    fprintf(stderr,
        "ERROR: Compiled wrapper layout changed around the DMA count; update m65wrap patch offsets.\n");
    return -1;
  }

  return 0;
}

static int read_file(const char *path, struct file_buffer *out)
{
  FILE *f;
  long length;
  size_t got;

  memset(out, 0, sizeof(*out));

  f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "ERROR: Cannot open '%s': %s\n", path, strerror(errno));
    return -1;
  }

  if (fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
    fprintf(stderr, "ERROR: Cannot determine size of '%s': %s\n", path, strerror(errno));
    fclose(f);
    return -1;
  }

  if (length < 2) {
    fprintf(stderr, "ERROR: '%s' is too short to be a PRG file.\n", path);
    fclose(f);
    return -1;
  }

  out->size = (size_t)length;
  out->data = (uint8_t *)malloc(out->size);
  if (!out->data) {
    fprintf(stderr, "ERROR: Out of memory reading '%s'.\n", path);
    fclose(f);
    return -1;
  }

  got = fread(out->data, 1, out->size, f);
  if (got != out->size) {
    fprintf(stderr, "ERROR: Short read from '%s'.\n", path);
    free(out->data);
    memset(out, 0, sizeof(*out));
    fclose(f);
    return -1;
  }

  if (fclose(f) != 0) {
    fprintf(stderr, "ERROR: Error closing '%s': %s\n", path, strerror(errno));
    free(out->data);
    memset(out, 0, sizeof(*out));
    return -1;
  }

  return 0;
}

static int write_file(const char *path, const uint8_t *data, size_t size)
{
  FILE *f = fopen(path, "wb");
  size_t written;

  if (!f) {
    fprintf(stderr, "ERROR: Cannot create '%s': %s\n", path, strerror(errno));
    return -1;
  }

  written = fwrite(data, 1, size, f);
  if (written != size) {
    fprintf(stderr, "ERROR: Short write to '%s'.\n", path);
    fclose(f);
    return -1;
  }

  if (fclose(f) != 0) {
    fprintf(stderr, "ERROR: Error closing '%s': %s\n", path, strerror(errno));
    return -1;
  }

  return 0;
}

static int replace_file(const char *temporary, const char *destination)
{
#ifdef _WIN32
  if (!MoveFileExA(temporary, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    fprintf(stderr, "ERROR: Cannot replace '%s' (Windows error %lu). Temporary file remains as '%s'.\n",
        destination, (unsigned long)GetLastError(), temporary);
    return -1;
  }
#else
  if (rename(temporary, destination) != 0) {
    fprintf(stderr, "ERROR: Cannot replace '%s': %s. Temporary file remains as '%s'.\n", destination,
        strerror(errno), temporary);
    return -1;
  }
#endif
  return 0;
}

static int is_statement_end(const uint8_t *data, size_t pos, size_t end)
{
  return pos >= end || data[pos] == ':';
}

static void skip_spaces(const uint8_t *data, size_t *pos, size_t end)
{
  while (*pos < end && data[*pos] == ' ')
    (*pos)++;
}

/*
 * Parse the deliberately small, safe subset of BASIC expressions accepted
 * after SYS:
 *
 *   SYS 49152
 *   SYS 646*PI       (tokenised BASIC normally stores PI as $ff)
 *
 * The result is truncated to an integer as Commodore BASIC does when an
 * integer address is required. Expressions which inspect memory or otherwise
 * depend on runtime state are intentionally rejected.
 */
static int parse_sys_expression(const uint8_t *data, size_t start, size_t end, uint16_t *address)
{
  size_t pos = start;
  uint32_t lhs = 0;
  int digits = 0;
  double value;

  skip_spaces(data, &pos, end);

  while (pos < end && data[pos] >= '0' && data[pos] <= '9') {
    digits = 1;
    lhs = lhs * 10u + (uint32_t)(data[pos] - '0');
    if (lhs > 65535u) {
      fprintf(stderr, "ERROR: SYS expression begins with an out-of-range value.\n");
      return -1;
    }
    pos++;
  }

  if (!digits) {
    fprintf(stderr, "ERROR: SYS expression is not statically supported.\n");
    return -1;
  }

  skip_spaces(data, &pos, end);
  value = (double)lhs;

  if (!is_statement_end(data, pos, end)) {
    if (data[pos] == BASIC_TOKEN_MUL || data[pos] == '*') {
      pos++;
      skip_spaces(data, &pos, end);

      if (pos < end && data[pos] == BASIC_TOKEN_PI) {
        pos++;
      } else if (pos + 1 < end && (data[pos] == 'P' || data[pos] == 'p')
          && (data[pos + 1] == 'I' || data[pos + 1] == 'i')) {
        /* Useful for synthetic/tokeniser variants; normal BASIC V2 uses $ff. */
        pos += 2;
      } else {
        fprintf(stderr, "ERROR: Only a literal N*PI expression is supported after SYS.\n");
        return -1;
      }

      value *= C64_BASIC_PI;
      skip_spaces(data, &pos, end);
    }
  }

  if (!is_statement_end(data, pos, end)) {
    fprintf(stderr, "ERROR: SYS expression is not statically supported.\n");
    return -1;
  }

  if (value < 0.0 || value >= 65536.0) {
    fprintf(stderr, "ERROR: SYS address is outside the 16-bit address range.\n");
    return -1;
  }

  *address = (uint16_t)value; /* BASIC integer conversion truncates the fraction. */
  return 0;
}

static int find_sys_address(const uint8_t *prg, size_t size, uint16_t *sys_address)
{
  const uint16_t load_address = size >= 2 ? read_le16(prg) : 0;
  uint16_t line_address = C64_BASIC_LOAD_ADDR;
  size_t line_offset = 2;
  int found = 0;
  uint16_t found_address = 0;

  if (size < 4) {
    fprintf(stderr, "ERROR: PRG is too short to contain a BASIC program.\n");
    return -1;
  }

  if (load_address != C64_BASIC_LOAD_ADDR) {
    fprintf(stderr, "ERROR: Input PRG loads at $%04x; m65wrap requires $0801.\n", load_address);
    return -1;
  }

  if ((size - 2) > (size_t)(0x10000u - C64_BASIC_LOAD_ADDR)) {
    fprintf(stderr, "ERROR: Input PRG extends beyond the 64KB C64 address space.\n");
    return -1;
  }

  for (;;) {
    uint16_t next_address;
    size_t next_offset;
    size_t pos;
    size_t body_start;
    size_t body_end;
    int quoted = 0;

    if (line_offset + 1 >= size) {
      fprintf(stderr, "ERROR: Truncated BASIC line link at $%04x.\n", line_address);
      return -1;
    }

    next_address = read_le16(prg + line_offset);
    if (next_address == 0)
      break;

    if (line_offset + 4 >= size) {
      fprintf(stderr, "ERROR: Truncated BASIC line at $%04x.\n", line_address);
      return -1;
    }

    if (next_address <= line_address + 4u) {
      fprintf(stderr, "ERROR: Invalid BASIC next-line pointer $%04x at $%04x.\n", next_address,
          line_address);
      return -1;
    }

    next_offset = 2u + (size_t)(next_address - C64_BASIC_LOAD_ADDR);
    if (next_address < C64_BASIC_LOAD_ADDR || next_offset + 1 >= size) {
      fprintf(stderr, "ERROR: BASIC next-line pointer $%04x lies outside the PRG.\n", next_address);
      return -1;
    }

    body_start = line_offset + 4;
    body_end = next_offset - 1;
    if (body_end >= size || prg[body_end] != 0) {
      fprintf(stderr, "ERROR: BASIC line at $%04x is not zero-terminated where its link pointer indicates.\n",
          line_address);
      return -1;
    }

    for (pos = body_start; pos < body_end; pos++) {
      uint8_t c = prg[pos];

      if (c == '"') {
        quoted = !quoted;
        continue;
      }

      if (quoted)
        continue;

      if (c == BASIC_TOKEN_REM)
        break;

      if (c == BASIC_TOKEN_SYS) {
        uint16_t this_address;

        if (parse_sys_expression(prg, pos + 1, body_end, &this_address) != 0)
          return -1;

        if (found && this_address != found_address) {
          fprintf(stderr, "ERROR: BASIC contains multiple SYS destinations ($%04x and $%04x).\n",
              found_address, this_address);
          return -1;
        }

        found = 1;
        found_address = this_address;
      }
    }

    line_address = next_address;
    line_offset = next_offset;
  }

  if (!found) {
    fprintf(stderr, "ERROR: No supported BASIC SYS statement found.\n");
    return -1;
  }

  *sys_address = found_address;
  return 0;
}

static int wrapper_byte_is_patchable(size_t offset)
{
  return offset == C65TOC64WRAPPER_JMP_OFFSET || offset == C65TOC64WRAPPER_JMP_OFFSET + 1
      || offset == C65TOC64WRAPPER_DMA_COUNT_OFFSET
      || offset == C65TOC64WRAPPER_DMA_COUNT_OFFSET + 1;
}

static int looks_wrapped(const uint8_t *data, size_t size)
{
  size_t i;

  if (size < C65TOC64WRAPPER_SIZE + 2)
    return 0;

  for (i = 0; i < C65TOC64WRAPPER_SIZE; i++) {
    if (wrapper_byte_is_patchable(i))
      continue;
    if (data[i] != c65toc64wrapper[i])
      return 0;
  }

  /* The appended original PRG must itself have the $0801 load address. */
  return data[C65TOC64WRAPPER_SIZE] == 0x01 && data[C65TOC64WRAPPER_SIZE + 1] == 0x08;
}

static int validate_existing_wrapper(const uint8_t *data, size_t size)
{
  const uint8_t *inner = data + C65TOC64WRAPPER_SIZE;
  size_t inner_size = size - C65TOC64WRAPPER_SIZE;
  uint16_t sys_address;
  uint16_t wrapper_jump;
  uint16_t wrapper_count;
  uint16_t expected_count;

  if (find_sys_address(inner, inner_size, &sys_address) != 0) {
    fprintf(stderr, "ERROR: File has the m65wrap wrapper signature, but its appended PRG is invalid.\n");
    return -1;
  }

  wrapper_jump = read_le16(data + C65TOC64WRAPPER_JMP_OFFSET);
  wrapper_count = read_le16(data + C65TOC64WRAPPER_DMA_COUNT_OFFSET);
  expected_count = (uint16_t)(inner_size - 2);

  if (wrapper_jump != sys_address) {
    fprintf(stderr,
        "ERROR: Program is already wrapped, but wrapper JMP $%04x does not match inner BASIC SYS $%04x.\n",
        wrapper_jump, sys_address);
    return -1;
  }

  /* Accept both historical fixed-size wrappers and m65wrap's exact-size copy. */
  if (wrapper_count != C65TOC64WRAPPER_LEGACY_DMA_COUNT && wrapper_count != expected_count) {
    fprintf(stderr,
        "ERROR: Program is already wrapped, but wrapper DMA count $%04x is neither legacy $%04x nor expected $%04x.\n",
        wrapper_count, C65TOC64WRAPPER_LEGACY_DMA_COUNT, expected_count);
    return -1;
  }

  return 0;
}

static int make_wrapped(const uint8_t *input, size_t input_size, uint8_t **output, size_t *output_size,
    uint16_t sys_address)
{
  uint8_t *result;
  size_t size;
  uint16_t dma_count;

  if (input_size < 2 || input_size - 2 > 65535u) {
    fprintf(stderr, "ERROR: PRG body is too large for the wrapper DMA job.\n");
    return -1;
  }

  if (C65TOC64WRAPPER_SIZE > SIZE_MAX - input_size) {
    fprintf(stderr, "ERROR: Wrapped file size overflows host size_t.\n");
    return -1;
  }

  size = C65TOC64WRAPPER_SIZE + input_size;
  result = (uint8_t *)malloc(size);
  if (!result) {
    fprintf(stderr, "ERROR: Out of memory creating wrapped PRG.\n");
    return -1;
  }

  memcpy(result, c65toc64wrapper, C65TOC64WRAPPER_SIZE);
  memcpy(result + C65TOC64WRAPPER_SIZE, input, input_size);

  dma_count = (uint16_t)(input_size - 2);
  write_le16(result + C65TOC64WRAPPER_JMP_OFFSET, sys_address);
  write_le16(result + C65TOC64WRAPPER_DMA_COUNT_OFFSET, dma_count);

  *output = result;
  *output_size = size;
  return 0;
}

int main(int argc, char **argv)
{
  const char *input_path = NULL;
  const char *output_path = NULL;
  int in_place = 0;
  int i;
  struct file_buffer input;
  uint16_t sys_address;
  uint8_t *wrapped = NULL;
  size_t wrapped_size = 0;
  int rc = 1;

  if (validate_compiled_wrapper() != 0)
    return 1;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-i") == 0) {
      if (in_place || output_path) {
        fprintf(stderr, "ERROR: Specify exactly one of -i or -o.\n");
        usage(argv[0]);
        return 1;
      }
      in_place = 1;
    } else if (strcmp(argv[i], "-o") == 0) {
      if (in_place || output_path || i + 1 >= argc) {
        fprintf(stderr, "ERROR: -o requires one output filename and cannot be combined with -i.\n");
        usage(argv[0]);
        return 1;
      }
      output_path = argv[++i];
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return 0;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "ERROR: Unknown option '%s'.\n", argv[i]);
      usage(argv[0]);
      return 1;
    } else if (!input_path) {
      input_path = argv[i];
    } else {
      fprintf(stderr, "ERROR: More than one input file specified.\n");
      usage(argv[0]);
      return 1;
    }
  }

  if (!input_path || (!in_place && !output_path)) {
    usage(argv[0]);
    return 1;
  }

  if (output_path && strcmp(input_path, output_path) == 0) {
    fprintf(stderr, "ERROR: Input and output paths are the same; use -i for in-place wrapping.\n");
    return 1;
  }

  if (read_file(input_path, &input) != 0)
    return 1;

  if (looks_wrapped(input.data, input.size)) {
    if (validate_existing_wrapper(input.data, input.size) != 0)
      goto done;

    if (in_place) {
      fprintf(stderr, "INFO: Program already wrapped. Nothing to do.\n");
      rc = 0;
    } else {
      fprintf(stderr, "ERROR: Program is already wrapped; refusing to wrap it again.\n");
    }
    goto done;
  }

  if (find_sys_address(input.data, input.size, &sys_address) != 0)
    goto done;

  if (make_wrapped(input.data, input.size, &wrapped, &wrapped_size, sys_address) != 0)
    goto done;

  if (in_place) {
    size_t temp_len = strlen(input_path) + sizeof(".m65wrap.tmp");
    char *temp_path = (char *)malloc(temp_len);

    if (!temp_path) {
      fprintf(stderr, "ERROR: Out of memory constructing temporary filename.\n");
      goto done;
    }

    snprintf(temp_path, temp_len, "%s.m65wrap.tmp", input_path);
    if (write_file(temp_path, wrapped, wrapped_size) != 0) {
      free(temp_path);
      goto done;
    }

    if (replace_file(temp_path, input_path) != 0) {
      free(temp_path);
      goto done;
    }
    free(temp_path);
  } else {
    if (write_file(output_path, wrapped, wrapped_size) != 0)
      goto done;
  }

  fprintf(stderr, "INFO: Wrapped program: SYS $%04x, DMA copy %lu bytes.\n", sys_address,
      (unsigned long)(input.size - 2));
  rc = 0;

done:
  free(wrapped);
  free(input.data);
  return rc;
}
