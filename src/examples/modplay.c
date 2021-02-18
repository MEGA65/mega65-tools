#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>

#define MOD_TEST
#undef SINE_TEST
#undef DIRECT_TEST

char msg[64 + 1];

unsigned short song_offset = 1084;

unsigned short i;
unsigned char a, b, c, d;

#define NUM_SIGS 4
char mod31_sigs[NUM_SIGS][4] = { { 0x4D, 0x2e, 0x4b, 0x2e }, { 0x4D, 0x21, 0x4b, 0x21 }, { 0x46, 0x4c, 0x54, 0x34 },
  { 0x46, 0x4c, 0x54, 0x38 } };

/*
  Our sample clock is in 1/2^24ths of a 40.5MHz clock.
  So a value of 0xFFFFFF ~= 40MHz sample rate (impossible in practice)
  Target is to have 40.5MHz / Amiga Paul clock.
  But we actually aim for 2^16x that, so that we can scale it back down
  from the high-precision multiplier output.
  So MOD frequency 1 should be set to:
  FREQ/(PAL RASTERS PER SECOND) * CPU_FREQUENCY/AMIGA_PAULA_CLOCK * 2^24
  Everything else is a constant, so we can say:
  SAMPLE_CLOCK = FREQ * CPU_FREQUENCY/AMIGA_PAULA_CLOCK * 2^24 / PAL_RASTERS_PER_SECOND

  Well, that was all wrong, because we were talking about frequencies rather than period.
  MODs use period.

  Easy way: Period = 214 = 16574.27 Hz sample rate
  Time base gets added 40.5M times per second.
  16574.27*2^24 / 40.5M = 6866 for period = 214
  If period is higher, then this number will be lower
  So it should be 6866*214 / PERIOD
  = 1469038 / PERIOD

  If we calculate N=2^24/PERIOD, then
  what we want will be (1469038 x N ) >> 24
  But our multiplier is only 23x18 bits
  So N=2^16/PERIOD should fit, and still allow us
  to just shift the result right by 2 bytes

 */
#define CPU_FREQUENCY 40500000
#define AMIGA_PAULA_CLOCK (70937892 / 20)
#define RASTERS_PER_SECOND (313 * 50)

// long sample_rate_divisor=CPU_FREQUENCY*0x100000/(AMIGA_PAULA_CLOCK * RASTERS_PER_SECOND);
// 2^24×(16574÷40500000)/214 Hz nominal frequency
// /2 = fudge factor?
long sample_rate_divisor = 1469038;
#define RASTERS_PER_MINUTE (RASTERS_PER_SECOND * 60)
#define BEATS_PER_MINUTE 125
#define ROWS_PER_BEAT 8
unsigned short tempo = RASTERS_PER_MINUTE / BEATS_PER_MINUTE / ROWS_PER_BEAT;

void graphics_clear_screen(void)
{
  lfill(0x40000L, 0, 32768L);
  lfill(0x48000L, 0, 32768L);
}

void graphics_clear_double_buffer(void)
{
  lfill(0x50000L, 0, 32768L);
  lfill(0x58000L, 0, 32768L);
}

void h640_text_mode(void)
{
  // lower case
  POKE(0xD018, 0x16);

  // Normal text mode
  POKE(0xD054, 0x00);
  // H640, fast CPU, extended attributes
  POKE(0xD031, 0xE0);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016, 0xC9);
  // 640x200 16bits per char, 16 pixels wide per char
  // = 640/16 x 16 bits = 80 bytes per row
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  // Draw 80 chars per row
  POKE(0xD05E, 80);
  // Put 2KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  lfill(0xc000, 0x20, 2000);
  // Clear colour RAM, while setting all chars to 4-bits per pixel
  lfill(0xff80000L, 0x0E, 2000);
}

void graphics_mode(void)
{
  // 16-bit text mode, full-colour text for high chars
  POKE(0xD054, 0x05);
  // H640, fast CPU
  POKE(0xD031, 0xC0);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016, 0xC9);
  // 640x200 16bits per char, 16 pixels wide per char
  // = 640/16 x 16 bits = 80 bytes per row
  POKE(0xD058, 80);
  POKE(0xD059, 80 / 256);
  // Draw 40 (double-wide) chars per row
  POKE(0xD05E, 40);
  // Put 2KB screen at $C000
  POKE(0xD060, 0x00);
  POKE(0xD061, 0xc0);
  POKE(0xD062, 0x00);

  // Layout screen so that graphics data comes from $40000 -- $4FFFF

  i = 0x40000 / 0x40;
  for (a = 0; a < 40; a++)
    for (b = 0; b < 25; b++) {
      POKE(0xC000 + b * 80 + a * 2 + 0, i & 0xff);
      POKE(0xC000 + b * 80 + a * 2 + 1, i >> 8);

      i++;
    }

  // Clear colour RAM, while setting all chars to 4-bits per pixel
  for (i = 0; i < 2000; i) {
    lpoke(0xff80000L + 0 + i, 0x0E);
    lpoke(0xff80000L + 1 + i, 0x00);
  }
  POKE(0xD020, 0);
  POKE(0xD021, 0);

  graphics_clear_screen();
}

unsigned short pixel_addr;
unsigned char pixel_temp;
void plot_pixel(unsigned short x, unsigned char y, unsigned char colour)
{
  pixel_addr = ((x & 0xf) >> 1) + 64 * 25 * (x >> 4);
  pixel_addr += y << 3;
  pixel_temp = lpeek(0x50000L + pixel_addr);
  if (x & 1) {
    pixel_temp &= 0x0f;
    pixel_temp |= colour << 4;
  }
  else {
    pixel_temp &= 0xf0;
    pixel_temp |= colour & 0xf;
  }
  lpoke(0x50000L + pixel_addr, pixel_temp);
}

unsigned char char_code;
void print_text(unsigned char x, unsigned char y, unsigned char colour, char* msg)
{
  pixel_addr = 0xC000 + x * 2 + y * 80;
  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    POKE(pixel_addr + 0, char_code);
    POKE(pixel_addr + 1, 0);
    lpoke(0xff80000 - 0xc000 + pixel_addr + 0, 0x00);
    lpoke(0xff80000 - 0xc000 + pixel_addr + 1, colour);
    msg++;
    pixel_addr += 2;
  }
}

void print_text80(unsigned char x, unsigned char y, unsigned char colour, char* msg)
{
  pixel_addr = 0xC000 + x + y * 80;
  while (*msg) {
    char_code = *msg;
    if (*msg >= 0xc0 && *msg <= 0xe0)
      char_code = *msg - 0x80;
    else if (*msg >= 0x40 && *msg <= 0x60)
      char_code = *msg - 0x40;
    else if (*msg >= 0x60 && *msg <= 0x7A)
      char_code = *msg - 0x20;
    POKE(pixel_addr + 0, char_code);
    lpoke(0xff80000L - 0xc000 + pixel_addr, colour);
    msg++;
    pixel_addr += 1;
  }
}

void activate_double_buffer(void)
{
  lcopy(0x50000, 0x40000, 0x8000);
  lcopy(0x58000, 0x48000, 0x8000);
}

unsigned char histo_bins[640];
char peak_msg[40 + 1];
unsigned char random_target = 40;
unsigned char last_random_target = 40;
unsigned int random_seek_count = 0;
unsigned char request_track = 40;
unsigned char read_sectors[41] = { 0 };
unsigned char last_track_seen = 255;
unsigned int histo_samples = 0;

unsigned char fd;
int count;
unsigned char buffer[512];

unsigned long load_addr;

unsigned char mod_name[23];

#define MAX_INSTRUMENTS 32
unsigned short instrument_lengths[MAX_INSTRUMENTS];
unsigned short instrument_loopstart[MAX_INSTRUMENTS];
unsigned short instrument_looplen[MAX_INSTRUMENTS];
unsigned char instrument_finetune[MAX_INSTRUMENTS];
unsigned long instrument_addr[MAX_INSTRUMENTS];
unsigned char instrument_vol[MAX_INSTRUMENTS];

unsigned char song_length;
unsigned char song_loop_point;
unsigned char song_pattern_list[128];
unsigned char max_pattern = 0;
unsigned long sample_data_start = 0x40000;

unsigned long time_base = 0;

unsigned char sin_table[32] = {
  //  128,177,218,246,255,246,218,177,
  //  128,79,38,10,0,10,38,79
  128, 152, 176, 198, 217, 233, 245, 252, 255, 252, 245, 233, 217, 198, 176, 152, 128, 103, 79, 57, 38, 22, 10, 3, 1, 3, 10,
  22, 38, 57, 79, 103
};

// CC65 PETSCII conversion is a pain, so we provide the exact bytes of the file name

// POPCORN.MOD
// char filename[16]={0x50,0x4f,0x50,0x43,0x4f,0x52,0x4e,0x2e,0x4d,0x4f,0x44,0x00,
//		   0x00,0x00,0x00,0x00  };
// AXELF.MOD
// char filename[16]={0x41,0x58,0x45,0x4c,0x46,0x2e,0x4d,0x4f,0x44,0x00,0x00,0x00,
//		   0x00,0x00,0x00,0x00 };
// SWEET2.MOD
char filename[16] = { 0x53, 0x57, 0x45, 0x45, 0x54, 0x32, 0x2e, 0x4d, 0x4f, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
// CCC.MOD
// char filename[16]={0x43,0x43,0x43,0x2e,0x4d,0x4f,0x44,0x00,0x00,0x00,0x00,0x00,
//		   0x00,0x00,0x00,0x00 };

unsigned char current_pattern_in_song = 0;
unsigned char current_pattern = 0;
unsigned char current_pattern_position = 0;

unsigned char screen_first_row = 0;

unsigned ch_en[4] = { 1, 1, 1, 1 };

unsigned char pattern_buffer[16];
char note_fmt[9 + 1];

void audioxbar_setcoefficient(uint8_t n, uint8_t value)
{
  // Select the coefficient
  POKE(0xD6F4, n);

  // Now wait at least 16 cycles for it to settle
  POKE(0xD020U, PEEK(0xD020U));
  POKE(0xD020U, PEEK(0xD020U));

  POKE(0xD6F5U, value);
}

char* note_name(unsigned short period)
{
  switch (period) {
  case 0:
    return "---";

  case 1712:
    return "C-0";
  case 1616:
    return "C#0";
  case 1524:
    return "D-0";
  case 1440:
    return "D#0";
  case 1356:
    return "E-0";
  case 1280:
    return "F-0";
  case 1208:
    return "F#0";
  case 1140:
    return "G-0";
  case 1076:
    return "G#0";
  case 1016:
    return "A-0";
  case 960:
    return "A#0";
  case 906:
    return "B-0";

  case 856:
    return "C-1";
  case 808:
    return "C#1";
  case 762:
    return "D-1";
  case 720:
    return "D#1";
  case 678:
    return "E-1";
  case 640:
    return "F-1";
  case 604:
    return "F#1";
  case 570:
    return "G-1";
  case 538:
    return "G#1";
  case 508:
    return "A-1";
  case 480:
    return "A#1";
  case 453:
    return "B-1";

  case 428:
    return "C-2";
  case 404:
    return "C#2";
  case 381:
    return "D-2";
  case 360:
    return "D#2";
  case 339:
    return "E-2";
  case 320:
    return "F-2";
  case 302:
    return "F#2";
  case 285:
    return "G-2";
  case 269:
    return "G#2";
  case 254:
    return "A-2";
  case 240:
    return "A#2";
  case 226:
    return "B-2";

  case 214:
    return "C-3";
  case 202:
    return "C#3";
  case 190:
    return "D-3";
  case 180:
    return "D#3";
  case 170:
    return "E-3";
  case 160:
    return "F-3";
  case 151:
    return "F#3";
  case 143:
    return "G-3";
  case 135:
    return "G#3";
  case 127:
    return "A-3";
  case 120:
    return "A#3";
  case 113:
    return "B-3";

  default:
    return "???";
  }
}

void format_note(unsigned char* n)
{
  snprintf(note_fmt, 9, "%s%X%02X%02X", note_name(((n[0] & 0xf) << 8) | n[1]), n[0] >> 4, n[2], n[3]);
}

void draw_pattern_row(unsigned char screen_row, unsigned char pattern_row, unsigned char colour)
{
  unsigned char c;
  // Get pattern row
  lcopy(0x40000 + song_offset + (current_pattern << 10) + (pattern_row << 4), pattern_buffer, 16);
  // Draw row number
  snprintf(note_fmt, 9, "%02d", pattern_row);
  print_text80(0, screen_row, 0x01, note_fmt);
  // Draw the four notes
  c = ch_en[0] ? colour : 2;
  format_note(&pattern_buffer[0]);
  print_text80(4, screen_row, c, note_fmt);
  c = ch_en[1] ? colour : 2;
  format_note(&pattern_buffer[4]);
  print_text80(13, screen_row, c, note_fmt);
  c = ch_en[2] ? colour : 2;
  format_note(&pattern_buffer[8]);
  print_text80(22, screen_row, c, note_fmt);
  c = ch_en[3] ? colour : 2;
  format_note(&pattern_buffer[12]);
  print_text80(31, screen_row, c, note_fmt);
}

void show_current_position_in_song(void)
{
  if (current_pattern_position < screen_first_row)
    screen_first_row = current_pattern_position;
  while (current_pattern_position > (screen_first_row + 16)) {
    screen_first_row += 16;
  }
  if (screen_first_row > (63 - (25 - 5)))
    screen_first_row = (63 - (25 - 5));

  for (i = 5; i < 25; i++) {
    draw_pattern_row(i, screen_first_row + i - 5, ((screen_first_row + i - 5) == current_pattern_position) ? 0x27 : 0x0c);
  }
}

unsigned short top_addr;
unsigned short freq;

void play_sample(unsigned char channel, unsigned char instrument, unsigned short period, unsigned short effect)
{
  unsigned ch_ofs = channel << 4;

  if (channel > 3)
    return;

  freq = 0xFFFFL / period;

  // Stop playback while loading new sample data
  POKE(0xD720 + ch_ofs, 0x00);
  // Load sample address into base and current addr
  POKE(0xD721 + ch_ofs, (((unsigned short)instrument_addr[instrument]) >> 0) & 0xff);
  POKE(0xD722 + ch_ofs, (((unsigned short)instrument_addr[instrument]) >> 8) & 0xff);
  POKE(0xD723 + ch_ofs, (((unsigned long)instrument_addr[instrument]) >> 16) & 0xff);
  POKE(0xD72A + ch_ofs, (((unsigned short)instrument_addr[instrument]) >> 0) & 0xff);
  POKE(0xD72B + ch_ofs, (((unsigned short)instrument_addr[instrument]) >> 8) & 0xff);
  POKE(0xD72C + ch_ofs, (((unsigned long)instrument_addr[instrument]) >> 16) & 0xff);
  // Sample top address
  top_addr = instrument_addr[instrument] + instrument_lengths[instrument];
  POKE(0xD727 + ch_ofs, (top_addr >> 0) & 0xff);
  POKE(0xD728 + ch_ofs, (top_addr >> 8) & 0xff);

  // Volume
  POKE(0xD729 + ch_ofs, instrument_vol[instrument] >> 2);
  // Mirror channel quietly on other side for nicer stereo imaging
  POKE(0xD71C + channel, instrument_vol[instrument] >> 4);

  // XXX - We should set base addr and top addr to the looping range, if the
  // sample has one.
  if (instrument_loopstart[instrument]) {
    // start of loop
    POKE(0xD721 + ch_ofs, (((unsigned long)instrument_addr[instrument] + 2 * instrument_loopstart[instrument]) >> 0) & 0xff);
    POKE(0xD722 + ch_ofs, (((unsigned long)instrument_addr[instrument] + 2 * instrument_loopstart[instrument]) >> 8) & 0xff);
    POKE(
        0xD723 + ch_ofs, (((unsigned long)instrument_addr[instrument] + 2 * instrument_loopstart[instrument]) >> 16) & 0xff);

    // Top addr
    POKE(0xD727 + ch_ofs, (((unsigned short)instrument_addr[instrument]
                               + 2 * (instrument_loopstart[instrument] + instrument_looplen[instrument] - 1))
                              >> 0)
                              & 0xff);
    POKE(0xD728 + ch_ofs, (((unsigned short)instrument_addr[instrument]
                               + 2 * (instrument_loopstart[instrument] + instrument_looplen[instrument] - 1))
                              >> 8)
                              & 0xff);
  }

  //  POKE(0xC050+channel,instrument);
  //  POKE(0xC0A0+channel,instrument_vol[instrument]);

  // Calculate time base.
  // XXX Here we use a slightly randomly chosen fudge-factor
  // It should be:
  // SPEED = SAMPLE RATE * 0.414252
  // But I don't (yet) know how to get the fundamental frequency of a sample
  // from a MOD file

  // The natural timebase for MOD files is ~3.5MHz
  // This means we need a time-base of ~11.42 for PAL and ~11.31 for
  // NTSC
  // Here the MEGA65's 25x18 hardware multiplier comes in handy.
  // 11.42x ~= 748316 / 65536   = $B6B1C / $10000
  // 11.31x ~= 741494 / 65536   = $B5075 / $10000

  // XXX Some samples manifestly require a slower play back rate,
  // but this does not seem to be encoded anywhere!? The "BOMI"
  // sample in POPCORN.MOD is an example of this

  //  POKE(0xC0F0+channel,instrument_finetune[instrument]);
  POKE(0xD770, sample_rate_divisor);
  POKE(0xD771, sample_rate_divisor >> 8);
  POKE(0xD772, sample_rate_divisor >> 16);
  POKE(0xD773, 0x00);

  POKE(0xD774, freq);
  POKE(0xD775, freq >> 8);
  POKE(0xD776, 0);

#if 0
  snprintf(msg,64,"Period=%4d ($%04x) -> $%02x%02x%02x%02x%02x%02x.       ",
	   period,freq,PEEK(0xD77D),PEEK(0xD77C),PEEK(0xD77B),PEEK(0xD77A),PEEK(0xD779),PEEK(0xD778));
  print_text80(0,1,7,msg);
  snprintf(msg,64,"$%02x%02x%02x%02x x $%02x%02x%02x",
	   PEEK(0xD773),PEEK(0xD772),PEEK(0xD771),PEEK(0xD770),
	   PEEK(0xD776),PEEK(0xD775),PEEK(0xD774)
	   );
  print_text80(0,2,8,msg);
#endif

  // Pick results from output / 2^16
  POKE(0xD724 + ch_ofs, PEEK(0xD77A));
  POKE(0xD725 + ch_ofs, PEEK(0xD77B));
  //  POKE(0xD726+ch_ofs,PEEK(0xD77C));
  POKE(0xD726 + ch_ofs, 0);

  if (instrument_loopstart[instrument]) {
    // Enable playback+ nolooping of channel 0, 8-bit, no unsigned samples
    POKE(0xD720 + ch_ofs, 0xC2);
  }
  else {
    // Enable playback+ nolooping of channel 0, 8-bit, no unsigned samples
    POKE(0xD720 + ch_ofs, 0x82);
  }

  switch (effect & 0xf00) {
  case 0xf00:
    // Tempo/Speed
    if ((effect & 0x0ff) < 0x20) {
      tempo = RASTERS_PER_MINUTE / BEATS_PER_MINUTE / ROWS_PER_BEAT;
      tempo = tempo * 6 / (effect & 0x1f);
    }
    break;
  case 0xc00:
    // Channel volume
    POKE(0xD729 + ch_ofs, effect & 0xff);
    break;
  }

  // Enable audio dma, enable bypass of audio mixer, signed samples
  POKE(0xD711, 0x80);
}

unsigned char last_instruments[4] = { 0, 0, 0, 0 };

void play_note(unsigned char channel, unsigned char* note)
{
  unsigned char instrument;
  unsigned short period;
  unsigned short effect;

  instrument = note[0] & 0xf0;
  instrument |= note[2] >> 4;
  if (!instrument)
    instrument = last_instruments[channel];
  else
    instrument--;
  last_instruments[channel] = instrument;
  period = ((note[0] & 0xf) << 8) + note[1];
  effect = ((note[2] & 0xf) << 8) + note[3];

  if (period)
    play_sample(channel, instrument, period, effect);
}

void play_mod_pattern_line(void)
{
  // Get pattern row
  lcopy(0x40000 + song_offset + (current_pattern << 10) + (current_pattern_position << 4), pattern_buffer, 16);
  if (ch_en[0])
    play_note(0, &pattern_buffer[0]);
  if (ch_en[1])
    play_note(1, &pattern_buffer[4]);
  if (ch_en[2])
    play_note(2, &pattern_buffer[8]);
  if (ch_en[3])
    play_note(3, &pattern_buffer[12]);
}

void play_sine(unsigned char ch, unsigned long time_base)
{
  unsigned ch_ofs = ch << 4;

  if (ch > 3)
    return;

  // Play sine wave for frequency matching
  POKE(0xD721 + ch_ofs, ((unsigned short)&sin_table) & 0xff);
  POKE(0xD722 + ch_ofs, ((unsigned short)&sin_table) >> 8);
  POKE(0xD723 + ch_ofs, 0);
  POKE(0xD72A + ch_ofs, ((unsigned short)&sin_table) & 0xff);
  POKE(0xD72B + ch_ofs, ((unsigned short)&sin_table) >> 8);
  POKE(0xD72C + ch_ofs, 0);
  // 16 bytes long
  POKE(0xD727 + ch_ofs, ((unsigned short)&sin_table + 32) & 0xff);
  POKE(0xD728 + ch_ofs, ((unsigned short)&sin_table + 32) >> 8);
  // 1/4 Full volume
  POKE(0xD729 + ch_ofs, 0x3F);
  // Enable playback+looping of channel 0, 8-bit samples, signed
  POKE(0xD720 + ch_ofs, 0xE2);
  // Enable audio dma
  POKE(0xD711 + ch_ofs, 0x80);

  // time base = $001000
  POKE(0xD724 + ch_ofs, time_base & 0xff);
  POKE(0xD725 + ch_ofs, time_base >> 8);
  POKE(0xD726 + ch_ofs, time_base >> 16);
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

void load_modfile(void)
{
  song_offset = 1084;

  // Load a MOD file for testing
  closeall();
  fd = open(filename);
  if (fd == 0xff) {
    print_text80(0, 0, 2, "Could not read MOD file                                             ");
    return;
  }
  load_addr = 0x40000;
  while ((count = read512(buffer)) > 0) {
    if (count > 512)
      break;
    lcopy((unsigned long)buffer, (unsigned long)load_addr, 512);
    POKE(0xD020, (PEEK(0xD020) + 1) & 0xf);
    load_addr += 512;

    if (count < 512)
      break;
  }

  h640_text_mode();
  lfill(0xc000, 0x20, 2000);

  lcopy(0x40000, mod_name, 20);
  mod_name[20] = 0;

  // Show MOD file name
  print_text80(0, 0, 1, mod_name);

  // Check if 15 orr 31 instrument mode
  lcopy(0x40000 + 1080, mod_name, 4);
  mod_name[4] = 0;
  song_offset = 1084 - (16 * 30);

  for (i = 0; i < NUM_SIGS; i++) {
    for (a = 0; a < 4; a++)
      if (mod_name[a] != mod31_sigs[i][a])
        break;
    if (a == 4)
      song_offset = 1084;
  }

  // Show  instruments from MOD file
  for (i = 0; i < (song_offset == 1084 ? 31 : 15); i++) {
    lcopy(0x40014 + i * 30, mod_name, 22);
    mod_name[22] = 0;
    if (mod_name[0]) {
      if (i + 5 < 25)
        print_text80(57, i + 5, 0x0c, mod_name);
      //	printf("Instr#%d is %s\n",i,mod_name);
    }
    // Get instrument data for plucking
    lcopy(0x40014 + i * 30 + 22, mod_name, 22);
    instrument_lengths[i] = mod_name[1] + (mod_name[0] << 8);
    if ((instrument_lengths[i] & 0x8000)) {
      printf("ERROR: MOD file has samples >64KB\n");
      return;
    }
    // Redenominate instrument length into bytes
    instrument_lengths[i] <<= 1;
    instrument_finetune[i] = mod_name[2];

    // Instrument volume
    instrument_vol[i] = mod_name[3];

    // Repeat start point and end point
    instrument_loopstart[i] = mod_name[5] + (mod_name[4] << 8);
    instrument_looplen[i] = mod_name[7] + (mod_name[6] << 8);
    //      POKE(0xC048+(i+5)*80,mod_name[5]);
    //      POKE(0xC049+(i+5)*80,mod_name[4]);
    //      POKE(0xC04A+(i+5)*80,mod_name[7]);
    //      POKE(0xC04B+(i+5)*80,mod_name[6]);
  }

  song_length = lpeek(0x40000 + 950);
  song_loop_point = lpeek(0x40000 + 951);
  //  printf("Song length = %d, loop point = %d\n",
  //	 song_length,song_loop_point);
  lcopy(0x40000 + 952, song_pattern_list, 128);
  for (i = 0; i < song_length; i++) {
    //    printf(" $%02x",song_pattern_list[i]);
    if (song_pattern_list[i] > max_pattern)
      max_pattern = song_pattern_list[i];
  }
  //  printf("\n%d unique patterns.\n",max_pattern);
  sample_data_start = 0x40000L + song_offset + (max_pattern + 1) * 1024;

  //  printf("sample data starts at $%lx\n",sample_data_start);
  for (i = 0; i < MAX_INSTRUMENTS; i++) {
    instrument_addr[i] = sample_data_start;
    sample_data_start += instrument_lengths[i];
    //    printf("Instr #%d @ $%05lx\n",i,instrument_addr[i]);
  }

  current_pattern_in_song = 0;
  current_pattern = song_pattern_list[0];
  current_pattern_position = 0;

  show_current_position_in_song();
}

void read_filename(void)
{
  unsigned char len = strlen(filename);
  print_text80(0, 0, 1, "Enter name of MOD file to load:");
  while (1) {
    print_text80(0, 1, 7, "                    ");
    print_text80(0, 1, 7, filename);
    // Show block cursor
    lpoke(0xff80000 + 80 + strlen(filename), 0x21);

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

  while (PEEK(0xD610))
    POKE(0xD610, 0);

  POKE(0xD020, 0);
  POKE(0xD021, 0);

  // Stop all DMA audio first
  POKE(0xD720, 0);
  POKE(0xD730, 0);
  POKE(0xD740, 0);
  POKE(0xD750, 0);

#ifdef DIRECT_TEST
  while (1) {
    for (top_addr = 0; top_addr < 32; top_addr++) {
      POKE(0xD6F9, sin_table[top_addr]);
      POKE(0xD6FB, sin_table[top_addr]);
      for (i = 0; i < 100; i++)
        continue;
      POKE(0xD020, (PEEK(0xD020) + 1) & 0x0f);
    }
  }
#endif

#ifdef MOD_TEST

  h640_text_mode();

  // Audio cross-bar full volume
  for (i = 0; i < 256; i++)
    audioxbar_setcoefficient(i, 0xff);

  load_modfile();

  POKE(0xD020, 1);

  while (1) {
    if (PEEK(0xD610)) {
      switch (PEEK(0xD610)) {
      case 0xF1:
        sample_rate_divisor += 0x10000;
        break;
      case 0xF2:
        sample_rate_divisor -= 0x10000;
        break;
      case 0xF3:
        sample_rate_divisor += 0x100;
        break;
      case 0xF4:
        sample_rate_divisor -= 0x100;
        break;
      case 0xF5:
        sample_rate_divisor += 0x1;
        break;
      case 0xF6:
        sample_rate_divisor -= 0x1;
        break;
      case 0xF7:
        tempo += 0x20;
        break;
      case 0xF8:
        tempo -= 0x20;
        break;
      case 0x4d:
      case 0x6d:
        // M - Toggle master enable
        POKE(0xD711, PEEK(0xD711) ^ 0x80);
        break;
      case 0x31:
        ch_en[0] ^= 1;
        show_current_position_in_song();
        break;
      case 0x32:
        ch_en[1] ^= 1;
        show_current_position_in_song();
        break;
      case 0x33:
        ch_en[2] ^= 1;
        show_current_position_in_song();
        break;
      case 0x34:
        ch_en[3] ^= 1;
        show_current_position_in_song();
        break;
      case 0x30:
        // 0 - Reset song to start
        current_pattern_in_song = 0;
        current_pattern = song_pattern_list[0];
        current_pattern_position = 0;

        show_current_position_in_song();

        break;
      case 0xCF: // MEGA+O
        POKE(0xD610, 0);
        read_filename();
        load_modfile();
        show_current_position_in_song();
        break;
      case 0x0d:
        // RETURN - Play current note
        playing ^= 1;
        break;
      case 0x9d:
        current_pattern_in_song--;
        if (current_pattern_in_song >= song_length)
          current_pattern_in_song = (song_length - 1);
        current_pattern = song_pattern_list[current_pattern_in_song];
        show_current_position_in_song();
        break;
      case 0x1d:
        current_pattern_in_song++;
        if (current_pattern_in_song == song_length)
          current_pattern_in_song = 0;
        current_pattern = song_pattern_list[current_pattern_in_song];
        show_current_position_in_song();
        break;
      case 0x11:
        current_pattern_position++;
        if (current_pattern_position > 0x3f) {
          current_pattern_position = 0x00;
          current_pattern_in_song++;
          if (current_pattern_in_song == song_length)
            current_pattern_in_song = 0;
          current_pattern = song_pattern_list[current_pattern_in_song];
          current_pattern_position = 0;
        }
        show_current_position_in_song();
        break;
      case 0x91:
        current_pattern_position--;
        if (current_pattern_position > 0x3f) {
          current_pattern_position = 0x3f;
          current_pattern_in_song--;
          if (current_pattern_in_song < 0)
            current_pattern_in_song = 0;
          current_pattern = song_pattern_list[current_pattern_in_song];
          current_pattern_position = 0;
        }
        show_current_position_in_song();
        break;
      case 0x20:
        playing = 2;
        break;
      case '+':
        tempo--;
        if (tempo == 0xff)
          tempo = 0;
        break;
      case '-':
        tempo++;
        if (tempo == 0)
          tempo = 0xff;
        break;
      }
      if (PEEK(0xD610) >= 0x61 && PEEK(0xD610) < 0x6d) {
        play_sample(0, PEEK(0xD610) & 0xf, 200, 0);
        POKE(0xD020, PEEK(0xD610) & 0xf);
      }
      if (PEEK(0xD610) >= 0x41 && PEEK(0xD610) < 0x4d) {
        play_sample(0, PEEK(0xD610) & 0xf, 100, 0);
        POKE(0xD020, PEEK(0xD610) & 0xf);
      }
      if (PEEK(0xD610) == 0x40) {
        play_sample(0, PEEK(0xD610) & 0xf, 200, 0);
        POKE(0xD020, PEEK(0xD610) & 0xf);
      }
      POKE(0xD610, 0);
    }

    if (0)
      for (i = 0; i < 4; i++) {
        // Display Audio DMA channel
        snprintf(msg, 64, "%x: e=%x l=%x p=%x st=%x v=$%02x cur=$%02x%02x%02x tb=$%02x%02x%02x ct=$%02x%02x%02x", i,
            (PEEK(0xD720 + i * 16 + 0) & 0x80) ? 1 : 0, (PEEK(0xD720 + i * 16 + 0) & 0x40) ? 1 : 0,
            (PEEK(0xD720 + i * 16 + 0) & 0x10) ? 1 : 0, (PEEK(0xD720 + i * 16 + 0) & 0x08) ? 1 : 0, PEEK(0xD729 + i * 16),
            PEEK(0xD72C + i * 16), PEEK(0xD72B + i * 16), PEEK(0xD72A + i * 16), PEEK(0xD726 + i * 16),
            PEEK(0xD725 + i * 16), PEEK(0xD724 + i * 16), PEEK(0xD72F + i * 16), PEEK(0xD72E + i * 16),
            PEEK(0xD72D + i * 16));
        print_text80(16, i, 15, msg);
      }
    snprintf(msg, 64, "Sample divisor = $%06lx, tempo=$%04x", sample_rate_divisor, tempo);
    print_text80(0, 3, 15, msg);

    if (playing) {
      play_mod_pattern_line();
      for (i = 0; i < tempo; i++) {
        c = PEEK(0xD012);
        while (PEEK(0xD012) == c)
          continue;
      }
      current_pattern_position++;
      if (current_pattern_position > 0x3f) {
        current_pattern_position = 0x00;
        current_pattern_in_song++;
        if (current_pattern_in_song == song_length)
          current_pattern_in_song = 0;
        current_pattern = song_pattern_list[current_pattern_in_song];
        current_pattern_position = 0;
      }
      show_current_position_in_song();
    }
    playing &= 0x1;
  }

#endif

#ifdef SINE_TEST

  play_sine(0, time_base);

  printf("%c", 0x93);
  while (1) {

    printf("%c", 0x13);

    for (i = 0; i < 4; i++) {
      printf("%d: en=%d, loop=%d, pending=%d, B24=%d, SS=%d\n"
             "   v=$%02x, base=$%02x%02x%02x, top=$%04x\n"
             "   curr=$%02x%02x%02x, tb=$%02x%02x%02x, ct=$%02x%02x%02x\n",
          i, (PEEK(0xD720 + i * 16 + 0) & 0x80) ? 1 : 0, (PEEK(0xD720 + i * 16 + 0) & 0x40) ? 1 : 0,
          (PEEK(0xD720 + i * 16 + 0) & 0x20) ? 1 : 0, (PEEK(0xD720 + i * 16 + 0) & 0x10) ? 1 : 0,
          (PEEK(0xD720 + i * 16 + 0) & 0x3), PEEK(0xD729 + i * 16), PEEK(0xD723 + i * 16), PEEK(0xD722 + i * 16),
          PEEK(0xD721 + i * 16), PEEK(0xD727 + i * 16) + (PEEK(0xD728 + i * 16) << 8 + i * 16), PEEK(0xD72C + i * 16),
          PEEK(0xD72B + i * 16), PEEK(0xD72A + i * 16), PEEK(0xD726 + i * 16), PEEK(0xD725 + i * 16), PEEK(0xD724 + i * 16),
          PEEK(0xD72F + i * 16), PEEK(0xD72E + i * 16), PEEK(0xD72D + i * 16));
    }
    if (PEEK(0xD610)) {
      switch (PEEK(0xD610)) {
      case 0x30:
        ch = 0;
        break;
      case 0x31:
        ch = 1;
        break;
      case 0x32:
        ch = 2;
        break;
      case 0x33:
        ch = 3;
        break;
      case 0x11:
        time_base--;
        break;
      case 0x91:
        time_base++;
        break;
      case 0x1d:
        time_base -= 0x100;
        break;
      case 0x9d:
        time_base += 0x100;
        break;
      }
      time_base &= 0x0fffff;

      POKE(0xD720, 0);
      POKE(0xD730, 0);
      POKE(0xD740, 0);
      POKE(0xD750, 0);
      play_sine(ch, time_base);

      POKE(0xD610, 0);
    }

    POKE(0x400 + 999, PEEK(0x400 + 999) + 1);
  }
#endif

  // base addr = $040000
  POKE(0xD721, 0x00);
  POKE(0xD722, 0x00);
  POKE(0xD723, 0x04);
  // time base = $001000
  POKE(0xD724, 0x01);
  POKE(0xD725, 0x10);
  POKE(0xD726, 0x00);
  // Top address
  POKE(0xD727, 0xFE);
  POKE(0xD728, 0xFF);
  // Full volume
  POKE(0xD729, 0xFF);
  // Enable audio dma, 16 bit samples
  POKE(0xD711, 0x80);
  // Enable playback+looping of channel 0, 16-bit samples
  //  POKE(0xD720,0xC3);

  //  graphics_mode();
  //  print_text(0,0,1,"");

  printf("%c", 0x93);

  while (1) {
    printf("%c", 0x13);

    printf("Audio DMA tick counter = $%02x%02x%02x%02x\n", PEEK(0xD71F), PEEK(0xD71E), PEEK(0xD71D), PEEK(0xD71C));

    printf("Master enable = %d,\n   blocked=%d, block_timeout=%d    \n"
           "   write_enable=%d\n",
        PEEK(0xD711) & 0x80 ? 1 : 0, PEEK(0xD711) & 0x40 ? 1 : 0, PEEK(0xD711) & 0x0f, PEEK(0xD711) & 0x20 ? 1 : 0

    );
    for (i = 0; i < 4; i++) {
      // Display Audio DMA channel
      printf("%d: en=%d, loop=%d, pending=%d, B24=%d, SS=%d\n"
             "   v=$%02x, base=$%02x%02x%02x, top=$%04x\n"
             "   curr=$%02x%02x%02x, tb=$%02x%02x%02x, ct=$%02x%02x%02x\n",
          i, (PEEK(0xD720 + i * 16 + 0) & 0x80) ? 1 : 0, (PEEK(0xD720 + i * 16 + 0) & 0x40) ? 1 : 0,
          (PEEK(0xD720 + i * 16 + 0) & 0x20) ? 1 : 0, (PEEK(0xD720 + i * 16 + 0) & 0x10) ? 1 : 0,
          (PEEK(0xD720 + i * 16 + 0) & 0x3), PEEK(0xD729 + i * 16), PEEK(0xD723 + i * 16), PEEK(0xD722 + i * 16),
          PEEK(0xD721 + i * 16), PEEK(0xD727 + i * 16) + (PEEK(0xD728 + i * 16) << 8 + i * 16), PEEK(0xD72C + i * 16),
          PEEK(0xD72B + i * 16), PEEK(0xD72A + i * 16), PEEK(0xD726 + i * 16), PEEK(0xD725 + i * 16), PEEK(0xD724 + i * 16),
          PEEK(0xD72F + i * 16), PEEK(0xD72E + i * 16), PEEK(0xD72D + i * 16));
    }

    printf("Audio left/right: $%02x%02x, $%02X%02X\n", PEEK(0xD6F8), PEEK(0xD6F9), PEEK(0xD6FA), PEEK(0xD6FB));
  }
}
