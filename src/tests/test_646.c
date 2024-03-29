/*
  Multi-sector writes were not writing the last sector (command $06), but
  instead the bytes of the sector buffer were accidently interpreted as
  SD controller commands, which could lead to random behaviour. Usually,
  the SD card gets stuck in busy status at some point.

  This routine tests writing multiple batches sequential sectors using
  multi-sector write commands. It saves the overwritten sectors first
  and restores them with single sector write commands at the end.

  Without the fix for this issue in place, there is a chance the SD
  card gets stuck in busy status. This is detected via a timeout
  and reported as failed test, as well.
*/
#define ISSUE_NUM 646
#define ISSUE_NAME "multi-sector sd card write omits last sector"

#include <stdio.h>
#include <stdint.h>

#include <memory.h>
#include <tests.h>

char msg[80 + 1];

int8_t test_status = -1;
uint32_t start_sector = 0;
static const uint8_t num_batches = 10;
static const uint8_t num_sectors_per_batch = 5;
static uint8_t sector_buffer[512];

void init(void)
{
  // Fast CPU, M65 IO
  mega65_io_enable();

  // black screen
  POKE(0xD020, 0);
  POKE(0xD021, 0);

  POKE(0xD689, PEEK(0xD689) | 128); // Enable SD buffer instead of floppy buffer
}

void wait_for_sd_ready()
{
  uint8_t timeout_frames = 100;
  uint8_t current_frame, last_frame = PEEK(0xD7FA);
  // Both busy flags in bits 0 and 1 need to be cleared
  while (PEEK(0xD680) & 0x03) {
    POKE(0xD020, PEEK(0xD020) + 1);
    current_frame = PEEK(0xD7FA);
    if (current_frame != last_frame) {
      last_frame = current_frame;
      --timeout_frames;
      if (timeout_frames == 0) {
        test_status = 3;
        return;
      }
    }
  }
  return;
}

void get_slot_zero_start_sector()
{
  *(uint32_t *)0xD681 = 0;
#pragma optimize(push, off)
  __asm__("ldx #$00");
  __asm__("ldy #$00");
  __asm__("lda #$10");
  __asm__("sta $d642");
  __asm__("clv");
#pragma optimize(pop)

  start_sector = *(uint32_t *)0xD681;
}

void backup_sectors()
{
  uint32_t cur_sector = start_sector;
  uint8_t sectors_left = num_batches * num_sectors_per_batch;
  uint32_t backup_address = 0x40000UL;

  wait_for_sd_ready();
  if (test_status == 3)
    return;
  while (sectors_left > 0) {
    *(uint32_t *)0xD681 = cur_sector;
    POKE(0xD680, 0x02);
    wait_for_sd_ready();
    if (test_status == 3)
      return;
    lcopy(0xffd6e00, backup_address, 512);
    backup_address += 512;
    ++cur_sector;
    --sectors_left;
  }
  wait_for_sd_ready();
  if (test_status == 3)
    return;
}

void restore_sectors()
{
  uint32_t cur_sector = start_sector;
  uint8_t sectors_left = num_batches * num_sectors_per_batch;
  uint32_t backup_address = 0x40000UL;

  while (sectors_left > 0) {
    wait_for_sd_ready();
    if (test_status == 3)
      return;
    lcopy(backup_address, 0xffd6e00, 512);
    *(uint32_t *)0xD681 = cur_sector;
    POKE(0xD680, 0x57);
    POKE(0xD680, 0x03);
    backup_address += 512;
    ++cur_sector;
    --sectors_left;
  }
  wait_for_sd_ready();
  if (test_status == 3)
    return;
}

void multi_sector_write()
{
  uint8_t data_byte = 0xa1;
  uint8_t batches_left = num_batches;
  uint8_t sectors_left = num_sectors_per_batch;
  uint8_t cmd = 4;
  uint32_t batch_start_sector = start_sector;

  wait_for_sd_ready();
  if (test_status == 3)
    return;
  while (batches_left > 0) {
    *(uint32_t *)0xD681 = batch_start_sector;
    while (sectors_left > 0) {
      if (sectors_left == num_sectors_per_batch - 1) {
        cmd = 5;
      }
      else if (sectors_left == 1) {
        cmd = 6;
      }
      lfill(0xffd6e00, data_byte, 512);
      POKE(0xD680, 0x57);
      POKE(0xD680, cmd);
      wait_for_sd_ready();
      if (test_status == 3)
        return;
      ++data_byte;
      --sectors_left;
    }
    cmd = 4;
    --batches_left;
    sectors_left = num_sectors_per_batch;
    batch_start_sector += num_sectors_per_batch;
  }
}

void verify_sectors_written()
{
  uint16_t i;
  uint8_t data_byte = 0xa1;
  uint32_t cur_sector = start_sector;
  uint8_t sectors_left = num_batches * num_sectors_per_batch;
  uint8_t *ptr;

  wait_for_sd_ready();
  if (test_status == 3)
    return;
  while (sectors_left > 0) {
    *(uint32_t *)0xD681 = cur_sector;
    POKE(0xD680, 0x02);
    wait_for_sd_ready();
    if (test_status == 3)
      return;
    lcopy(0xffd6e00, (uint32_t)sector_buffer, 512);
    ptr = sector_buffer;
    for (i = 0; i < 512; ++i) {
      if (*ptr != data_byte) {
        test_status = 1;
        return;
      }
      ++ptr;
    }
    ++data_byte;
    ++cur_sector;
    --sectors_left;
  }
  wait_for_sd_ready();
  if (test_status == 3)
    return;
  test_status = 0;
}

void main(void)
{
  init();
  printf("%cIssue #%d - %s\n\n", 0x93 /*clrscr*/, ISSUE_NUM, ISSUE_NAME);

  unit_test_setup(ISSUE_NAME, ISSUE_NUM);

  printf("Get freeze slot 0 sector from Hyppo\n");
  get_slot_zero_start_sector();
  if (start_sector == 0) {
    test_status = 2;
  }
  else {
    printf("Backing up sectors\n");
    backup_sectors();
    if (test_status != 3) {
      printf("Writing sectors\n");
      multi_sector_write();
    }
    if (test_status != 3) {
      printf("Verifing data\n");
      verify_sectors_written();
    }
    if (test_status != 3) {
      printf("Restoring sectors\n");
      restore_sectors();
      printf("Done\n");
    }
  }

  switch (test_status) {
  case 0:
    snprintf(msg, 80, "multi-sector write working as expected");
    unit_test_ok(msg);
    break;
  case 1:
    snprintf(msg, 80, "error in multi-sector write operation");
    unit_test_fail(msg);
    break;
  case 2:
    snprintf(msg, 80, "error getting freeze slot #0 sector");
    unit_test_fail(msg);
    break;
  case 3:
    snprintf(msg, 80, "sd card stuck in busy state");
    unit_test_fail(msg);
    break;
  default:
    snprintf(msg, 80, "internal error in test case");
    unit_test_fail(msg);
  }

  printf("\nResult:\n%s\n", msg);

  unit_test_report(ISSUE_NUM, 0, TEST_DONEALL);
}
