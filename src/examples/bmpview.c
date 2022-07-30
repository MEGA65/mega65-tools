#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>

char msg[64 + 1];

unsigned short i;
short x, y;
unsigned char a, b, c, d, r, g, b, p;

void graphics_mode(void)
{
  /* Setup for 640x400 256 colour.
     If the image is smaller, we can manually set zoom etc for 320x200 with H320, V200
  */

  // 16-bit text mode, full-colour text for high chars
  POKE(0xD054, 0x05);
  // H640, fast CPU, V400
  POKE(0xD031, 0xC8);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016, 0xC9);
  // 640x400 16bits per char, 8 pixels wide per char
  // = 640/16 x 8 bits = 160 bytes per row
  POKE(0xD058, 160);
  POKE(0xD059, 160 / 256);
  // Draw 80 chars per row
  POKE(0xD05E, 80);
  // Put 80x50x2 = 8KB screen at $E000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xE0);
  POKE(0xD062, 0x00);

  /*
    640x400 256 colour requires ~250KB RAM.
    We have 128MB at $40000-$5FFFF that can be used.
    Then we have to crib the rest from different places.
    $30000-$3FFFF *could* be used if we stick to C64 mode
    and disable "ROM write protect" via the hypervisor.
    $12000-$1F7FF (54KB) can be safely used.
    $0A000-$0DFFF (16KB) can also be safely used.

    That gives us 128KB + 64KB + 54KB + 16KB = 262KB.

    This is all a bit of a pain for the pixel plotting routine,
    though, as we have to work out which range the pixel belongs in.
    Also, BMPs are scanned horizontally, so we should ideally lay out
    the screen that way, so that the loading can go faster, and use
    short DMA jobs where it makese sense.

  */

  // Layout screen so that graphics data comes from $40000 -- $5FFFF
  // for the first half, and then

  i = 0x40000 / 0x40;
  for (b = 0; b < 50; b++) {
    for (a = 0; a < 80; a++) {
      POKE(0xE000 + b * 80 * 2 + a * 2 + 0, i & 0xff);
      POKE(0xE000 + b * 80 * 2 + a * 2 + 1, i >> 8);
      i++;
      if (i == (0x60000 / 0x40))
        i = 0x30000 / 0x40;
      if (i == (0x40000 / 0x40))
        i = 0x12000 / 0x40;
      if (i == (0x1F800 / 0x40))
        i = 0x0A000 / 0x40;
    }
  }

  // Clear colour RAM
  lfill(0xff80000L, 0x00, 80 * 50 * 2);

  POKE(0xD020, 0);
  POKE(0xD021, 0);
}

void graphics_clear_screen(void)
{
  lfill(0x40000, 0x00, 0x8000);
  lfill(0x48000, 0x00, 0x8000);
  lfill(0x50000, 0x00, 0x8000);
  lfill(0x58000, 0x00, 0x8000);
  lfill(0x30000, 0x00, 0x8000);
  lfill(0x38000, 0x00, 0x8000);
  lfill(0x12000, 0x00, 0x8000);
  lfill(0x1A000, 0x00, 0x5800);
  lfill(0x0A000, 0x00, 0x4000);
}

unsigned char char_code;

unsigned short char_addr;
void print_text80(unsigned char x, unsigned char y, unsigned char colour, char *msg)
{
  char_addr = 0xE000 + x + y * 80 * 2;
  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    else if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    else if (*msg >= 0x60 && *msg <= 0x7A)
      char_code = *msg - 0x20;
    POKE(char_addr + 0, char_code);
    POKE(char_addr + 1, 0);
    lpoke(0xff80000L - 0xE000 + char_addr, 0);
    lpoke(0xff80000L - 0xE000 + char_addr + 1, colour);
    msg++;
    char_addr += 2;
  }
}

unsigned long pixel_addr, final_addr;

void plot_pixel(unsigned short x, unsigned short y, unsigned char colour)
{
  pixel_addr = (x & 7) + (x >> 3) * 0x40L + ((y & 7) << 3) + (y >> 3) * 80 * 0x40L;

  if (pixel_addr < 0x20000)
    final_addr = 0x40000L + pixel_addr;
  else if (pixel_addr < 0x30000)
    final_addr = 0x30000L - 0x20000L + pixel_addr;
  else if (pixel_addr < 0x3D800)
    final_addr = 0x12000L - 0x30000L + pixel_addr;
  else if (pixel_addr < 0x40000)
    final_addr = 0x0A000L - 0x3D800L + pixel_addr;
  else
    final_addr = 0x60000;
  lpoke(final_addr, colour);

  //  snprintf(msg,80,"%d,%d = $%05lx @ $%05lx",x,y,pixel_addr,final_addr);
  //  print_text80(0,2,0xff,msg);
}

void wait_frames(unsigned char n)
{
  while (n) {
    while (PEEK(0xD012) != 0x80)
      continue;
    while (PEEK(0xD012) == 0x80)
      continue;
    n--;
  }
}

unsigned char fd;
char filename[80 + 1] = "r3.bmp";
unsigned int width, height, pixel_data;
unsigned char buffer[512];
unsigned int buf_ofs;
unsigned long file_ofs;
unsigned int row_size;
unsigned char data_format, rle_count, rle_val;

unsigned char px[8];

unsigned char next_byte_from_file(void)
{
  if (buf_ofs >= 512) {
    read512(buffer);
    buf_ofs = 0;
  }
  file_ofs++;
  return buffer[buf_ofs++];
}

void load_bmpfile(void)
{
  // Load a MOD file for testing
  closeall();
  fd = open(filename);
  if (fd == 0xff) {
    print_text80(0, 0, 2, "Could not read BMP file                                             ");
    return;
  }

  graphics_mode();
  graphics_clear_screen();

  // Get first 512 bytes of the file
  if (read512(buffer) != 512)
    return;

  // Get image dimensions
  width = buffer[0x13];
  width = width << 8;
  width += buffer[0x12];
  height = buffer[0x17];
  height = height << 8;
  height += buffer[0x16];
  if (width > 640 || height > 400)
    return;
  pixel_data = buffer[0x0B];
  pixel_data = pixel_data << 8;
  pixel_data += buffer[0x0A];
  //  snprintf(msg,80,"Size=%dx%d. pixel_data @ $%x",width,height,pixel_data);
  //  print_text80(0,1,7,msg);

  data_format = buffer[0x1e];

  row_size = width;
  if (!data_format) {
    // BMP pixel rows are multiples of 4 bytes if not using RLE
    while (row_size & 3)
      row_size++;
  }
  // No set row size for RLE8
  if (data_format == 1)
    row_size = 9999;

  // Load colour table
  buf_ofs = 0x8a;
  file_ofs = 0x8a;
  for (i = 0; i < 256; i++) {
    r = next_byte_from_file();
    g = next_byte_from_file();
    b = next_byte_from_file();
    next_byte_from_file(); // alpha
    POKE(0xD100 + i, r >> 4 + (r << 4));
    POKE(0xD200 + i, g >> 4 + (g << 4));
    POKE(0xD300 + i, b >> 4 + (b << 4));
  }

  while (file_ofs < pixel_data)
    next_byte_from_file();

  y = height - 1;
  while (y >= 0) {
    for (x = 0; x < row_size; x++) {
      switch (data_format) {
      case 0:
        p = next_byte_from_file();
        break;
      case 1:
        // Its RLE8
        if (!rle_count) {
          rle_count = next_byte_from_file();
          rle_val = next_byte_from_file();
          if (!rle_count) {
            // End of row
            switch (rle_val) {
            case 1:
              y = -1;
              break; // end of image
            case 0:
              // Flush any remaining pixels
              if (x & 7) {
                pixel_addr = 0 + (x >> 3) * 0x40L + ((y & 7) << 3) + (y >> 3) * 80 * 0x40L;

                if (pixel_addr < 0x20000)
                  final_addr = 0x40000L + pixel_addr;
                else if (pixel_addr < 0x30000)
                  final_addr = 0x30000L - 0x20000L + pixel_addr;
                else if (pixel_addr < 0x3D800)
                  final_addr = 0x12000L - 0x30000L + pixel_addr;
                else if (pixel_addr < 0x40000)
                  final_addr = 0x0A000L - 0x3D800L + pixel_addr;
                else
                  final_addr = 0x60000;

                lcopy((unsigned long)px, final_addr, x & 7);
              }

              x = row_size;
              break; // end of line
            default:
              // delta position
              while (1) {
                POKE(0xD020, PEEK(0xD012));
              }
            }
          }
        }
        p = rle_val;
        if (rle_count)
          rle_count--;
        break;
      }

      if (x < width && y >= 0) {
        px[x & 7] = p;
        if ((x & 7) == 7) {
          pixel_addr = 0 + (x >> 3) * 0x40L + ((y & 7) << 3) + (y >> 3) * 80 * 0x40L;

          if (pixel_addr < 0x20000)
            final_addr = 0x40000L + pixel_addr;
          else if (pixel_addr < 0x30000)
            final_addr = 0x30000L - 0x20000L + pixel_addr;
          else if (pixel_addr < 0x3D800)
            final_addr = 0x12000L - 0x30000L + pixel_addr;
          else if (pixel_addr < 0x40000)
            final_addr = 0x0A000L - 0x3D800L + pixel_addr;
          else
            final_addr = 0x60000;

          lcopy((unsigned long)px, final_addr, 8);
        }
#if 0
	if (!(x&7)) plot_pixel(x,y,p);
	else {
	  final_addr++; lpoke(final_addr,p);
	}
#endif
      }
    }

    y--;
  }

  //  while((count=read512(buffer))>0) {
  //    if (count>512) break;
  //    if (count<512) break;
}

void read_filename(void)
{
  unsigned char len = strlen(filename);
  print_text80(0, 0, 1, "Enter name of BMP file to load:");
  while (1) {
    print_text80(0, 1, 7, "                    ");
    print_text80(0, 1, 7, filename);
    // Show block cursor
    lpoke(0xff80000 + 80 * 2 + strlen(filename), 0x21);

    while (!PEEK(0xD610))
      continue;

    if ((PEEK(0xD610) >= 0x20) && (PEEK(0xD610) <= 0x7A)) {
      if (len < 16) {
        filename[len] = PEEK(0xD610);
        if ((filename[len] >= 0x61) && (filename[len] <= 0x7A))
          filename[len] -= 0x20;
        filename[++len] = 0;
      }
    }
    else if (PEEK(0xD610) == 0x14) {
      if (len) {
        len--;
        filename[len] = 0;
      }
    }
    else if (PEEK(0xD610) == 0x0d) {
      POKE(0xD610, 0x00);
      return;
    }
    POKE(0xD610, 0x00);
  }
}

void main(void)
{
  unsigned char ch;
  unsigned char playing = 0;

  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // Make sure ROM area is writeable
  ch = lpeek(0x30000);
  lpoke(0x30000, ch + 1);
  if (lpeek(0x30000) == ch)
    toggle_rom_write_protect();
  lpoke(0x30000, ch);

  while (PEEK(0xD610))
    POKE(0xD610, 0);

  POKE(0xD020, 0);
  POKE(0xD021, 0);

  graphics_clear_screen();
  graphics_mode();
  read_filename();
  load_bmpfile();

  while (1) { }
}
