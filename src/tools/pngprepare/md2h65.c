/*
 * Markdown to H65 page formatter.
 * H65 is a very simple (if inefficient) rich hypertext standard
 * for the MEGA65.  It allows text, pictures and hyperlinks only
 * at this stage.  It largely works by pre-formatting a MEGA65
 * screen + colour RAM, and providing the custom FCM character data
 * required to satisfy this.
 *
 * File contains a header that is searched for by the viewer.
 * This contains the lengths of the various fields, and where they
 * should be loaded into memory, with the first 64KB of RAM being
 * reserved for the viewer. Screen data is expected at
 * $12000-$17FFF, colour RAM will load to $FF80000-$FF87FFF for
 * compatibility with MEGA65 models that have only 32KB colour RAM.
 * Hyperlinks are described in $18000-$1F7FF:
 * .word offset to list of link boundaries
 * List of null-terminated URLs.
 * List of screen-RAM offsets, hyperlink length and target link tuples.
 * (4 bytes each).
 * Tile data is allowed to be placed in banks 4 and 5.  Replacing
 * the 128KB ROM with page data is not currently allowed.
 *
 * Header:
 *
 * .dword "H65<FF>" (0x48, 0x36, 0x35, 0xFF) is required.
 * .byte version major, version minor
 * .byte screen line width
 * .word number of screen lines
 * .byte border colour, screen colour, initial text colour
 * <skip to offset $80>
 * [ .dword address, length
 *   .byte data .... ]
 * ...
 * .dword $0 -- end of file marker
 *
 * Parts:
 * Copyright 2002-2010 Guillaume Cottenceau.
 * Copyright 2015-2022 Paul Gardner-Stephen.
 *
 * This software may be freely redistributed under the terms
 * of the X11 license.
 *
 */

/* ============================================================= */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>

// For images
#define PNG_DEBUG 3
#include <png.h>

// For fonts
#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <unistd.h>

extern unsigned char ascii_font[4097];

struct tile_set *ts = NULL;

/* ============================================================= */

/* ============================================================= */

int x, y;
int screen_x = 0, screen_y = 0;

int width, height;
png_byte color_type;
png_byte bit_depth;

png_structp png_ptr;
png_infop info_ptr;
int number_of_passes;
png_bytep *row_pointers;
int multiplier;

FT_Library library;
#define FONT_PARAGRAPH 0
#define FONT_H1 1
#define FONT_H2 2
#define FONT_H3 3
#define FONT_PARAGRAPH_BOLD 4
#define FONT_PARAGRAPH_BOLDITALIC 5
#define FONT_PARAGRAPH_ITALIC 6
#define MAX_TYPEFACES (FONT_PARAGRAPH_ITALIC + 1)
FT_Face type_faces[MAX_TYPEFACES];
int current_font = FONT_PARAGRAPH;

FT_GlyphSlot glyph_slot;
FT_Error error;

int n;

/* ============================================================= */

void abort_(const char *s, ...)
{
  va_list args;
  va_start(args, s);
  vfprintf(stderr, s, args);
  fprintf(stderr, "\n");
  va_end(args);
  abort();
}

/* ============================================================= */

struct tile {
  unsigned char bytes[8][8];
};

struct rgb {
  int r;
  int g;
  int b;
};

// We only have 128KB of tile RAM, so we can't have more than 128K different
// coloured pixels
#define MAX_COLOURS (128 * 1024)

// If we have >256 colours, though, we do need to reduce the final palette down
// to 256 colours.
int second_pass_required = 0;
int pass_num = 1;

struct tile_set {
  struct tile *tiles;
  int tile_count;
  int max_tiles;

  // Palette
  struct rgb colours[MAX_COLOURS];
  int colour_counts[MAX_COLOURS];
  int target_colours[MAX_COLOURS];
  int colour_count;

  struct tile_set *next;
};

int find_nearest_colour(struct tile_set *ts, int c)
{
  int nearest_id = 0;
  int error = 999999999;

  for (int i = 0; i < ts->colour_count; i++)
    // Has colour been remapped already, or is it the same colour we are looking to approximate?
    if ((i != c) && ts->target_colours[i] == i) {
      int this_error = 0 + abs(ts->colours[i].r - ts->colours[c].r) * abs(ts->colours[i].r - ts->colours[c].r)
                     + abs(ts->colours[i].g - ts->colours[c].g) * abs(ts->colours[i].g - ts->colours[c].g)
                     + abs(ts->colours[i].b - ts->colours[c].b) * abs(ts->colours[i].b - ts->colours[c].b);
      if (this_error < error) {
        nearest_id = i;
        error = this_error;
      }
    }

  return nearest_id;
}

void quantise_colours(struct tile_set *ts)
{
  // Start with all colours mapped to themselves.
  for (int i = 0; i < ts->colour_count; i++)
    ts->target_colours[i] = i;

  int colour_count = ts->colour_count;
  // Colour $FF = 255 has trouble in FCM chars, so don't use it
  while (colour_count > 254) {
    int freq = 999999999;
    int colour_num = 99999999;
    // Don't remap the C64 normal 16 colours
    for (int i = 16; i < ts->colour_count; i++) {
      // Has colour been remapped already?
      if (ts->target_colours[i] == i) {
        // No, so check its frequency to see if it is the next rarest colour
        if (ts->colour_counts[i] < freq) {
          freq = ts->colour_counts[i];
          colour_num = i;
        }
      }
    }

    // Find the nearest colour to this one
    int nearest_colour = find_nearest_colour(ts, colour_num);
    ts->target_colours[colour_num] = nearest_colour;

    // Increase the weighting of the colour we have switched to
    ts->colour_counts[nearest_colour] += ts->colour_counts[colour_num];

    printf("Removing rarely used colour #%d (used %d times): Mapping #%02x%02x%02x -> #%02x%02x%02x\n", colour_num, freq,
        ts->colours[colour_num].r, ts->colours[colour_num].g, ts->colours[colour_num].b, ts->colours[nearest_colour].r,
        ts->colours[nearest_colour].g, ts->colours[nearest_colour].b);

    colour_count--;
  }
}

void palette_c64_init(struct tile_set *ts)
{
  // Pre-load in C64 palette, so that those colours can be re-used if required

  ts->colours[0] = (struct rgb) { .r = 0, .g = 0, .b = 0 };
  ts->colours[1] = (struct rgb) { .r = 0xff, .g = 0xff, .b = 0xff };
  ts->colours[2] = (struct rgb) { .r = 0xab, .g = 0x31, .b = 0x26 };
  ts->colours[3] = (struct rgb) { .r = 0x66, .g = 0xda, .b = 0xff };
  ts->colours[4] = (struct rgb) { .r = 0xbb, .g = 0x3f, .b = 0xb8 };
  ts->colours[5] = (struct rgb) { .r = 0x55, .g = 0xce, .b = 0x58 };
  ts->colours[6] = (struct rgb) { .r = 0x1d, .g = 0x0e, .b = 0x97 };
  ts->colours[7] = (struct rgb) { .r = 0xea, .g = 0xf5, .b = 0x7c };
  ts->colours[8] = (struct rgb) { .r = 0xb9, .g = 0x74, .b = 0x18 };
  ts->colours[9] = (struct rgb) { .r = 0x78, .g = 0x73, .b = 0x00 };
  ts->colours[10] = (struct rgb) { .r = 0xdd, .g = 0x93, .b = 0x87 };
  ts->colours[11] = (struct rgb) { .r = 0x5b, .g = 0x5b, .b = 0x5b };
  ts->colours[12] = (struct rgb) { .r = 0x8b, .g = 0x8b, .b = 0x8b };
  ts->colours[13] = (struct rgb) { .r = 0xb0, .g = 0xf4, .b = 0xac };
  ts->colours[14] = (struct rgb) { .r = 0xaa, .g = 0x9d, .b = 0xef };
  ts->colours[15] = (struct rgb) { .r = 0xb8, .g = 0xb8, .b = 0xb8 };
  ts->colour_count = 16;
  fprintf(stderr, "Setup C64 palette.\n");
}

int palette_lookup(struct tile_set *ts, int r, int g, int b)
{
  int i;

  // Do we know this colour already?
  for (i = 0; i < ts->colour_count; i++) {
    if (r == ts->colours[i].r && g == ts->colours[i].g && b == ts->colours[i].b) {
      // It's a colour we have seen before, so return the index
      if (pass_num == 1)
        ts->colour_counts[i]++;
      if (pass_num == 2) {
        // Resolve remapped/merged colours
        while (ts->target_colours[i] != i)
          i = ts->target_colours[i];
      }
      return i;
    }
  }

  // new colour, check if palette has space
  if (ts->colour_count == 255) {
    fprintf(stderr, "WARNING: Image has many colours. A second pass will be required.\n");
    second_pass_required = 1;
  }

  // allocate the new colour
  ts->colours[ts->colour_count].r = r;
  ts->colours[ts->colour_count].g = g;
  ts->colours[ts->colour_count].b = b;
  ts->colour_counts[ts->colour_count] = 1;
  return ts->colour_count++;
}

unsigned char nonyblswap(unsigned char in)
{
  return in;
}

unsigned char nyblswap(unsigned char in)
{
  return ((in & 0xf) << 4) + ((in & 0xf0) >> 4);
}

struct tile_set *new_tileset(int max_tiles)
{
  struct tile_set *ts = calloc(sizeof(struct tile_set), 1);
  if (!ts) {
    perror("calloc() failed");
    exit(-3);
  }
  ts->tiles = calloc(sizeof(struct tile), max_tiles);
  if (!ts->tiles) {
    perror("calloc() failed");
    exit(-3);
  }
  ts->max_tiles = max_tiles;
  return ts;
}

struct screen {
  // Unique identifier
  unsigned char screen_id;
  // Which tile set the screen uses
  struct tile_set *tiles;
  unsigned char width, height;
  unsigned char **screen_rows;
  unsigned char **colourram_rows;

  struct screen *next;
};

struct screen *new_screen(int id, struct tile_set *tiles, int width, int height)
{
  struct screen *s = calloc(sizeof(struct screen), 1);
  if (!s) {
    perror("calloc() failed");
    exit(-3);
  }

  if ((width < 1) || (width > 255) || (height < 1) | (height > 255)) {
    fprintf(stderr, "Illegal screen dimensions, must be 1-255 x 1-255 characters.\n");
    exit(-3);
  }

  s->screen_id = id;
  s->tiles = tiles;
  s->width = width;
  s->height = height;
  s->screen_rows = calloc(sizeof(unsigned char *), height);
  s->colourram_rows = calloc(sizeof(unsigned char *), height);
  if ((!s->screen_rows) || (!s->colourram_rows)) {
    perror("calloc() failed");
    exit(-3);
  }
  for (int i = 0; i < height; i++) {
    s->screen_rows[i] = calloc(sizeof(unsigned char) * 2, width);
    s->colourram_rows[i] = calloc(sizeof(unsigned char) * 2, width);
    if ((!s->screen_rows[i]) || (!s->colourram_rows[i])) {
      perror("calloc() failed");
      exit(-3);
    }
  }

  return s;
}

int tile_lookup(struct tile_set *ts, struct tile *t)
{
  // See if tile matches any that we have already stored.
  // (Also check if it matches flipped in either or both X,Y
  // axes.
  for (int i = 0; i < ts->tile_count; i++) {
    int matches = 1;
    // Compare unflipped
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++)
        if (ts->tiles[i].bytes[x][y] != t->bytes[x][y]) {
          matches = 0;
          break;
        }
    if (matches)
      return i;
    // Compare with flipped X
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++)
        if (ts->tiles[i].bytes[x][y] != t->bytes[7 - x][y]) {
          matches = 0;
          break;
        }
    if (matches)
      return i | 0x4000;
    // Compare with flipped Y
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++)
        if (ts->tiles[i].bytes[x][y] != t->bytes[x][7 - y]) {
          matches = 0;
          break;
        }
    if (matches)
      return i | 0x8000;
    // Compare with flipped X and Y
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++)
        if (ts->tiles[i].bytes[x][y] != t->bytes[7 - x][7 - y]) {
          matches = 0;
          break;
        }
    if (matches)
      return i | 0xC000;
  }

  // The tile is new.
  if (ts->tile_count >= ts->max_tiles) {
    fprintf(stderr, "ERROR: Used up all %d tiles.\n", ts->max_tiles);
    exit(-3);
  }

  // Allocate new tile and return
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++)
      ts->tiles[ts->tile_count].bytes[x][y] = t->bytes[x][y];
  return ts->tile_count++;
}

struct screen *png_to_screen(int id, struct tile_set *ts)
{
  int x, y;

  struct screen *s = new_screen(id, ts, 1 + (width / 8), 1 + (height / 8));

  for (y = 0; y < height; y += 8)
    for (x = 0; x < width; x += 8) {
      int transparent_tile = 1;
      struct tile t;
      for (int yy = 0; yy < 8; yy++) {
        if ((yy + y) < height) {
          png_byte *row = row_pointers[yy + y];
          for (int xx = 0; xx < 8; xx++) {
            int r, g, b, a, c;
            if ((xx + x) < width) {
              png_byte *ptr = &(row[(xx + x) * multiplier]);
              r = ptr[0];
              g = ptr[1];
              b = ptr[2];
              if (multiplier == 4)
                a = ptr[3];
              else
                a = 0xff;
              if (a) {
                transparent_tile = 0;
                c = palette_lookup(ts, r, g, b);
              }
              else
                c = 0;
            }
            else {
              // Off edge of image
              c = 0;
            }
            t.bytes[xx][yy] = c;
          }
        }
        else {
          // Off edge of image
          for (int xx = 0; xx < 8; xx++) {
            t.bytes[xx][yy] = 0;
          }
        }
      }
      if (transparent_tile) {
        // Set screen and colour bytes to all $00 to indicate
        // non-set block.
        s->screen_rows[y / 8][x * 2 + 0] = 0x00;
        s->screen_rows[y / 8][x * 2 + 1] = 0x00;
        s->colourram_rows[y / 8][x * 2 + 0] = 0x00;
        s->colourram_rows[y / 8][x * 2 + 1] = 0x00;
      }
      else {
        // Block has non-transparent pixels, so add to tileset,
        // or lookup to see if it is already there.
        int tile_number = tile_lookup(ts, &t);

        // Adjust tile number in screen data for address of tile in RAM
        tile_number += (0x40000 / 0x40);

        s->screen_rows[y / 8][x / 8 * 2 + 0] = tile_number & 0xff;
        s->screen_rows[y / 8][x / 8 * 2 + 1] = (tile_number >> 8) & 0xff;
        s->colourram_rows[y / 8][x / 8 * 2 + 0] = 0x00;
        // FG colour
        // XXX Must be <$10, as we have VIC-III attributes enabled
        s->colourram_rows[y / 8][x / 8 * 2 + 1] = 0x00;
      }
    }
  return s;
}

void read_png_file(char *file_name)
{
  unsigned char header[8]; // 8 is the maximum size that can be checked

  /* open file and test for it being a png */
  FILE *infile = fopen(file_name, "rb");
  if (infile == NULL)
    abort_("[read_png_file] File %s could not be opened for reading", file_name);

  fread(header, 1, 8, infile);
  if (png_sig_cmp(header, 0, 8))
    abort_("[read_png_file] File %s is not recognized as a PNG file", file_name);

  /* initialize stuff */
  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if (!png_ptr)
    abort_("[read_png_file] png_create_read_struct failed");

  info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr)
    abort_("[read_png_file] png_create_info_struct failed");

  if (setjmp(png_jmpbuf(png_ptr)))
    abort_("[read_png_file] Error during init_io");

  png_init_io(png_ptr, infile);
  png_set_sig_bytes(png_ptr, 8);

  // Convert palette to RGB values
  png_set_expand(png_ptr);

  png_read_info(png_ptr, info_ptr);

  width = png_get_image_width(png_ptr, info_ptr);
  height = png_get_image_height(png_ptr, info_ptr);
  color_type = png_get_color_type(png_ptr, info_ptr);
  bit_depth = png_get_bit_depth(png_ptr, info_ptr);

  printf("Input-file is: width=%d, height=%d.\n", width, height);

  number_of_passes = png_set_interlace_handling(png_ptr);
  png_read_update_info(png_ptr, info_ptr);

  /* read file */
  if (setjmp(png_jmpbuf(png_ptr)))
    abort_("[read_png_file] Error during read_image");

  row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
  for (y = 0; y < height; y++)
    row_pointers[y] = (png_byte *)malloc(png_get_rowbytes(png_ptr, info_ptr));

  png_read_image(png_ptr, row_pointers);

  if (infile != NULL) {
    fclose(infile);
    infile = NULL;
  }

  if (png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_RGB)
    multiplier = 3;
  else if (png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_RGBA)
    multiplier = 4;
  else {
    fprintf(stderr, "Could not convert file to RGB or RGBA\n");
    exit(-3);
  }

  return;
}

/* ============================================================= */

// Only 24KB colour RAM and screen RAM available
#define MAX_COLOURRAM_SIZE (24 * 1024)

int i, x = 0, y = 0;
int colour = 14; // C64 light blue by default
unsigned char text_colour = 14;
unsigned char text_colour_saved = 14;
unsigned char indent = 0;
unsigned char attributes = 0;
unsigned char attributes_saved = 0;
unsigned char screen_ram[MAX_COLOURRAM_SIZE];
unsigned char colour_ram[MAX_COLOURRAM_SIZE];
#define MAX_LINE_LENGTH 80
const int max_lines = (MAX_COLOURRAM_SIZE / (MAX_LINE_LENGTH * 2));
unsigned int screen_ram_used = 0;
unsigned int in_paragraph = 0;
unsigned int queued_word_gap = 0;

// Buffer for partially accumulated lines of text
#define MAX_LINE_HEIGHT 64
#define MAX_LINE_DEPTH 64
unsigned char accline_screen_ram[MAX_LINE_HEIGHT + MAX_LINE_DEPTH][MAX_LINE_LENGTH * 2];
unsigned char accline_colour_ram[MAX_LINE_DEPTH + MAX_LINE_DEPTH][MAX_LINE_LENGTH * 2];
int accline_len = 0;
int accline_display_len = 0;
int accline_height = 1;
int accline_depth = 0;

// And the same for the current word being rendered
unsigned char accword_screen_ram[MAX_LINE_HEIGHT + MAX_LINE_DEPTH][MAX_LINE_LENGTH * 2];
unsigned char accword_colour_ram[MAX_LINE_HEIGHT + MAX_LINE_DEPTH][MAX_LINE_LENGTH * 2];
int accword_len = 0;
int accword_display_len = 0;
int accword_height = 1;
int accword_depth = 0;

#define MAX_URLS 255
#define MAX_URL_LEN 255
char urls[MAX_URLS][MAX_URL_LEN];
int url_addrs[MAX_URLS];
int url_count = 0;
int bounding_box_count = 0;
struct bounding_box {
  int url_id;
  int x1, x2, y1, y2;
};
#define MAX_LINKS 4096
struct bounding_box url_boxes[MAX_LINKS];
int link_count = 0;

void register_box(int url_id, int x1, int y1, int x2, int y2)
{
  for (int i = 0; i < link_count; i++) {
    if (url_boxes[i].url_id == url_id && url_boxes[i].x1 == x1 && url_boxes[i].y1 == y1 && url_boxes[i].x2 == x2
        && url_boxes[i].y2 == y2)
      return;
  }
  if (link_count >= MAX_LINKS) {
    fprintf(stderr, "ERROR: Too many link sources. Increase MAX_LINKS?\n");
    exit(-1);
  }

  url_boxes[link_count].url_id = url_id;
  url_boxes[link_count].x1 = x1;
  url_boxes[link_count].y1 = y1;
  url_boxes[link_count].x2 = x2;
  url_boxes[link_count++].y2 = y2;
}

int register_url(char *url)
{
  for (int i = 0; i < url_count; i++) {
    if (!strcmp(url, urls[i]))
      return i;
  }
  if (url_count >= MAX_URLS) {
    fprintf(stderr, "ERROR: Too many URLs. Increase MAX_URLS?\n");
    exit(-1);
  }
  strcpy(urls[url_count++], url);
  return url_count - 1;
}

#define MAX_ATTRIBUTES 16
char attribute_keys[MAX_ATTRIBUTES][1024];
char attribute_values[MAX_ATTRIBUTES][1024];
int attribute_count = 0;

void parse_attributes(char *in)
{
  printf("INFO: Parsing attribute string '%s'\n", in);
  attribute_count = 0;
  char key[1024];
  char value[1024];
  int klen = 0;
  int vlen = 0;
  int state = 0;
  for (int i = 0; in[i]; i++) {
    switch (state) {
    case 0:
      switch (in[i]) {
      case ',': // next attribute (end of keyword)
        if (klen) {
          if (attribute_count >= MAX_ATTRIBUTES) {
            fprintf(stderr, "ERROR: Too many attributes in attribute string '%s'\n", in);
            exit(-1);
          }
          strcpy(attribute_keys[attribute_count], key);
          attribute_values[attribute_count++][0] = 0;
          klen = 0;
        }
        break;
      case '=': // value follows
        state = 1;
        break;
      default:
        if (klen < 1023)
          key[klen++] = in[i];
        key[klen] = 0;
      }
      break;
    case 1: // value of key=val pair
      switch (in[i]) {
      case ',':
        // End of attribute
        if (attribute_count >= MAX_ATTRIBUTES) {
          fprintf(stderr, "ERROR: Too many attributes in attribute string '%s'\n", in);
          exit(-1);
        }
        strcpy(attribute_keys[attribute_count], key);
        strcpy(attribute_values[attribute_count++], value);
        fprintf(stderr, "Attrib tag: '%s'='%s'\n", key, value);
        klen = 0;
        vlen = 0;
        state = 0;
        break;
      default:
        if (vlen < 1023)
          value[vlen++] = in[i];
        value[vlen] = 0;
      }
    }
  }
  if (state == 1) {
    strcpy(attribute_keys[attribute_count], key);
    strcpy(attribute_values[attribute_count++], value);
    printf("INFO: Attrib tag: '%s'='%s'\n", key, value);
  }
}

char *get_attribute(char *key)
{
  for (int i = 0; i < attribute_count; i++) {
    if (!strcmp(key, attribute_keys[i]))
      return attribute_values[i];
  }
  return NULL;
}

void emit_accumulated_line(void)
{
  if (accline_len) {
    // XXX - Emit accumulated line by copying the rows of screen and colour RAM into place.
    int first_row = MAX_LINE_HEIGHT - accline_height;
    int last_row = MAX_LINE_HEIGHT - 1 + accline_depth;
    printf("[Line = +%d,-%d, first=%d, last=%d, accline_len=%d]\n", accline_height, accline_depth, first_row, last_row,
        accline_len);
    for (int row = first_row; row <= last_row; row++) {
      printf("DEBUG: Emitting row %d into y=%d\n", row, screen_y);

      if (screen_y * (MAX_LINE_LENGTH * 2) >= MAX_COLOURRAM_SIZE) {
        fprintf(stderr, "ERROR: Page too long. Split into separate files, or use smaller fonts.\n");
        exit(-1);
      }

      memcpy(&screen_ram[screen_y * (MAX_LINE_LENGTH * 2)], accline_screen_ram[row], accline_len * 2);
      memcpy(&colour_ram[screen_y * (MAX_LINE_LENGTH * 2)], accline_colour_ram[row], accline_len * 2);
      screen_y++;
    }

    // XXX - Implement indent to allow text to flow around images

    // Set next draw position to start of the next line
    screen_x = 0;
  }

  // Reset accumulated line
  accline_len = 0;
  accline_display_len = 0;
  accline_height = 1;
  accline_depth = 0;
}

void paragraph_font(void)
{
  text_colour = 14;
  attributes = 0;
  current_font = FONT_PARAGRAPH;
}

void next_paragraph(void)
{
  // End a previous paragraph, if one was started.
  emit_accumulated_line();

  if (in_paragraph) {
    printf("[YSkip for new paragraph]\n");
    screen_y++;
    in_paragraph = 0;
    indent = 0;
  }

  queued_word_gap = 0;

  // Reset to paragraph font after
  paragraph_font();
}

void next_paragraph_no_gap(void)
{
  // End a previous paragraph, if one was started.
  emit_accumulated_line();

  if (in_paragraph) {
    screen_x = 0;
    in_paragraph = 0;
    indent = 0;
  }

  // Reset to paragraph font after
  paragraph_font();

  current_font = FONT_PARAGRAPH;
  attributes = 0x00;
  text_colour = 14;
}

// Accumulated word (int type to allow use of unicode points)
int word[1024];
int word_len = 0;

// clang-format off
struct tile blank_tile = {
  { {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0}
  }   
};
// clang-format on

#define MAX_BITMAP_SIZE 640
#define BITMAP_BASELINE (MAX_BITMAP_SIZE / 2)
unsigned char glyph_bitmap[MAX_BITMAP_SIZE][MAX_BITMAP_SIZE];

int encode_glyph_card(int card_x, int card_y, struct tile_set *ts)
{
  int base_x = card_x * 16;
  // Y=0 is the card directly above the baseline, Y=-1 is directly below the baseline
  int base_y = BITMAP_BASELINE - (card_y + 1) * 8;

  struct tile t;

  int x, y;
  for (y = 0; y < 8; y++) {
    for (x = 0; x < 8; x++) {
      int x_pos = x * 2 + base_x;
      int y_pos = y + base_y;

      // printf("pixel (%d,%d) will be in bitmap at (%d,%d)\n",
      //	 x,y,x_pos,y_pos);

      int low = glyph_bitmap[x_pos + 0][y_pos];
      int hi = glyph_bitmap[x_pos + 1][y_pos];
      low = (low >> 4) & 0xf;
      hi = hi & 0xf0;
      t.bytes[x][y] = hi + low;
    }
  }

  if (1) {
    printf("card (%d,%d) is:\n", card_x, card_y);
    for (y = 0; y < 8; y++) {
      for (x = 0; x < 8; x++) {
        if ((t.bytes[x][y] & 0xf) == 0)
          printf(".");
        else if ((t.bytes[x][y] & 0xf) < 8)
          printf("+");
        else
          printf("*");
        if (t.bytes[x][y] < 0x10)
          printf(".");
        else if ((t.bytes[x][y] & 0xf0) < 128)
          printf("+");
        else
          printf("*");
      }
      printf("\n");
    }
  }

  return tile_lookup(ts, &t)
       // Adjust tile number in screen data for address of tile in RAM
       + (0x40000 / 0x40);
}

int render_codepoints(int *code_points, int num)
{
  glyph_slot = type_faces[current_font]->glyph;

  int total_width = 0;
  int count = 0;

  int max_height = 0;
  int max_under = 0;

  // Clear bitmap
  for (int i = 0; i < MAX_BITMAP_SIZE; i++)
    bzero(glyph_bitmap[i], MAX_BITMAP_SIZE);

  // Try to make sure that we can use the full width, by combining glyphs until we have at least 8 in every
  // char block
  while ((total_width <= 11) && count < num) {
    int code_point = code_points[count++];

    int glyph_index = FT_Get_Char_Index(type_faces[current_font], code_point);
    error = FT_Load_Glyph(type_faces[current_font], glyph_index, FT_LOAD_RENDER);
    if (error) {
      fprintf(stderr, "ERROR: Could not find glyph for Unicode Point 0x%x in font.\n", code_point);
      exit(-1);
    }
    printf("bitmap_left=%d, bitmap_top=%d\n", glyph_slot->bitmap_left, glyph_slot->bitmap_top);
    printf("bitmap_width=%d, bitmap_rows=%d\n", glyph_slot->bitmap.width, glyph_slot->bitmap.rows);

    // XXX - We ignore bitmap_left, which is used for some kerning functions
    int glyph_display_width = /* glyph_slot->bitmap_left+ */ glyph_slot->bitmap.width;
    if (glyph_display_width == 0) {
      glyph_display_width = (glyph_slot->metrics.horiAdvance / 64);
      printf("INFO: Adding %dpx inter-word gap.\n", glyph_display_width);
      total_width += glyph_display_width;
    }
    else {
      // Copy glyph into the bitmap
      int y_start = BITMAP_BASELINE - glyph_slot->bitmap_top;
      printf("y_start=%d\n", y_start);
      for (int y = 0; y < glyph_slot->bitmap.rows; y++) {
        printf("  row %d : ", y);
        for (int i = 0; i < glyph_display_width; i++) {
          if (glyph_slot->bitmap.buffer[i + y * glyph_slot->bitmap.width])
            printf("+");
          else
            printf(".");
          glyph_bitmap[total_width + i][y_start + y] = glyph_slot->bitmap.buffer[i + y * glyph_slot->bitmap.width];
        }
        printf("\n");
      }
      total_width += glyph_display_width;
      if (max_height < glyph_slot->bitmap_top)
        max_height = glyph_slot->bitmap_top;
      if (max_under < (glyph_slot->bitmap.rows - glyph_slot->bitmap_top))
        max_under = glyph_slot->bitmap.rows - glyph_slot->bitmap_top;
    }
  }

  int char_rows = 0, char_columns = 0;
  int under_rows = 0;

  // Work out size of char grid required to fit the glyph(s)
  char_rows = max_height / 8;
  if (max_height & 7)
    char_rows++;
  char_columns = total_width / 16;
  if (total_width & 15)
    char_columns++;
  under_rows = max_under / 8;
  if (max_under & 7)
    under_rows++;

  if (1)
    printf("Character is %dx%d cards above and %dx%d below, max_height=%d, max_under=%d\n", char_columns, char_rows,
        char_columns, under_rows, max_height, max_under);
  int x, y;

  printf("y range = %d..%d, width=%d\n", char_rows - 1, -under_rows, total_width);

  printf("total width of glyph(s) = %d\n", total_width);

  // Record number of pixels to trim from right-most tile
  int trim_pixels = 16 - (total_width & 15);
  if (!(total_width & 15))
    trim_pixels = 0;
  printf("total_width=%d, trim_pixels=%d\n", total_width, trim_pixels);

  // Now build the glyph map

  for (x = 0; x < char_columns; x++) {
    if (accword_len >= MAX_LINE_LENGTH) {
      fprintf(stderr, "ERROR: Word is too long:\n");
      exit(-1);
    }
    if (accword_display_len > 640) {
      fprintf(stderr, "ERROR: Rendered word is too long:\n");
      exit(-1);
    }
    if (char_rows > accword_height)
      accword_height = char_rows;
    if (under_rows > accword_depth)
      accword_depth = under_rows;

    // Blank out entire column, so that we avoid alignment issues with variable character heights
    int blank_card = tile_lookup(ts, &blank_tile);
    blank_card += (0x40000 / 0x40);

    int this_trim = 0;
    if (x == (char_columns - 1))
      this_trim = trim_pixels;

    for (y = 0; y < MAX_LINE_HEIGHT + MAX_LINE_DEPTH; y++) {
      accword_screen_ram[y][accword_len * 2 + 0] = blank_card >> 0;
      accword_screen_ram[y][accword_len * 2 + 1] = (blank_card >> 8) + (this_trim << 5);
      accword_colour_ram[y][accword_len * 2 + 0] = 0x20 + 0x08; // ALPHA + NCM glyph
      if (y == (MAX_LINE_HEIGHT - 1)) {
        accword_colour_ram[y][accword_len * 2 + 1] = text_colour + attributes;
      }
      else {
        // Do not apply underline to other than the base row
        accword_colour_ram[y][accword_len * 2 + 1] = (text_colour + attributes) & 0x7f;
      }
      if (this_trim & 8)
        accword_colour_ram[y][accword_len * 2 + 0] |= 0x04; // Trim 8 more pixels
    }

    for (y = char_rows - 1; y >= -under_rows; y--) {
      int card_number = encode_glyph_card(x, y, ts);
      printf("  encoding tile (%d,%d) using card $%04x in row store y=%d\n", x, y, card_number, MAX_LINE_HEIGHT - 1 - y);
      // Write tile details into accline_screen_ram and accline_colour_ram
      accword_screen_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 0] = card_number >> 0;
      accword_screen_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 1] = (card_number >> 8) + (this_trim << 5);
      accword_colour_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 0] = 0x20 + 0x08; // ALPHA + NCM glyph
      if (!y) {
        accword_colour_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 1] = text_colour + attributes;
      }
      else {
        // Do not apply underline to other than the base row
        accword_colour_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 1] = (text_colour + attributes) & 0x7f;
      }
      if (this_trim & 8)
        accword_colour_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 0] |= 0x04; // Trim 8 more pixels
      else {
        // Do not apply underline to other than the base row
        accword_colour_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 1] = (text_colour + attributes) & 0x7f;
      }
      if (trim_pixels & 8)
        accword_colour_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 0] |= 0x04; // Trim 8 more pixels
    }
    for (y = char_rows - 1; y >= -under_rows; y--) {
      int card_number = encode_glyph_card(x, y, ts);
      printf("  encoding tile (%d,%d) using card $%04x\n", x, y, card_number);
      // Write tile details into accline_screen_ram and accline_colour_ram
      accword_screen_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 0] = card_number >> 0;
      accword_screen_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 1] = (card_number >> 8) + (this_trim << 5);
      accword_colour_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 0] = 0x20 + 0x08; // ALPHA + NCM glyph
      if (!y) {
        accword_colour_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 1] = text_colour + attributes;
      }
      else {
        // Do not apply underline to other than the base row
        accword_colour_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 1] = (text_colour + attributes) & 0x7f;
      }
      if (this_trim & 8)
        accword_colour_ram[MAX_LINE_HEIGHT - 1 - y][accword_len * 2 + 0] |= 0x04; // Trim 8 more pixels
    }
    accword_len++;
  }
  accword_display_len += total_width;
  return count;
}

int emit_rendered_word(void)
{
  if ((accline_len + accword_len) > MAX_LINE_LENGTH) {
    printf("WARNING: Line too long, truncating.\n");
    accword_len = MAX_LINE_LENGTH - accline_len;
  }
  if (accline_display_len + accword_display_len > 640) {
    printf("WARNING: Line too long, and will be clipped by the side border.\n");
  }
  for (int j = 0; j < MAX_LINE_HEIGHT + MAX_LINE_DEPTH; j++) {
    //    int show=0;
    //    if ((j>=(MAX_LINE_HEIGHT-accword_height))&&(j<(MAX_LINE_HEIGHT+accword_depth))) show=1;
    //    if (show) printf("DEBUG: copy row %d: ",j);
    for (int i = 0; i < accword_len * 2; i++) {
      //      if (show) printf("%02x",accword_screen_ram[j][i]);
      accline_screen_ram[j][accline_len * 2 + i] = accword_screen_ram[j][i];
      accline_colour_ram[j][accline_len * 2 + i] = accword_colour_ram[j][i];
    }
    //    if (show) printf("\n");
  }
  accline_len += accword_len;
  accline_display_len += accword_display_len;
  printf("DEBUG: Appended %d columns to accline. accline_len=%d, accword_h=%d,_d=%d\n", accword_len, accline_len,
      accword_height, accword_depth);
  if (accword_height > accline_height)
    accline_height = accword_height;
  if (accword_depth > accline_depth)
    accline_depth = accword_depth;

  accword_height = 1;
  accword_depth = 0;
  accword_len = 0;
  accword_display_len = 0;
  for (int j = 0; j < MAX_LINE_HEIGHT + MAX_LINE_DEPTH; j++) {
    bzero(accword_screen_ram[j], MAX_LINE_LENGTH * 2);
    bzero(accword_colour_ram[j], MAX_LINE_LENGTH * 2);
  }
  return 0;
}

int emit_word_gap(void)
{
  if (!type_faces[current_font]) {
    // C64 ASCII Font
    printf(" ");
    // Draw single char high C64 ASCII font space following the previous word
    accline_screen_ram[MAX_LINE_HEIGHT - 1][accline_len * 2 + 0] = 0x20;
    accline_screen_ram[MAX_LINE_HEIGHT - 1][accline_len * 2 + 1] = 0;
    accline_colour_ram[MAX_LINE_HEIGHT - 1][accline_len * 2 + 0] = 0;
    accline_colour_ram[MAX_LINE_HEIGHT - 1][accline_len * 2 + 1] = text_colour + attributes;
    accline_len++;
  }
  else {
    printf("!!!SPACE!!!");
    int cp = 0x20;
    render_codepoints(&cp, 1); // SPACE
    emit_rendered_word();
  }
  queued_word_gap = 0;
  return 0;
}

int emit_accumulated_word(void)
{
  if (!word_len) {
    accword_height = 1;
    accword_depth = 0;
    return 0;
  }

  if (!type_faces[current_font]) {
    int len = word_len;
    // Is there room for the word and a space (or just the word, if there is nothing on the line so far)
    if ((accline_len && (MAX_LINE_LENGTH - accline_len) < (1 + len))
        || ((!accline_len) && (MAX_LINE_LENGTH - accline_len) < len)) {
      queued_word_gap = 0;
      emit_accumulated_line();
    }
    // Draw single char high C64 ASCII font text directly
    for (int xx = 0; xx < len; xx++) {
      if (accline_len >= MAX_LINE_LENGTH) {
        queued_word_gap = 0;
        emit_accumulated_line();
      }
      accline_screen_ram[MAX_LINE_HEIGHT - 1][accline_len * 2 + 0] = word[xx];
      accline_screen_ram[MAX_LINE_HEIGHT - 1][accline_len * 2 + 1] = 0;
      accline_colour_ram[MAX_LINE_HEIGHT - 1][accline_len * 2 + 0] = 0;
      accline_colour_ram[MAX_LINE_HEIGHT - 1][accline_len * 2 + 1] = text_colour + attributes;
      accline_len++;
    }
  }
  else {
    // True-type font

    int code_points[MAX_LINE_LENGTH];
    int count = 0;

    // 1. Build render of word into a temporary screen/colour RAM buffer
    for (int i = 0; i < word_len;) {
      // XXX - UTF-8 decode
      int extra_bytes = 0;
      int code_point = 0;
      if (word[i] < 0x80) {
        // Single-byte ASCII range
        code_point = word[i] & 0x7f;
      }
      else if ((word[i] & 0xe0) == 0xc0) {
        // 2 byte unicode
        extra_bytes = 1;
        code_point = word[i] & 0x1f;
      }
      else if ((word[i] & 0xf0) == 0xe0) {
        // 3 byte unicode
        extra_bytes = 2;
        code_point = word[i] & 0x0f;
      }
      else if ((word[i] & 0xf8) == 0xf0) {
        // 4 byte unicode
        extra_bytes = 3;
        code_point = word[i] & 0x07;
      }
      else {
        fprintf(stderr, "Illegal character 0x%02x in UTF-8 text stream.\n", word[i]);
        exit(-1);
      }
      i++;
      while (extra_bytes) {
        if ((word[i] & 0xc0) != 0x80) {
          fprintf(stderr, "Illegal extension byte 0x%02x in UTF-8 text stream.\n", word[i]);
          exit(-1);
        }
        // Add the 6 new bits to the code point
        code_point = code_point << 6;
        code_point += word[i] & 0x3f;
        extra_bytes--;
        i++;
      }
      code_points[count++] = code_point;
    }

    for (int i = 0; i < count;) {

      // 1.1. Get the glyph and render it into NCM blocks
      // XXX - Eventually check for NCM blocks with spare columns on the right-hand side, and
      // use them H-flipped to pack two chars into one NCM block to save memory. But for now,
      // we will just make normal NCM blocks for each glyph, with the usual de-duplication
      // logic.
      int num = render_codepoints(&code_points[i], count - i);
      printf("DEBUG: Output %d codepoints in one char.\n", num);
      printf("[CP=$%x]\n", code_points[i]);
      for (int j = 1; j < num; j++)
        printf("+ [CP=$%x]\n", code_points[i + j]);

      i += num;
    }

    // 2. Check if it is too wide for the current remaining line.
    //    If so, start it on the next line. If it's still too long, we will just clip it.
    //    If starting a new line, emit the accumulated line of text to start the new line.
    if ((accword_len + accline_len > MAX_LINE_LENGTH) || (accword_display_len + accline_display_len > 640)) {
      printf("INFO: Breaking line as either %d+%d>%d chars, or %d+%d>640 px\n", accline_len, accword_len, MAX_LINE_LENGTH,
          accline_display_len, accword_display_len);

      emit_accumulated_line();
    }
    else {
      printf("INFO: Continuing line as neither %d+%d>%d chars, or %d+%d>640 px\n", accline_len, accword_len, MAX_LINE_LENGTH,
          accline_display_len, accword_display_len);
    }

    // 3. Copy it into the current accumulating line of text.
    //    Update maximum above and below height of the line.
    emit_rendered_word();

    // Show the user the word we have emitted
    printf("<");
    for (int i = 0; i < word_len; i++) {
      printf("%c", word[i]);
    }
    printf(">");
    fflush(stdout);
  }

  word_len = 0;
  word[0] = 0;
  return 0;
}

void emit_text(char *text)
{
  /* Check for special formatting options:
     - Begins with * = enable bold/italic etc
     - Begins with ! = show image
     - Begins with digit followed by dot followed by space = ordered list
     - Begins with "- " = unordered list item
     - "---" = horizontal rule
     - Begins with "[" = link
     etc.

     For now, the parser is horrible, and only detects a very few things.
     Link labels and alt-text fields with spaces will not initially be supported.

     In fact, initially, we will support only bold = **text goes here**
  */

  int x1, y1;
  int len = strlen(text);

  char alttext[2048] = "", imgname[2048] = "", theurl[2048] = "";

  for (int i = 0; i < len;) {

    // Look for asterisks to toggle bold/italic
    int stars = 0;
    while ((i < len) && text[i] == '*') {
      stars++;
      i++;
    }

    int before = 1;

    // Emit accumulated word before setting fancy font face
    if (current_font == FONT_PARAGRAPH)
      before = 1;

    if (before && queued_word_gap)
      emit_word_gap();

    if (stars && word_len)
      emit_accumulated_word();

    switch (stars) {
    case 0:
      break;
    case 1: // Toggle italic
      if (current_font == FONT_PARAGRAPH) {
        current_font = FONT_PARAGRAPH_ITALIC;
        if (!type_faces[current_font])
          text_colour = 7;
      }
      else
        paragraph_font();
      break;
    case 2: // Toggle bold
      if (current_font == FONT_PARAGRAPH) {
        current_font = FONT_PARAGRAPH_BOLD;
        if (!type_faces[current_font]) {
          attributes = 0x20; // Reverse text for bold
        }
      }
      else
        paragraph_font();
      break;
    case 3: // Toggle bold+italic
      if (current_font == FONT_PARAGRAPH) {
        current_font = FONT_PARAGRAPH_ITALIC;
        if (!type_faces[current_font])
          text_colour = 7;
      }
      else
        paragraph_font();
      break;
    default:
      // Too many stars
      fprintf(stderr, "ERROR: Too many *'s in a row: only upto 3 are allowed. Escape with \\ for literals.\n");
      fprintf(stderr, "       Offending text is: %s\n", text);
      exit(-1);
    }
    if (stars)
      printf("{%d stars, wl=%d}", stars, word_len);

    // Emit word if we have just cancelled a fancy font face
    if ((!before) && queued_word_gap)
      emit_word_gap();

    // Check for [text](url) -- A link
    if (sscanf(&text[i], "[%[^]]](%[^)])%n", alttext, theurl, &n) == 2) {
      i += n;
      // Emit word and get the dimensions for it
      attributes_saved = attributes;
      attributes = 0x80; // underline links
      text_colour_saved = text_colour;
      text_colour = 0x04; // purple text
      // Remember where start of link appears
      x1 = screen_x;
      y1 = screen_y;
      emit_text(alttext);
      // If URL wrapped, then kludge to make whole link clickable
      if (screen_y > y1) {
        x1 = 0;
        y1 = screen_y;
      }
      // Return to whatever text colour and attributes we had before the link
      text_colour = text_colour_saved;
      attributes = 0;

      int url_id = register_url(theurl);
      register_box(url_id, x1, y1, screen_x, screen_y);

      in_paragraph = 1;
    }
    // ![alt-text](imagefile.png)
    // Currently we only support PNG images, and the alt text must
    else if (sscanf(&text[i], "![%[^]]](%[^)])%n", alttext, imgname, &n) == 2) {
      i += n;

      read_png_file(imgname);
      struct screen *s = png_to_screen(0, ts);

      // Check if the image has a link
      parse_attributes(alttext);
      if (get_attribute("href")) {
        int url_id = register_url(get_attribute("href"));
        register_box(url_id, screen_x, screen_y, screen_x + s->width - 1, screen_y + s->height - 1);
      }

      printf("INFO: Drawing image of %dx%d tiles\n", s->width, s->height);

      if (s->width > 80 || (s->height + screen_y) > max_lines) {
        fprintf(stderr, "ERROR: Not enough space left on page to fit image '%s'\n", imgname);
        exit(-1);
      }
      else {
        // Make sure we are on a fresh line before displaying an image,
        // but don't force a blank line, so images and text can be placed
        // flush
        next_paragraph_no_gap();

        // Draw the image on the screen and colour RAM
        for (y = 0; y < s->height; y++) {
          bcopy(&s->screen_rows[y][0], &screen_ram[(screen_y + y) * 80 * 2], s->width * 2);
          bcopy(&s->colourram_rows[y][0], &colour_ram[(screen_y + y) * 80 * 2], s->width * 2);
        }
        // Now advance our draw point to after the image
        screen_y += s->height;
      }

      in_paragraph = 1;
    }
    else if (!stars) {
      // Accumulate character into word, or terminate and emit word
      switch (text[i]) {
      case ' ':
      case '\t':
      case '\r':
      case '\n':
        // End of word
        emit_accumulated_word();
        in_paragraph = 1;
        queued_word_gap = 1;
        break;
      default:
        // Part of a word
        if (word_len < 1024) {
          word[word_len++] = text[i];
          word[word_len] = 0;
        }
        else {
          fprintf(stderr, "ERROR: Markdown contains a word that is too long.\n");
          exit(-1);
        }
        if (!in_paragraph)
          printf("[Char '%c' started paragraph]\n", text[i]);
        in_paragraph = 1;
        in_paragraph = 1;
        in_paragraph = 1;
      }
      i++;
    }
  }

  // And output any last bit of a word at the end of the text
  if (word_len)
    emit_accumulated_word();
}

int do_pass(char **argv, struct tile_set *ts)
{
  unsigned char block_header[8];

  screen_x = 0;
  screen_y = 0;

  // Initialise screen and colour RAM
  bzero(colour_ram, sizeof(colour_ram));
  for (i = 0; i < MAX_COLOURRAM_SIZE; i += 2) {
    screen_ram[i] = ' ';
    screen_ram[i + 1] = 0;
  }

  FILE *infile = fopen(argv[1], "r");
  if (!infile) {
    perror("Could not open input file");
    exit(-3);
  }

  FILE *outfile = fopen(argv[2], "wb");
  if (!outfile) {
    perror("Could not open output file");
    exit(-3);
  }

  // Read the .md file
  char line[1024];
  line[0] = 0;
  fgets(line, 1024, infile);
  while (line[0]) {
    char font_id_str[1024];
    char font_file[1024];
    int font_size;
    if (sscanf(line, "#define FONT(%[^)]) %[^,],%d", font_id_str, font_file, &font_size) == 3) {
      // Redefine a font
      int font_id = -1;
      if (!strcasecmp(font_id_str, "h1"))
        font_id = FONT_H1;
      if (!strcasecmp(font_id_str, "h2"))
        font_id = FONT_H2;
      if (!strcasecmp(font_id_str, "h3"))
        font_id = FONT_H3;
      if (!strcasecmp(font_id_str, "p"))
        font_id = FONT_PARAGRAPH;
      if (!strcasecmp(font_id_str, "bold"))
        font_id = FONT_PARAGRAPH_BOLD;
      if (!strcasecmp(font_id_str, "italic"))
        font_id = FONT_PARAGRAPH_ITALIC;
      if (!strcasecmp(font_id_str, "bolditalic"))
        font_id = FONT_PARAGRAPH_BOLDITALIC;
      if (font_id == -1) {
        fprintf(stderr,
            "ERROR: Unsupported FONT('%s') definition. Only h1, h2, h3, p, bold, bolditalic and italic are supported right "
            "now.\n",
            font_id_str);
        exit(-1);
      }
      // Try loading the font
      // Release old font, first.
      if (type_faces[font_id]) {
        FT_Done_Face(type_faces[font_id]);
        type_faces[font_id] = NULL;
      }
      error = FT_New_Face(library, font_file, 0, &type_faces[font_id]);
      if (error) {
        fprintf(stderr, "ERROR: Could not load font: %s\n", line);
        exit(-1);
      }
      error = FT_Set_Pixel_Sizes(type_faces[font_id], font_size, font_size);
      if (error) {
        fprintf(stderr, "ERROR: Could not set pixel size for font: %s\n", line);
        exit(-1);
      }
      glyph_slot = type_faces[font_id]->glyph;
      printf("INFO: Loaded font: %s as %s, size %d\n", font_id_str, font_file, font_size);
    }
    else if (!strncmp("# ", line, 2)) {
      // Heading H1
      printf("[in_para=%d]\n", in_paragraph);
      next_paragraph();
      printf("[in_para=%d]\n", in_paragraph);
      current_font = FONT_H1;
      text_colour = 1;   // white text for headings
      attributes = 0x80; // underline for headings
      emit_text(&line[2]);
      next_paragraph();
    }
    else if (!strncmp("## ", line, 3)) {
      // Heading H2
      next_paragraph();
      current_font = FONT_H2;
      text_colour = 1;   // white text for headings
      attributes = 0x80; // underline for headings
      emit_text(&line[3]);
      next_paragraph();
    }
    else if (!strncmp("### ", line, 4)) {
      // Heading H3
      next_paragraph();
      current_font = FONT_H3;
      text_colour = 1;   // white text for headings
      attributes = 0x80; // underline for headings
      emit_text(&line[4]);
      next_paragraph();
    }
    else if (line[0] == '\n' || line[0] == '\r') {
      // Blank line = paragraph break
      next_paragraph();
    }
    else {
      // Normal text
      attributes = 0; // cancel underline from a heading
      emit_text(&line[0]);
    }
    line[0] = 0;
    fgets(line, 1024, infile);
  }

  screen_ram_used = screen_y * 160 + screen_x * 2;

  printf("Writing headers...\n");
  unsigned char header[128];
  bzero(header, sizeof(header));
  // Magic bytes
  header[0] = 0x48;
  header[1] = 0x36;
  header[2] = 0x35;
  header[3] = 0xFF;
  // Version
  header[4] = 0x01;
  header[5] = 0x00;
  // Screen line width
  header[6] = 80;
  // $D054 value: Enable FCM for chars >$fF, enable alpha compositor
  header[7] = 0x85;
  // Number of chars per line to display
  header[8] = 80;
  // V400/H640 flags
  header[9] = 0xE8; // V400, H640, VIC-III attributes
  // Number of screen lines
  header[10] = screen_y & 0xff;
  header[11] = screen_y >> 8;

  // Screen colours
  header[12] = 0x06;
  header[13] = 0x06;
  header[14] = 0x01;

  // Charset absolute address
  // $1800 = lower case
  // $1000 = upper case
  // $F000 = custom ASCII charset we load at $F000
  header[15] = 0xF0; // we provide only the page number
  // $D016 value
  // ($C9 for 80 colums, $C8 for 40 columns for VIC-III but compatibility)
  header[16] = 0xC9;

  // Write header out
  fwrite(header, 128, 1, outfile);

  // $300 @ $FFD3100 for palettes
  block_header[0] = 0x00;
  block_header[1] = 0x31;
  block_header[2] = 0xFD;
  block_header[3] = 0x0F;
  block_header[4] = 0x00;
  block_header[5] = 0x03;
  block_header[6] = 0x00;
  block_header[7] = 0x00;
  fwrite(block_header, 8, 1, outfile);
  unsigned char paletteblock[256];
  for (i = 0; i < 256; i++)
    paletteblock[i] = nyblswap(ts->colours[i].r);
  fwrite(paletteblock, 256, 1, outfile);
  for (i = 0; i < 256; i++)
    paletteblock[i] = nyblswap(ts->colours[i].g);
  fwrite(paletteblock, 256, 1, outfile);
  for (i = 0; i < 256; i++)
    paletteblock[i] = nyblswap(ts->colours[i].b);
  fwrite(paletteblock, 256, 1, outfile);

  // Header for screen RAM
  block_header[0] = 0x00;
  block_header[1] = 0x20;
  block_header[2] = 0x01;
  block_header[3] = 0x00;
  block_header[4] = screen_ram_used & 0xff;
  block_header[5] = ((screen_ram_used) >> 8) & 0xff;
  block_header[6] = ((screen_ram_used) >> 16) & 0xff;
  block_header[7] = ((screen_ram_used) >> 24) & 0xff;
  fwrite(block_header, 8, 1, outfile);
  // Screen RAM
  fwrite(screen_ram, screen_ram_used, 1, outfile);

  // Header for colour RAM
  block_header[0] = 0x00;
  block_header[1] = 0x20;
  block_header[2] = 0xF8;
  block_header[3] = 0x0F;
  block_header[4] = screen_ram_used & 0xff;
  block_header[5] = ((screen_ram_used) >> 8) & 0xff;
  block_header[6] = ((screen_ram_used) >> 16) & 0xff;
  block_header[7] = ((screen_ram_used) >> 24) & 0xff;
  fwrite(block_header, 8, 1, outfile);
  // Colour RAM
  fwrite(colour_ram, screen_ram_used, 1, outfile);

  // Header for ASCII charset
  block_header[0] = 0x00;
  block_header[1] = 0xF0;
  block_header[2] = 0x00;
  block_header[3] = 0x00;
  block_header[4] = 0x00;
  block_header[5] = 0x08;
  block_header[6] = 0x00;
  block_header[7] = 0x00;
  fwrite(block_header, 8, 1, outfile);
  // Colour RAM
  fwrite(ascii_font, 0x0800, 1, outfile);

  if (ts->tile_count) {
    // Header for tiles at $40000
    block_header[0] = 0x00;
    block_header[1] = 0x00;
    block_header[2] = 0x04;
    block_header[3] = 0x00;
    block_header[4] = (ts->tile_count * 64) & 0xff;
    block_header[5] = ((ts->tile_count * 64) >> 8) & 0xff;
    block_header[6] = ((ts->tile_count * 64) >> 16) & 0xff;
    block_header[7] = ((ts->tile_count * 64) >> 24) & 0xff;
    fwrite(block_header, 8, 1, outfile);

    printf("Writing tiles");
    fflush(stdout);
    for (i = 0; i < ts->tile_count; i++) {
      unsigned char tile[64];
      for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
          tile[y * 8 + x] = ts->tiles[i].bytes[x][y];
      fwrite(tile, 64, 1, outfile);
      printf(".");
      fflush(stdout);
    }
    printf("\n");
  }

  /* Build and write URL list
     Build and write hyperlink bounding box list.

     These both live in a shared 30KB segment in BANK 1 from
     $18000 to $1F7FF.

     The URLs can actually go anywhere in BANK 1, but the
     list of bounding boxes has to start at $18000 and grows
     upwards.  $18000 and $18001 is the number of bounding
     boxes.  Then each box consists of 6 bytes:
           (url_addr (16 bits), x1, y1, x2, y2)
     Where the positions are counted in characters, not pixels.

     XXX - We use all 32KB regardless of how much we need, which
     increases file size unnecessarily.
  */
  printf("File contains %d link bounding boxes, pointing to %d URLs.\n", link_count, url_count);
  if (link_count) {
    unsigned char url_data[30 * 1024];
    bzero(url_data, 30 * 1024);
    int url_data_ofs = 30 * 1024;
    for (int i = 0; i < url_count; i++) {
      url_data_ofs -= 1 + strlen(urls[i]);
      if (url_data_ofs < 2) {
        fprintf(stderr, "ERROR: URLs are too big. Split page or use relativel links perhaps?\n");
        exit(-1);
      }
      url_addrs[i] = url_data_ofs;
      bcopy(urls[i], &url_data[url_data_ofs], 1 + strlen(urls[i]));
      printf("'%s' @ $%04x\n", urls[i], url_data_ofs);
    }
    // Number of links
    url_data[0] = link_count >> 0;
    url_data[1] = link_count >> 8;
    // The 6 byte records per link
    int url_box_ofs = 2;
    for (int i = 0; i < link_count; i++) {
      if ((url_box_ofs + 5) >= url_data_ofs) {
        fprintf(
            stderr, "ERROR: URL area overflowed: Reduce number and/or length of URLs, and/or length and number of links.\n");
        exit(-1);
      }
      url_data[url_box_ofs + 0] = url_addrs[url_boxes[i].url_id] >> 0;
      url_data[url_box_ofs + 1] = url_addrs[url_boxes[i].url_id] >> 8;
      url_data[url_box_ofs + 2] = url_boxes[i].x1;
      url_data[url_box_ofs + 3] = url_boxes[i].y1;
      url_data[url_box_ofs + 4] = url_boxes[i].x2;
      url_data[url_box_ofs + 5] = url_boxes[i].y2;
      url_box_ofs += 6;
    }
    // Header for URL data at $18000
    block_header[0] = 0x00;
    block_header[1] = 0x80;
    block_header[2] = 0x01;
    block_header[3] = 0x00;
    block_header[4] = 0x00;
    block_header[5] = 0x78;
    block_header[6] = 0x00;
    block_header[7] = 0x00;
    fwrite(block_header, 8, 1, outfile);
    fwrite(url_data, 30 * 1024, 1, outfile);
  }

  // Write end of file marker
  bzero(block_header, 8);
  fwrite(block_header, 8, 1, outfile);

  // Write 1500 bytes of nulls at the end, to work around WeeIP data-on-close
  // bug.
  // XXX - Actually, now that the MEGA65 can have 32 ethernet frames in the
  // queue, if we see a FIN early, WeeIP's idea of "I saw a FIN, regardless of
  // the sequence number, I'm going to disconnect RIGHT NOW!" means we need even
  // more empty space here, until I fix that bug.
  bzero(screen_ram, 1500);
  for (int i = 0; i < 32; i++)
    fwrite(screen_ram, 1500, 1, outfile);

  if (outfile != NULL) {
    fclose(outfile);
    outfile = NULL;
  }

  return 0;
}

/* ============================================================= */

int main(int argc, char **argv)
{
  if (argc != 3) {
    fprintf(stderr, "Usage: md2h65 <input.md> <output.h65>\n");
    exit(-1);
  }

  // Default to all fonts using MEGA65 ASCII font
  for (int i = 0; i < MAX_TYPEFACES; i++)
    type_faces[i] = NULL;

  // Setup TTF handling library
  error = FT_Init_FreeType(&library); /* initialize library */

  current_font = FONT_PARAGRAPH;

  // Allow upto 128KB of tiles (for both graphics and font glyphs)
  ts = new_tileset(128 * 1024 / 64);
  palette_c64_init(ts);

  do_pass(argv, ts);
  pass_num = 2;
  if (second_pass_required) {
    fprintf(stderr, "Quantising colours for 2nd pass.\n");
    quantise_colours(ts);
    fprintf(stderr, "Running 2nd pass.\n");
    do_pass(argv, ts);
  }
}
