/* Simple cartridge loader for MEGA65.
   It supports primarily Ocean Type 1 cartridges, because that is
   what the MEGA65 has hardware support for. Specifically, BANK 4
   and BANK 5 can be used to emulate a 128KB Ocean Type 1 cartridge.
*/ 

#include <stdio.h>
#include <cbm.h>
#include "memory.h"

unsigned char header[64];
unsigned char header_magic[16]=
  {0x43, 0x36, 0x34, 0x20, 0x43, 0x41, 0x52, 0x54,
   0x52, 0x49, 0x44, 0x47, 0x45, 0x20, 0x20, 0x20};
char cartname[32+1];
unsigned int cart_type=0;
unsigned char exrom,game;
unsigned int i,chip_type,bank_num,load_addr,rom_size;
unsigned long section_len,j,offset;

unsigned char buffer[16384];
int b;

char filename[64]="luftrauserz.crt";

// From: https://retrocomputing.stackexchange.com/questions/8253/how-to-read-disk-files-using-cbm-specific-functions-in-cc65-with-proper-error-ch
unsigned char kernel_getin(void *ptr, unsigned char size)
{
  unsigned char * data = (unsigned char *)ptr;
  unsigned char i;
  unsigned char st=0;
  
  for(i=0; i<size; ++i)
    {
      st = cbm_k_readst();
      if (st) {
	break;
      }
      data[i] = cbm_k_getin();
    }
  data[i] = '\0';
  size = i;
  return st;
}

int main(void)
{
  
  
  // Open file for reading

  cbm_k_clall();
  cbm_k_setlfs(3,8,0);
  cbm_k_setnam(filename);
  cbm_k_open();
  if(cbm_k_readst()) {
    printf("ERROR: Could not read file '%s'\n",filename);
    return 0;
  }

  // Read and check the header
  cbm_k_chkin(3);
  kernel_getin(header,64);
  if(cbm_k_readst()) {
    printf("ERROR: Could not read header.\n");
    return 0;
  }

  // Check header
  for(i=0;i<16;i++) {
    if (header[i]!=header_magic[i]) {
      printf("ERROR: File header is incorrect for a .CRT file\n");
      return 0;
    }
  }

  // Get cartridge name
  for(i=0;i<32;i++) cartname[i]=header[32+i];
  cartname[32]=0;
  printf("Examining cartridge:\n  %s\n",cartname);

  // Check cartridge type etc
  offset=
    header[0x13]
    +(((unsigned long)header[0x12])<<8L)
    +(((unsigned long)header[0x11])<<16L)
    +(((unsigned long)header[0x10])<24L);

  if (offset!=0x40) {
    printf("WARNING: CRT file has strange header length.\n");
  }
  // Subtract length of header we have already read
  offset-=0x40;

  if (header[0x14]!=0x01||header[0x15]!=0x00) {
    printf("WARNING: CRT file version >01.00.\n");
  }

  cart_type=(header[0x16]<<8)+header[0x17];
  switch(cart_type) {
  case 1: // normal
  case 5: // Ocean Type 1
    break;
  default:
    printf("WARNING: Cartridge type $%04x unsupported.  Most likely won't work.\n", cart_type);
  }

  exrom=header[0x18];
  game=header[0x19];
  
  // Skip any extra header bytes
  while(offset) {
    cbm_k_chkin(3);
    if (offset>16384) {
      kernel_getin(buffer,16384);
      offset-=16384;
    } else {
      kernel_getin(buffer,offset);
      offset=0;
    }
      
  }

  while(!cbm_k_readst()) {
    // Look for next CHIP block
    cbm_k_chkin(3);
    kernel_getin(header,16);
    if (b!=1) break;
    // Check for CHIP magic string
    if (header[0]!=0x43||header[1]!=0x48||header[2]!=0x49||header[3]!=0x50) break;
    section_len=(((unsigned long)header[0x4])<<24)|(((unsigned long)header[0x5])<<16)|(header[0x6]<<8)|(header[0x7]);
    chip_type=(header[8]<<8)+header[9];
    bank_num=(header[0xa]<<8)+header[0xb];
    load_addr=(header[0xc]<<8)+header[0xd];
    rom_size=(header[0xe]<<8)+header[0xf];
    printf("CHIP: type=$%04x, bank=$%04x, addr=$%04x, size=$%04x\n",
	   chip_type,bank_num,load_addr,rom_size);
    if (chip_type) {
      printf("WARNING: Skipping non-ROM bank.\n");
    }
    section_len-=0x10;
    if (section_len>16384||rom_size>16384) {
      printf("ERROR: CHIP block is >16KB.\n");
      return 0;
    }
    // Load ROM data into buffer
    cbm_k_chkin(3);
    kernel_getin(buffer,section_len);

    // Then DMA into place
    lcopy((unsigned long)&buffer,0x40000L+(bank_num*8192),rom_size);
    
  }

  printf("Setting up cartridge simulator...\n");
  POKE(0xD7FE,0x02); // Enable Ocean Type 1 cart simulation
  POKE(0xD7FB,0x01); // Disable physical cartridges
  // Set /GAME and /EXROM appropriately, and keep power on for
  // handheld version.
  POKE(0xD7FD,(exrom?0x00:0x80)|(game?0x00:0x40)+0x01);
  // Jump into cartridge
  asm("jmp ($8000)");
}
