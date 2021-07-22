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
 * Copyright 2015-2018 Paul Gardner-Stephen.
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

#define PNG_DEBUG 3
#include <png.h>

/* ============================================================= */

/* ============================================================= */

int x, y;

int width, height;
png_byte color_type;
png_byte bit_depth;

png_structp png_ptr;
png_infop info_ptr;
int number_of_passes;
png_bytep* row_pointers;
int multiplier;

/* ============================================================= */

void abort_(const char* s, ...)
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

struct tile_set {
  struct tile* tiles;
  int tile_count;
  int max_tiles;

  // Palette
  struct rgb colours[256];
  int colour_count;

  struct tile_set* next;
};

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
}

int palette_lookup(struct tile_set* ts, int r, int g, int b)
{
  int i;

  // Do we know this colour already?
  for (i = 0; i < ts->colour_count; i++) {
    if (r == ts->colours[i].r && g == ts->colours[i].g && b == ts->colours[i].b) {
      // It's a colour we have seen before, so return the index
      return i;
    }
  }

  // new colour, check if palette has space
  if (ts->colour_count > 255) {
    fprintf(stderr, "Too many colours in image: Must be <= %d\n", 256);
    exit(-1);
  }

  // allocate the new colour
  ts->colours[ts->colour_count].r = r;
  ts->colours[ts->colour_count].g = g;
  ts->colours[ts->colour_count].b = b;
  return ts->colour_count++;
}

unsigned char nyblswap(unsigned char in)
{
  return ((in & 0xf) << 4) + ((in & 0xf0) >> 4);
}

struct tile_set* new_tileset(int max_tiles)
{
  struct tile_set* ts = calloc(sizeof(struct tile_set), 1);
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
  struct tile_set* tiles;
  unsigned char width, height;
  unsigned char** screen_rows;
  unsigned char** colourram_rows;

  struct screen* next;
};

struct screen* new_screen(int id, struct tile_set* tiles, int width, int height)
{
  struct screen* s = calloc(sizeof(struct screen), 1);
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
  s->screen_rows = calloc(sizeof(unsigned char*), height);
  s->colourram_rows = calloc(sizeof(unsigned char*), height);
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

int tile_lookup(struct tile_set* ts, struct tile* t)
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

struct screen* png_to_screen(int id, struct tile_set* ts)
{
  int x, y;

  if (height % 8 || width % 8) {
    fprintf(stderr, "ERROR: PNG image dimensions must be a multiple of 8.\n");
    exit(-3);
  }

  struct screen* s = new_screen(id, ts, width / 8, height / 8);

  for (y = 0; y < height; y += 8)
    for (x = 0; x < width; x += 8) {
      int transparent_tile = 1;
      struct tile t;
      for (int yy = 0; yy < 8; yy++) {
        png_byte* row = row_pointers[yy + y];
        for (int xx = 0; xx < 8; xx++) {
          png_byte* ptr = &(row[(xx + x) * multiplier]);
          int r, g, b, a, c;
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
          t.bytes[xx][yy] = c;
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
        s->screen_rows[y / 8][x / 8 * 2 + 0] = tile_number & 0xff;
        s->screen_rows[y / 8][x / 8 * 2 + 1] = (tile_number >> 8) & 0xff;
        s->colourram_rows[y / 8][x / 8 * 2 + 0] = 0x00;
        s->colourram_rows[y / 8][x / 8 * 2 + 1] = 0xff; // FG colour
      }
    }
  return s;
}

void read_png_file(char* file_name)
{
  unsigned char header[8]; // 8 is the maximum size that can be checked

  /* open file and test for it being a png */
  FILE* infile = fopen(file_name, "rb");
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

  row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
  for (y = 0; y < height; y++)
    row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png_ptr, info_ptr));

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

#define MAX_COLOURRAM_SIZE 32768

int i, x = 0, y = 0;
int colour=14; // C64 light blue by default
unsigned char text_colour = 14;
unsigned char text_colour_saved = 14;
unsigned char indent = 0;
unsigned char attributes = 0;
unsigned char screen_ram[MAX_COLOURRAM_SIZE];
unsigned char colour_ram[MAX_COLOURRAM_SIZE];
unsigned int screen_ram_used=0;
unsigned int line_count=0;
unsigned int in_paragraph=0;

void emit_paragraph(void)
{
  // End a previous paragraph, if one was started.
  if (in_paragraph) {
    if (x) y+=2; else y++; x=0;
    in_paragraph=0;
    indent=0;
  }
}

unsigned char ascii_to_screen_code(unsigned char c)
{
  // XXX Fold lower to upper case for now
  if (c>='a'&&c<='z') c=c^0x20;
  return c;
}

void emit_word(char *word) {
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

  int start=0;
  int end=strlen(word);

  in_paragraph=1;
  
  if (word[0]=='*'&&word[1]=='*') {
    text_colour_saved=text_colour;
    text_colour=7; // bold = yellow
    start=2;
  }
  if (word[end-2]=='*'&&word[end-1]=='*') {
    text_colour=text_colour_saved; // end of bold
    end-=2;
  }

  int len=end-start;
  if ((80-x)<len) {
    y++; x=indent;
  }
  for(int xx=start;xx<end;xx++) {
    if (x>=80) {
      x=0; y++;
    }
    if (y*160+x*2<MAX_COLOURRAM_SIZE) {
      screen_ram[y*160+x*2+0]=ascii_to_screen_code(word[xx]);
      colour_ram[y*160+x*2+0]=0;
      colour_ram[y*160+x*2+1]=text_colour+attributes;
    }
    x++;
  }

}

void emit_text(char *text)
{
  // Emit the text
  char word[1024];
  int word_len=0;
  // Emit word at a time, so that we can find special token
  for(int i=0;text[i];i++) {
    if (text[i]==' '||text[i]=='\t'||text[i]=='\n'||text[i]=='\r') {
      word[word_len]=0;
      if (word_len) emit_word(word);
      word_len=0;
    } else word[word_len++]=text[i];
   
    if (text[i]==' '||text[i]=='\t') {
      // Emit a space after the word if required.
      x++;
      if (x>=80) { y++; x=indent; }
    }
    
  }
}

int main(int argc, char** argv)
{

  unsigned char block_header[8];

  // Initialise screen and colour RAM
  bzero(colour_ram, sizeof(colour_ram));
  for (i = 0; i < MAX_COLOURRAM_SIZE; i += 2) {
    screen_ram[i] = ' ';
    screen_ram[i + 1] = 0;
  }

  if (argc < 3) {
    fprintf(stderr, "Usage: md2h65 <input.md> <output.h65>\n");
    exit(-1);
  }

  FILE* infile = fopen(argv[1], "r");
  if (!infile) {
    perror("Could not open input file");
    exit(-3);
  }

  FILE* outfile = fopen(argv[2], "wb");
  if (!outfile) {
    perror("Could not open output file");
    exit(-3);
  }

  // Allow upto 128KB of tiles
  struct tile_set* ts = new_tileset(128 * 1024 / 64);
  palette_c64_init(ts);

  // Read the .md file
  char line[1024];
  line[0] = 0;
  fgets(line, 1024, infile);
  while (line[0]) {
    if (line[0]=='#') {
      emit_paragraph();
      // Heading
      text_colour=1; // white text for headings
      attributes=0x80; // underline for headings
      emit_text(&line[2]);
      text_colour=14;
      emit_paragraph();
    } else if (line[0]=='\n'||line[0]=='\r') {
      // Blank line = paragraph break
      emit_paragraph();
    } else {      
      // Normal text
      attributes=0; // cancel underline from a heading
      emit_text(&line[0]);      
    }
    line[0] = 0;
    fgets(line, 1024, infile);
  }

  screen_ram_used=y*160+x*2;
  
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
  // $D054 value: Enable FCM for chars >$fF
  header[7] = 0x05;
  // Number of chars per line to display
  header[8] = 80;
  // V400/H640 flags
  header[9] = 0xE8; // V400, H640, VIC-III attributes
  // Number of screen lines
  header[10] = (MAX_COLOURRAM_SIZE / 80) & 0xff;
  header[11] = (MAX_COLOURRAM_SIZE / 80) >> 8;

  // Screen colours
  header[12] = 0x06;
  header[13] = 0x06;
  header[14] = 0x01;

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
  block_header[1] = 0x00;
  block_header[2] = 0xF8;
  block_header[3] = 0x0F;
  block_header[4] = screen_ram_used & 0xff;
  block_header[5] = ((screen_ram_used) >> 8) & 0xff;
  block_header[6] = ((screen_ram_used) >> 16) & 0xff;
  block_header[7] = ((screen_ram_used) >> 24) & 0xff;
  fwrite(block_header, 8, 1, outfile);
  // Colour RAM
  fwrite(colour_ram, screen_ram_used, 1, outfile);
  
  // Header for tiles at $40000
  block_header[0] = 0x00;
  block_header[1] = 0x00;
  block_header[2] = 0x00;
  block_header[3] = 0x04;
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

  // Write end of file marker
  bzero(block_header, 8);
  fwrite(block_header, 8, 1, outfile);

  // Write 1500 bytes of nulls at the end, to work around WeeIP data-on-close
  // bug
  bzero(screen_ram,1500);
  fwrite(screen_ram,1500,1,outfile);
  
  if (outfile != NULL) {
    fclose(outfile);
    outfile = NULL;
  }

  return 0;
}

/* ============================================================= */
