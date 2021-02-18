/*
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

char* vhdl_prefix = "library IEEE;\n"
                    "use IEEE.STD_LOGIC_1164.ALL;\n"
                    "use ieee.numeric_std.all;\n"
                    "use work.debugtools.all;\n"
                    "\n"
                    "--\n"
                    "entity charrom is\n"
                    "port (Clk : in std_logic;\n"
                    "        address : in integer range 0 to 4095;\n"
                    "        -- chip select, active low       \n"
                    "        cs : in std_logic;\n"
                    "        data_o : out std_logic_vector(7 downto 0);\n"
                    "\n"
                    "        writeclk : in std_logic;\n"
                    "        -- Yes, we do have a write enable, because we allow modification of ROMs\n"
                    "        -- in the running machine, unless purposely disabled.  This gives us\n"
                    "        -- something like the WOM that the Amiga had.\n"
                    "        writecs : in std_logic;\n"
                    "        we : in std_logic;\n"
                    "        writeaddress : in unsigned(11 downto 0);\n"
                    "        data_i : in std_logic_vector(7 downto 0)\n"
                    "      );\n"
                    "end charrom;\n"
                    "\n"
                    "architecture Behavioral of charrom is\n"
                    "\n"
                    "-- 4K x 8bit pre-initialised RAM for character ROM\n"
                    "\n"
                    "type ram_t is array (0 to 4095) of std_logic_vector(7 downto 0);\n"
                    "signal ram : ram_t := (\n"
                    "\n";

char* vhdl_suffix = ");\n"
                    "\n"
                    "begin\n"
                    "\n"
                    "--process for read and write operation.\n"
                    "PROCESS(Clk,ram,writeclk)\n"
                    "BEGIN\n"
                    "\n"
                    "  if(rising_edge(writeClk)) then \n"
                    "    if writecs='1' then\n"
                    "      if(we='1') then\n"
                    "        ram(to_integer(writeaddress)) <= data_i;\n"
                    "      end if;\n"
                    "    end if;\n"
                    "    data_o <= ram(address);\n"
                    "  end if;\n"
                    "END PROCESS;\n"
                    "\n"
                    "end Behavioral;\n";

/* ============================================================= */

int x, y;

int width, height;
png_byte color_type;
png_byte bit_depth;

png_structp png_ptr;
png_infop info_ptr;
int number_of_passes;
png_bytep* row_pointers;

FILE* infile;
FILE* outfile;

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

void read_png_file(char* file_name)
{
  unsigned char header[8]; // 8 is the maximum size that can be checked

  /* open file and test for it being a png */
  infile = fopen(file_name, "rb");
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

  printf("Input-file is read and now closed\n");
}

/* ============================================================= */

struct rgb {
  int r;
  int g;
  int b;
};

struct rgb palette[2560];
int palette_first = 16;
int palette_index = 16; // only use upper half of palette

int palette_lookup(int r, int g, int b)
{
  int i;

  // Do we know this colour already?
  for (i = palette_first; i < palette_index; i++) {
    if (r == palette[i].r && g == palette[i].g && b == palette[i].b) {
      return i;
    }
  }

  // new colour
  if (palette_index > 255) {
    fprintf(stderr, "Too many colours in image: Must be < 256, now up to %d\n", palette_index);
  }
  if (palette_index > 2559)
    exit(-1);

  // allocate it
  palette[palette_index].r = r;
  palette[palette_index].g = g;
  palette[palette_index].b = b;
  return palette_index++;
}

unsigned char nyblswap(unsigned char in)
{
  return ((in & 0xf) << 4) + ((in & 0xf0) >> 4);
}

void process_file(char* outputfilename)
{
  int multiplier = -1;
  if (png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_RGB)
    multiplier = 3;

  if (png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_RGBA)
    multiplier = 4;

  if (multiplier == -1) {
    fprintf(stderr, "Could not convert file to RGB or RGBA\n");
  }

  outfile = fopen(outputfilename, "wb");
  if (outfile == NULL) {
    // could not open output file, so close all and exit
    if (infile != NULL) {
      fclose(infile);
      infile = NULL;
    }
    abort_("[process_file] File %s could not be opened for writing", outputfilename);
  }

  /* ============================ */

  // hi-res image preparation mode

  // int bytes=0;
  int total = 0;

  int tiles[8000][8][8];
  int tile_count = 0;

  int this_tile[8][8];

  if (width > 720) {
    fprintf(stderr, "ERROR: Image must not be bigger than 720x480, ideally less than 640x400\n");
    exit(-1);
  }

  for (y = 0; y < height; y += 8) {

    // Process each image stripe

#define MAX_COLOURS 256
    int colour_count = 0;
    int colours[MAX_COLOURS];

    int subst = 0;

    for (x = 0; x < width; x += 8) {
      int yy, xx;
      int i;

      //      printf("[%d,%d]\n",x,y);

      total++;

      for (yy = y; yy < y + 8; yy++) {
        png_byte* row = row_pointers[yy];
        for (xx = x; xx < x + 8; xx++) {
          png_byte* ptr = &(row[xx * multiplier]);
          int r, g, b;
          if (ptr && (yy < height) && (xx < width)) {
            r = ptr[0];
            g = ptr[1];
            b = ptr[2];
          }
          else {
            r = 0;
            g = 0;
            b = 0;
          }
          int best_colour = -1;
          int best_distance = 0x1000000;
          int c = r + 256 * g + 65536 * b;
          for (i = 0; i < colour_count; i++) {
            if (c == colours[i])
              break;
            else {
              int red_delta = abs(r - (colours[i] & 0xff));
              int green_delta = abs(b - ((colours[i] >> 8) & 0xff));
              int blue_delta = abs(b - ((colours[i] >> 16) & 0xff));
              int this_distance = (red_delta * red_delta) + (green_delta * green_delta) + (blue_delta * blue_delta);
              if (this_distance < best_distance) {
                best_colour = i;
                best_distance = this_distance;
              }
            }
          }
          if (i == colour_count) {
            if (colour_count < MAX_COLOURS) {
              // Allocate a new colour
              colours[colour_count++] = c;
              this_tile[yy - y][xx - x] = c;
            }
            else {
              // Too many colours. Store in closest
              subst++;
              this_tile[yy - y][xx - x] = best_colour;
              //	      printf("[#%06x -> #%06x %d]\n",c,colours[best_colour],best_distance);
            }
          }
          else {
            this_tile[yy - y][xx - x] = i;
          }
        }
      }

      for (i = 0; i < tile_count; i++) {
        int dud = 0;
        int xx, yy;
        for (xx = 0; xx < 8; xx++)
          for (yy = 0; yy < 8; yy++) {
            if (this_tile[yy][xx] != tiles[i][yy][xx])
              dud = 1;
          }
        if (!dud)
          break;
      }
      if (i == tile_count) {
        int xx, yy;
        for (xx = 0; xx < 8; xx++)
          for (yy = 0; yy < 8; yy++) {
            tiles[tile_count][yy][xx] = this_tile[yy][xx];
          }
        printf(".[%d]", tile_count);
        fflush(stdout);
        tile_count++;
        if (tile_count >= 8000) {
          fprintf(stderr, "Too many tiles\n");
          if (outfile != NULL) {
            fclose(outfile);
            outfile = NULL;
          }
          exit(-1);
        }
      }
    }
    printf("\n%d unique tiles, %d colour substitutions\n", tile_count, subst);

    // Write out palette
  }
}

/* ============================================================= */

int main(int argc, char** argv)
{
  if (argc != 3) {
    fprintf(stderr, "Usage: program_name <file_in> <file_out>\n");
    exit(-1);
  }

  printf("Reading %s\n", argv[1]);
  read_png_file(argv[1]);

  printf("Processing with output=%s\n", argv[2]);
  process_file(argv[2]);

  printf("done\n");

  if (infile != NULL) {
    fclose(infile);
    infile = NULL;
  }

  if (outfile != NULL) {
    fclose(outfile);
    outfile = NULL;
  }

  return 0;
}

/* ============================================================= */
