#include <stdio.h>
#include <string.h>
#include <ctype.h>

char data[819200];

void write_tso_byte(int track, int sector, int offset, int val)
{
  data[(track - 1) * (256 * 40) + sector * 256 + offset] = val;
}

void write_tso_str(int track, int sector, int offset, char* str, int maxlen)
{
  char* pstr = str;
  for (int k = 0; k < maxlen; k++) {
    int val = 0xa0; // assume padding byte
    if (*pstr != '\0') {
      val = *pstr;
      pstr++;
    }
    write_tso_byte(track, sector, offset + k, val);
  }
}

void write_header(char* disk_name, char* disk_id)
{
  write_tso_byte(40, 0, 0x00, 40); // write T/S location of first directory sector
  write_tso_byte(40, 0, 0x01, 3);
  write_tso_byte(40, 0, 0x02, 0x44); // Disk DOS version type 'D' = 1581
  write_tso_byte(40, 0, 0x03, 0);
  write_tso_str(40, 0, 0x04, disk_name, 16);
  write_tso_byte(40, 0, 0x14, 0xA0);
  write_tso_byte(40, 0, 0x15, 0xA0);
  write_tso_str(40, 0, 0x16, disk_id, 2);
  write_tso_byte(40, 0, 0x18, 0xA0);
  write_tso_byte(40, 0, 0x19, '3');
  write_tso_byte(40, 0, 0x1A, 'D');
  write_tso_byte(40, 0, 0x1B, 0xA0);
  write_tso_byte(40, 0, 0x1C, 0xA0);

  // some BAM initialisation too
  write_tso_byte(40, 1, 0x00, 40); // next bam t/s
  write_tso_byte(40, 1, 0x01, 2);
  write_tso_byte(40, 1, 0x02, 'D');
  write_tso_byte(40, 1, 0x03, 0xBB);
  write_tso_str(40, 1, 0x04, disk_id, 2);
  write_tso_byte(40, 1, 0x06, 0xC0); // verify on, check header crc
  write_tso_byte(40, 1, 0x07, 0x00); // auto-bootloader flag?
  // initialise bam entries for tracks (set all sectors as empty)
  for (int k = 0; k < 40; k++)
    write_tso_str(40, 1, 0x10 + k * 6, "\x28\xff\xff\xff\xff\xff", 6);

  write_tso_byte(40, 2, 0x00, 0x00); // next bam t/s (0x00 0xff is last one)
  write_tso_byte(40, 2, 0x01, 0xff);
  write_tso_byte(40, 2, 0x02, 'D');
  write_tso_byte(40, 2, 0x03, 0xBB);
  write_tso_str(40, 2, 0x04, disk_id, 2);
  write_tso_byte(40, 2, 0x06, 0xC0); // verify on, check header crc
  write_tso_byte(40, 2, 0x07, 0x00); // auto-bootloader flag?
  // initialise bam entries for tracks (set all sectors as empty)
  for (int k = 0; k < 40; k++)
    write_tso_str(40, 2, 0x10 + k * 6, "\x28\xff\xff\xff\xff\xff", 6);
}

#define FTYPE_DEL 0
#define FTYPE_SEQ 1
#define FTYPE_PRG 2
#define FTYPE_USR 3
#define FTYPE_REL 4
#define FTYPE_CBM 5
#define FTYPE_CLOSEDFLAG 0x80

void write_direntry(char* prgname, int firsttrack, int firstsector, int sectorcnt)
{
  write_tso_byte(40, 3, 0x00, 0x00); // write T/S location of next dir-entry sector (0x00 and 0xff for last sector)
  write_tso_byte(40, 3, 0x01, 0xff);
  write_tso_byte(40, 3, 0x02, FTYPE_PRG | FTYPE_CLOSEDFLAG);
  write_tso_byte(40, 3, 0x03, firsttrack);
  write_tso_byte(40, 3, 0x04, firstsector);
  write_tso_str(40, 3, 0x05, prgname, 16);
  write_tso_byte(40, 3, 0x15, 0x00);
  write_tso_byte(40, 3, 0x16, 0x00);
  write_tso_byte(40, 3, 0x17, 0x00);
  write_tso_byte(40, 3, 0x1E, sectorcnt & 0xff);
  write_tso_byte(40, 3, 0x1F, sectorcnt >> 8);
}

void get_nice_prgname(char* dest, char* src)
{
  char* pdest = dest;
  char* psrc = src;
  while (*psrc != '.' && *psrc != '\0') {
    *pdest = toupper(*psrc);
    pdest++;
    psrc++;
  }
  *pdest = '\0';
}

void get_nice_d81name(char* dest, char* src)
{
  get_nice_prgname(dest, src);

  strcat(dest, ".D81");
}

int calc_offset(int track, int sector, int offset)
{
  return (track - 1) * (256 * 40) + sector * 256 + offset;
}

void mark_sector_used(char* bamentry, int sector)
{
  int offs = sector >> 3;
  int bitloc = sector & 0x0f;
  bamentry[1 + offs] &= ~bitloc;
}

int calc_sectors_free(char* bamentry)
{
  int cnt = 0;
  for (int k = 1; k < 6; k++) {
    int byteval = bamentry[k];
    for (int z = 0; z < 8; z++) {
      if (byteval & (1 << z))
        cnt++;
    }
  }
  return cnt;
}

void update_bam(int track, int sector)
{
  int offs;
  if (track <= 40)
    offs = calc_offset(40, 1, 0x10) + (track - 1) * 0x06; // 6 bytes per track BAM entry
  else
    offs = calc_offset(40, 2, 0x10) + (track - 41) * 0x06; // 6 bytes per track BAM entry

  // first bam sector is at t/s = 40/1, 2nd bam is at t/s = 40/2
  char* bamentry = &data[offs];

  mark_sector_used(bamentry, sector);
  bamentry[0x00] = calc_sectors_free(bamentry); // number of free sectors on the track
}

void update_tsptr(int track, int sector, int t, int s)
{
  char* dest = &data[(track - 1) * (256 * 40) + sector * 256];
  dest[0] = t;
  dest[1] = s;
}

static void write_sector(int track, int sector, char* chunk)
{
  char* dest = &data[(track - 1) * (256 * 40) + sector * 256];
  memcpy(dest, chunk, 256);

  // update bam?
  update_bam(track, sector);
}

int check_file_access(char* file)
{
  FILE* f = fopen(file, "rb");
  if (!f)
    return -1;
  fclose(f);

  return 0;
}

void add_prg(char* fname)
{
  // load the prg
  FILE* f = fopen(fname, "rb");
  char prgname[256];
  get_nice_prgname(prgname, fname);
  char chunk[256] = { 0 };

  int starttrack = 1;
  int startsector = 0;
  int curtrack = starttrack;
  int cursector = startsector;
  int prevtrack = 0;
  int prevsector = 0;

  int bytes;
  int sectorcnt = 0;
  while ((bytes = fread(chunk + 2, 1, 254, f)) != 0) {
    if (sectorcnt != 0) {
      update_tsptr(prevtrack, prevsector, curtrack, cursector);
    }
    chunk[0] = 0x00; // assume this is the last chunk
    chunk[1] = 0xff; // (can overwrite it later)
    write_sector(curtrack, cursector, chunk);
    prevtrack = curtrack;
    prevsector = cursector;
    sectorcnt++;
    memset(chunk, 0, 256);
    cursector++;
    if (cursector >= 40) {
      cursector = 0;
      curtrack++;
    }
  }

  write_direntry(prgname, starttrack, startsector, sectorcnt);

  fclose(f);
}

void save_d81(char* fname)
{
  FILE* f = fopen(fname, "wb");
  fwrite(data, 1, 819200, f);
  fclose(f);
}

char* create_d81_for_prg(char* prgfname)
{
  static char d81name[256];

  if (check_file_access(prgfname)) {
    printf("ERROR: Cannot open file '%s'\n", prgfname);
    return NULL;
  }

  memset(data, 0, 819200);
  write_header("PRG WRAPPER", "GI");
  add_prg(prgfname);

  get_nice_d81name(d81name, prgfname);
  printf("Wrapping \"%s\" into \"%s\"...\n", prgfname, d81name);
  save_d81(d81name);
  return d81name;
}

/*
void main(void)
{
  create_d81_for_prg("momo.prg");
}
*/
