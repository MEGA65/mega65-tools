/*
  Remote SD card access tool for mega65_ftp to more quickly
  send and receive files.

  It implements a simple protocol with pre-emptive sending
  of read data in raw mode at 4mbit = 40KB/sec.  Can do this
  while writing jobs to the SD card etc to hide latency.

*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>
#include <debug.h>
#include <random.h>

// Set to 1 to show debug messages
#define DEBUG 0

// Slight delay between chars for old HYPPO versions that lack ready check on serial write
#define SERIAL_DELAY
//#define SERIAL_DELAY for(aa=0;aa!=5;aa++) continue;
uint16_t aa;
// Write a char to the serial monitor interface
#define SERIAL_WRITE(the_char)                                                                                              \
  {                                                                                                                         \
    __asm__("LDA %v", the_char);                                                                                            \
    __asm__("STA $D643");                                                                                                   \
    __asm__("NOP");                                                                                                         \
  }

void press_key(void)
{
  POKE(0xD610, 0);
  // wait for a key to be pressed
  printf("press a key to continue...\n");
  while (!PEEK(0xD610))
    continue;
  POKE(0xD610, 0);
}

uint8_t c, j, z, pl, job_type, pause_flag = 0, xemu_flag = 0;
uint16_t i, job_type_addr;
char filename[65];

void check_xemu_flag(void)
{
  if (!(PEEK(0xD60F) & 0x20)) {
    printf("Xemu detected!\n");
    xemu_flag = 1;
  }
}

void serial_write_string(uint8_t* m, uint16_t len)
{

  for (i = 0; i < len; i++) {
    SERIAL_DELAY;
    c = *m;
    m++;
    SERIAL_DELAY;
    SERIAL_WRITE(c);
    // printf("%02X", c);
    if (pause_flag) {
      //#if DEBUG > 1
      if (i < 368) {
        printf("%02X", c);
        if (((i + 5) % 4) == 0)
          printf(" ");
        if (((i + 17) % 16) == 0)
          printf("\n");
      }
      //#endif
    }
    if (pause_flag)
      press_key();
  }

#if DEBUG > 1
  press_key();
#endif
}

uint16_t last_advertise_time, current_time;
uint8_t job_count = 0;
uint16_t job_addr;

char msg[80 + 1];

uint32_t buffer_address, sector_number, transfer_size;

uint8_t temp_sector_buffer[0x200];

uint8_t rle_count = 0, a;
uint8_t olen = 0, iofs = 0, last_value = 0;
uint8_t obuf[0x80];
uint8_t local_buffer[256];

uint8_t buffer_ready = 0;
uint16_t sector_count = 0;
uint8_t read_pending = 0;

void rle_init(void)
{
  olen = 0;
  iofs = 0;
  last_value = 0xFF;
  rle_count = 0;
}

uint8_t block_len = 0;
uint8_t bo = 0;

void rle_write_string(uint32_t buffer_address, uint32_t transfer_size)
{
  lcopy(buffer_address, (uint32_t)local_buffer, 256);

  POKE(0xD610, 0);
  while (transfer_size) {

    // Use tight inner loop to send more quickly
    if (transfer_size & 0x7f)
      block_len = transfer_size & 0x7f;
    else
      block_len = 0x80;

    //     printf("transfer_size=%d, block_len=%d,ba=$%x\n",transfer_size,block_len,buffer_address);

    transfer_size -= block_len;

    for (bo = 0; bo < block_len; bo++) {
#if DEBUG > 1
      // wait for a key to be pressed
      printf("olen=%d, rle_count=%d, last=$%02x, byte=$%02x\n", olen, rle_count, last_value, local_buffer[iofs]);
      while (!PEEK(0xD610))
        continue;
      POKE(0xD610, 0);
#endif
      if (olen == 127) {
        // Write out 0x00 -- 0x7F as length of non RLE bytes,
        // followed by the bytes
#if DEBUG > 1
        printf("$%02x raw\n", olen);
#endif
        SERIAL_WRITE(olen);
        for (a = 0; a < 0x80; a++) {
          c = obuf[a];
          SERIAL_DELAY;
          SERIAL_WRITE(c);
        }
        olen = 0;
        rle_count = 0;
      }

      if (rle_count == 127) {
        // Flush a full RLE buffer
#if DEBUG > 1
        printf("$%02x x $%02x\n", rle_count, last_value);
#endif
        c = 0x80 | rle_count;
        SERIAL_WRITE(c);
        SERIAL_DELAY;
        SERIAL_WRITE(last_value);
#if DEBUG > 1
        printf("Wrote $%02x, $%02x\n", c, last_value);
#endif
        rle_count = 0;
      }
      obuf[olen++] = local_buffer[iofs];
      if (local_buffer[iofs] == last_value) {
        rle_count++;
        if (rle_count == 3) {
          // Switch from raw to RLE, flushing any pending raw bytes
          if (olen > 3)
            olen -= 3;
          else
            olen = 0;
#if DEBUG > 1
          printf("Flush $%02x raw %02x %02x %02x ...\n", olen, obuf[0], obuf[1], obuf[2]);
#endif
          if (olen) {
            SERIAL_WRITE(olen);
            for (a = 0; a < olen; a++) {
              c = obuf[a];
              SERIAL_DELAY;
              SERIAL_WRITE(c);
            }
          }
          olen = 0;
        }
        else if (rle_count < 3) {
          // Don't do anything yet, as we haven't yet flipped to RLE coding
        }
        else {
          // rle_count>3, so keep accumulating RLE data
          olen--;
        }
      }
      else {
        // Flush any accumulated RLE data
        if (rle_count > 2) {
#if DEBUG > 1
          printf("$%02x x $%02x\n", rle_count, last_value);
#endif
          c = 0x80 | rle_count;
          SERIAL_WRITE(c);
          SERIAL_DELAY;
          SERIAL_WRITE(last_value);
        }
        // 1 of the new byte seen
        rle_count = 1;
      }

      last_value = local_buffer[iofs];

      // Advance and keep buffer primed
      if (iofs == 0xff) {
        buffer_address += 256;
        lcopy(buffer_address, (long)&local_buffer[0], 256);

        iofs = 0;
      }
      else
        iofs++;
    }
  }
}

void rle_finalise(void)
{
  // Flush any accumulated RLE data
  if (rle_count > 2) {
#if DEBUG > 1
    printf("Terminal $%02x x $%02x\n", rle_count, last_value);
#endif
    c = 0x80 | rle_count;
    SERIAL_WRITE(c);
    SERIAL_DELAY;
    SERIAL_WRITE(last_value);
  }
  else if (olen) {
#if DEBUG > 1
    printf("Terminal flush $%02x raw %02x %02x %02x ...\n", olen, obuf[0], obuf[1], obuf[2]);
#endif
    SERIAL_WRITE(olen);
    for (a = 0; a < olen; a++) {
      c = obuf[a];
      SERIAL_DELAY;
      SERIAL_WRITE(c);
    }
  }
}

void wait_for_sdcard_to_go_busy(void)
{
  if (xemu_flag) {
    // Pause a little here, to permit xemu to echo the last command back to mega65_ftp.
    // This need came about due to xemu presently completing SD card jobs instantaneously,
    // and thus never goes busy.
    for (aa = 0; aa < 20; aa++)
      continue;
  }
  else {
    while (!(PEEK(0xD680) & 0x03))
      continue;
  }
}

#pragma optimize(off)
void mount_file(void)
{
  printf("Mounting \"%s\"...\n", filename);
  strcpy((char*)0x0400, filename);
  *((char*)0x400 + strlen(filename)) = 0x00;

  // Call dos_setname()
  __asm__("LDY #$04");
  __asm__("LDX #$00");

  __asm__("LDA #$2E");
  __asm__("STA $D640");
  __asm__("NOP");

  // Try to attach it
  __asm__("LDA #$40");
  __asm__("STA $D640");
  __asm__("NOP");
}
#pragma optimize(on)

void main(void)
{
  unsigned char jid;

  asm("sei");

  // Fast CPU, M65 IO
  POKE(0, 65);
  POKE(0xD02F, 0x47);
  POKE(0xD02F, 0x53);

  // Cursor off
  POKE(204, 0x80);

  printf("%cMEGA65 File Transfer helper.\n", 0x93);

  check_xemu_flag();

  last_advertise_time = 0;

  // Clear communications area
  lfill(0xc000, 0x00, 0x1000);
  lfill(0xc001, 0x42, 0x1000);

  while (1) {
    current_time = *(uint16_t*)0xDC08;
    if (current_time != last_advertise_time) {
      // Announce our protocol version
      // NOTE: Despite this constant serial-comms being incredibly spammy and annoying,
      // hold back your temptation to remove/comment this line, as mega65_ftp relies on it
      // in order to assess if remotesd is presently running on the hardware.
      // Turns out this is detection is a behaviour that M65Connect relies on.
      serial_write_string("\nmega65ft1.0\n\r", 14);
      last_advertise_time = current_time;
    }

    if (PEEK(0xC000)) {
      // Perform one or more jobs
      job_count = PEEK(0xC000);

      // pause a little here, to permit xemu to echo the last command back to mega65_ftp
      if (xemu_flag) {
        for (aa = 0; aa < 30000; aa++)
          continue;
      }

#if DEBUG
      printf("\n\n\nReceived list of %d jobs.\n", job_count);
#endif
      job_addr = 0xc001;
      for (jid = 0; jid < job_count; jid++) {
        if (job_addr > 0xcfff)
          break;
        job_type_addr = job_addr;
        job_type = PEEK(job_type_addr);
        // printf("job_type=0x%02x\n", job_type);
        switch (job_type) {

        // - - - - - - - - - - - - - - - - - - - - -
        // ??
        // - - - - - - - - - - - - - - - - - - - - -
        case 0x00:
          job_addr++;
          break;

        // - - - - - - - - - - - - - - - - - - - - -
        // Terminate
        // - - - - - - - - - - - - - - - - - - - - -
        case 0xFF:
          __asm__("jmp 58552");
          break;

        // - - - - - - - - - - - - - - - - - - - - -
        // Write sector
        // - - - - - - - - - - - - - - - - - - - - -
        case 0x02:
        case 0x05: // multi write first
        case 0x06: // multi write middle
        case 0x07: // multi write end
          job_addr++;
          buffer_address = *(uint32_t*)job_addr;
          job_addr += 4;
          sector_number = *(uint32_t*)job_addr;
          job_addr += 4;

#if DEBUG > 0
          printf("$%04x : write sector $%08lx from mem $%07lx\n", *(uint16_t*)0xDC08, sector_number, buffer_address);
#endif

          //	  lcopy(buffer_address,0x0400,512);
          lcopy(buffer_address, 0xffd6e00, 512);

          // Write sector
          *(uint32_t*)0xD681 = sector_number;
          POKE(0xD680, 0x57); // Open write gate
          if (job_type == 0x02)
            POKE(0xD680, 0x03);
          if (job_type == 0x05)
            POKE(0xD680, 0x04);
          if (job_type == 0x06)
            POKE(0xD680, 0x05);
          if (job_type == 0x07)
            POKE(0xD680, 0x06);

          wait_for_sdcard_to_go_busy();

          // Wait for SD to read and fiddle border colour to show we are alive
          while (PEEK(0xD680) & 0x03)
            POKE(0xD020, PEEK(0xD020) + 1);

          break;

        // - - - - - - - - - - - - - - - - - - - - -
        // Read sector
        // - - - - - - - - - - - - - - - - - - - - -
        case 0x01:
          job_addr++;
          buffer_address = *(uint32_t*)job_addr;
          job_addr += 4;
          sector_number = *(uint32_t*)job_addr;
          job_addr += 4;

#if DEBUG
          printf("$%04x : Read sector $%08lx into mem $%07lx\n", *(uint16_t*)0xDC08, sector_number, buffer_address);
#endif
          // Do read
          *(uint32_t*)0xD681 = sector_number;
          POKE(0xD680, 0x02);

          wait_for_sdcard_to_go_busy();

          // Wait for SD to read and fiddle border colour to show we are alive
          while (PEEK(0xD680) & 0x03)
            POKE(0xD020, PEEK(0xD020) + 1);

          lcopy(0xffd6e00, buffer_address, 0x200);

          snprintf(msg, 80, "ftjobdone:%04x:\n\r", job_type_addr);
          serial_write_string(msg, strlen(msg));

#if DEBUG
          printf("$%04x : Read sector done\n", *(uint16_t*)0xDC08);
#endif

          break;

        // - - - - - - - - - - - - - - - - - - - - -
        // Read sectors and stream
        // - - - - - - - - - - - - - - - - - - - - -
        case 0x03: // 0x03 == with RLE
        case 0x04: // 0x04 == no RLE
          job_addr++;
          sector_count = *(uint16_t*)job_addr;
          job_addr += 2;
          sector_number = *(uint32_t*)job_addr;
          job_addr += 4;

#if DEBUG > 1
          printf("sector_count = %d\n", sector_count);
          printf("sector_number = $%08lx (%ld)\n", sector_number, sector_number);
          press_key();
#endif

          // Begin with no bytes to send
          buffer_ready = 0;
          // and no sector in progress being read
          read_pending = 0;

          // Reset RLE state
          rle_init();

          if (job_type == 3)
            snprintf(msg, 80, "ftjobdata:%04x:%08lx:", job_type_addr, sector_count * 0x200L);
          else
            snprintf(msg, 80, "ftjobdatr:%04x:%08lx:", job_type_addr, sector_count * 0x200L);
#if DEBUG > 1
          printf("%s\n", msg);
          press_key();
#endif
          serial_write_string(msg, strlen(msg));

          while (sector_count || buffer_ready || read_pending) {
            if (sector_count && (!read_pending)) {
              // if sd-card is not busy, then do this stuff
              if (!(PEEK(0xD680) & 0x03)) {
                // Schedule reading of next sector

                POKE(0xD020, PEEK(0xD020) + 1);

                // Do read
                *(uint32_t*)0xD681 = sector_number;

                read_pending = 1;
                sector_count--;

                POKE(0xD680, 0x02);

                wait_for_sdcard_to_go_busy();

                sector_number++;
              }
            }
            if (read_pending && (!buffer_ready)) {

              // Read is complete, now queue it for sending back
              if (!(PEEK(0xD680) & 0x03)) {
                // Sector has been read. Copy it to a local buffer for sending,
                // so that we can send it while reading the next sector
                lcopy(0xffd6e00, (long)&temp_sector_buffer[0], 0x200);
                // if (sector_number-1 == 2623)
                //{
                //  //pause_flag = 1;
                //  for (z = 0; z < 16*8; z++)
                //  {
                //    if ( (z % 8) == 0)
                //      printf(" ");
                //    if ( (z % 16) == 0 )
                //      printf("\n");
                //    printf("%02X", temp_sector_buffer[z]);
                //  }
                //}

                read_pending = 0;
                buffer_ready = 1;
              }
            }
            if (buffer_ready) {
              // XXX - Just send it all in one go, since we don't buffer multiple
              // sectors
              if (job_type == 3)
                rle_write_string((uint32_t)temp_sector_buffer, 0x200);
              else
                serial_write_string(temp_sector_buffer, 0x200);
              buffer_ready = 0;
              pause_flag = 0;
            }
          } // end while we have sectors to send

          if (job_type == 3)
            rle_finalise();

#if DEBUG
          sector_number = *(uint32_t*)(job_addr - 4);
          sector_count = *(uint16_t*)(job_addr - 6);
          printf("$%04x : Completed read sector $%08lx (count=%d)\n", *(uint16_t*)0xDC08, sector_number, sector_count);
#endif

          snprintf(msg, 80, "ftjobdone:%04x:\n\r", job_type_addr);
          serial_write_string(msg, strlen(msg));

#if DEBUG
          printf("$%04x : Read sector done\n", *(uint16_t*)0xDC08);
#endif

          break;

        // - - - - - - - - - - - - - - - - - - - - -
        // Send block of memory
        // - - - - - - - - - - - - - - - - - - - - -
        case 0x11:
          job_addr++;
          buffer_address = *(uint32_t*)job_addr;
          job_addr += 4;
          transfer_size = *(uint32_t*)job_addr;
          job_addr += 4;

#if DEBUG
          printf("$%04x : Send mem $%07lx to $%07lx: %02x %02x ...\n", *(uint16_t*)0xDC08, buffer_address,
              buffer_address + transfer_size - 1, lpeek(buffer_address), lpeek(buffer_address + 1));
#endif

          snprintf(msg, 80, "ftjobdata:%04x:%08lx:", job_type_addr, transfer_size);
          serial_write_string(msg, strlen(msg));

          // Set up buffers
          rle_init();
          rle_write_string(buffer_address, transfer_size);
          rle_finalise();

          snprintf(msg, 80, "ftjobdone:%04x:\n\r", job_type_addr);
          serial_write_string(msg, strlen(msg));

#if DEBUG
          printf("$%04x : Send mem done\n", *(uint16_t*)0xDC08);
#endif

          break;

        // - - - - - - - - - - - - - - - - - - - - -
        // Mount a disk image
        // - - - - - - - - - - - - - - - - - - - - -
        case 0x12:
          job_addr++;
          strcpy(filename, (char*)job_addr);
          mount_file();
          break;

          // - - - - - - - - - - - - - - - - - - - - -

        default:
          job_addr = 0xd000;
          break;
        }
      }

      // Indicate when we think we are all done
      POKE(0xC000, 0);
      snprintf(msg, 80, "ftbatchdone\n");
      serial_write_string(msg, strlen(msg));

#if DEBUG
      printf("$%04x : Sending batch done\n", *(uint16_t*)0xDC08);
#endif
    }
  }
}
