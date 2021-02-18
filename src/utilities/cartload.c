/* Simple cartridge loader for MEGA65.
   It supports primarily Ocean Type 1 cartridges, because that is
   what the MEGA65 has hardware support for. Specifically, BANK 4
   and BANK 5 can be used to emulate a 128KB Ocean Type 1 cartridge.
*/

#include <stdio.h>
#include <cbm.h>
#include "memory.h"

unsigned char header[64];
unsigned char header_magic[16] = { 0x43, 0x36, 0x34, 0x20, 0x43, 0x41, 0x52, 0x54, 0x52, 0x49, 0x44, 0x47, 0x45, 0x20, 0x20,
  0x20 };
char cartname[32 + 1];
unsigned int cart_type = 0;
unsigned char exrom, game;
unsigned int i, chip_type, bank_num, load_addr, rom_size;
unsigned long section_len, j, offset;

unsigned char buffer[16384];
int b;

char filename[64] = "luftraus.crt";

// From:
// https://retrocomputing.stackexchange.com/questions/8253/how-to-read-disk-files-using-cbm-specific-functions-in-cc65-with-proper-error-ch
unsigned char kernel_getin(void* ptr, unsigned short size)
{
  unsigned char* data = (unsigned char*)ptr;
  unsigned short i;
  unsigned char st = 0;

  POKE(0x420, size);
  POKE(0x421, size >> 8);

  for (i = 0; i < size; i++) {
    st = cbm_k_readst();
    if (st) {
      break;
    }
    data[i] = cbm_k_getin();
  }
  data[i] = '\0';
  size = i;
  POKE(0x422, size);
  POKE(0x423, size >> 8);
  return st;
}

int main(void)
{

  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // Clear key input buffer
  while (PEEK(0xD610))
    POKE(0xD610, 0);

  // Open file for reading

  cbm_k_clall();
  cbm_k_setlfs(3, 8, 0);
  cbm_k_setnam(filename);
  cbm_k_open();
  if (cbm_k_readst()) {
    //    cbm_k_clrch();
    //    printf("ERROR: Could not read file '%s'\n",filename);
    return 0;
  }

  cbm_k_clrch();

  // Read and check the header
  cbm_k_chkin(3);
  kernel_getin(header, 64);
  if (cbm_k_readst()) {
    //    cbm_k_clrch();
    //    printf("ERROR: Could not read header.\n");
    return 0;
  }

  // Check header
  for (i = 0; i < 16; i++) {
    if (header[i] != header_magic[i]) {
      //      cbm_k_clrch();
      //      printf("ERROR: File header is incorrect for a .CRT file\n");
      return 0;
    }
  }

  // Get cartridge name
  for (i = 0; i < 32; i++)
    cartname[i] = header[32 + i];
  cartname[32] = 0;
  //  cbm_k_clrch();
  //  printf("Examining cartridge:\n  %s\n",cartname);

  // Check cartridge type etc
  offset = header[0x13] + (((unsigned long)header[0x12]) << 8L) + (((unsigned long)header[0x11]) << 16L)
         + (((unsigned long)header[0x10]) < 24L);

  if (offset != 0x40) {
    //    cbm_k_clrch();
    //    printf("WARNING: CRT file has strange header length.\n");
  }
  // Subtract length of header we have already read
  offset -= 0x40;

  if (header[0x14] != 0x01 || header[0x15] != 0x00) {
    //    cbm_k_clrch();
    //    printf("WARNING: CRT file version >01.00.\n");
  }

  cart_type = (header[0x16] << 8) + header[0x17];
  switch (cart_type) {
  case 1: // normal
  case 5: // Ocean Type 1
    break;
    //  default:
    //    printf("WARNING: Cartridge type $%04x unsupported.  Most likely won't work.\n", cart_type);
  }

  exrom = header[0x18];
  game = header[0x19];

#if 0
  // Skip any extra header bytes
  while(offset) {
    if (offset>16384) {
      kernel_getin(buffer,16384);
      offset-=16384;
    } else {
      kernel_getin(buffer,offset);
      offset=0;
    }      
  }
#endif

  while (!cbm_k_readst()) {
    // Look for next CHIP block
    kernel_getin(header, 16);
    for (i = 0; i < 16; i++)
      POKE(0x428 + i, header[i]);

    if (cbm_k_readst())
      break;

    //    if (cbm_k_readst()) break;
    // Check for CHIP magic string
    if (header[0] != 0x43 || header[1] != 0x48 || header[2] != 0x49 || header[3] != 0x50)
      break;
    section_len = (((unsigned long)header[0x4]) << 24) | (((unsigned long)header[0x5]) << 16) | (header[0x6] << 8)
                | (header[0x7]);
    chip_type = (header[8] << 8) + header[9];
    bank_num = (header[0xa] << 8) + header[0xb];
    POKE(0x700 + (bank_num & 0xff), PEEK(0x700 + (bank_num & 0xff) + 1));
    load_addr = (header[0xc] << 8) + header[0xd];
    rom_size = (header[0xe] << 8) + header[0xf];
    //    cbm_k_clrch();
    //    printf("CHIP: type=$%04x, bank=$%04x, addr=$%04x, size=$%04x\n",
    //	   chip_type,bank_num,load_addr,rom_size);
    //    if (chip_type) {
    //      cbm_k_clrch();
    //      printf("WARNING: Skipping non-ROM bank.\n");
    ///    }
    section_len -= 0x10;
    POKE(0x0410, section_len >> 0);
    POKE(0x0411, section_len >> 8);
    POKE(0x0412, bank_num >> 0);
    POKE(0x0413, bank_num >> 8);
    if (section_len > 16384 || rom_size > 16384) {
      //      cbm_k_clrch();
      //      printf("ERROR: CHIP block is >16KB.\n");
      return 0;
    }
    // Load ROM data into buffer
    cbm_k_chkin(3);
    kernel_getin(buffer, (unsigned int)section_len);

    // Then DMA into place
    lcopy((unsigned long)&buffer, 0x40000L + (bank_num * 8192L), rom_size);
  }

  // Close file
  cbm_k_close(3);

  cbm_k_clrch();
  printf("Setting up cartridge simulator...\n");

#if 1
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);
  asm("sei");
  POKE(0xD7FE, 0x02); // Enable Ocean Type 1 cart simulation
  POKE(0xD7FB, 0x01); // Disable physical cartridges
  // Set /GAME and /EXROM appropriately, and keep power on for
  // handheld version.
  // XXX HAve I misunderstood the sense here?
  // If not, then the CRT file is wrong having GAME and EXROM marked inactive.
  // For now, if I see this, I'll mark them both active
  if (!(exrom | game)) {
    exrom = 1;
    game = 1;
  }
  POKE(0xD7FD, (exrom ? 0x00 : 0x80) | (game ? 0x00 : 0x40) + 0x01);

  // Select bank 0 of the cart (must be done only after simulation enabled)
  POKE(0xDE00, 0x00);

  // Jump into cartridge
  asm("jmp ($8000)");
#endif
}
