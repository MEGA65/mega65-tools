#include "gtest/gtest.h"
#include <stdarg.h>
#include <stdio.h>

int parse_command(const char* str, const char* format, ...);
int upload_file(char* name, char* dest_name);
int delete_file(char* name);
int download_file(char* dest_name, char* local_name, int showClusters);
int is_fragmented(char* filename);

#define SECTOR_SIZE 512
#define MBR_SIZE SECTOR_SIZE
#define SECTORS_PER_CLUSTER 8
#define CLUSTER_SIZE (SECTOR_SIZE * SECTORS_PER_CLUSTER)
#define PARTITION1_CLUSTER_COUNT 16
#define PARTITION1_START (1 * SECTOR_SIZE)
#define PARTITION1_SIZE (PARTITION1_CLUSTER_COUNT * CLUSTER_SIZE)
#define SDSIZE (MBR_SIZE + PARTITION1_SIZE)

unsigned char sdcard[SDSIZE] = { 0 };

void init_sdcard_data(void)
{
  // MBR
  // ===
  // - Describe the 1st (and only) partition
  sdcard[0x1be] = 0x80;                                                              // active/bootable partition
  sdcard[0x1c2] = 0x0b;                                                              // fat32 (chs)
  *((unsigned int*)&sdcard[0x1c6]) = 0x01;                                           // first sector in partition
  *((unsigned int*)&sdcard[0x1ca]) = PARTITION1_CLUSTER_COUNT * SECTORS_PER_CLUSTER; // sector count

  // PARTITION1
  // ==========
  // Boot Sector (Paul refers to this as the FAT MBR)
  // -----------
  sdcard[PARTITION1_START + 0x1fe] = 0x55; // magic marker
  sdcard[PARTITION1_START + 0x1ff] = 0xaa;
  sdcard[PARTITION1_START + 0x0c] = 0x02; // bytes per logical sector (0x200 = 512 bytes)
  sdcard[PARTITION1_START + 0x0e] = 0x01; // no# of reserved sectors (includes bootsector + fsinfo_sector)
  sdcard[PARTITION1_START + 0x10] = 0x02; // number of FATs (2)
  sdcard[PARTITION1_START + 0x0d] = 0x08; // sectors per cluster (8)
  *((unsigned int*)&sdcard[PARTITION1_START + 0x20]) = PARTITION1_SIZE / SECTOR_SIZE; // total logical sectors
  *((unsigned int*)&sdcard[PARTITION1_START + 0x24]) = 1;                             // sectors per fat
  *((unsigned int*)&sdcard[PARTITION1_START + 0x2c]) = 2;                             // Cluster no# of root-directory start

  // NOTE: For now, I'll ignore the fsinfo_sector, pretend it doesn't exist, and see if I get away with it

  // 2 FAT TABLES
  // ============
  int offset = PARTITION1_START + SECTOR_SIZE;
  for (int k = 0; k < 2; k++) {
    sdcard[offset + 0x00] = 0xf8;
    sdcard[offset + 0x01] = 0xff;
    sdcard[offset + 0x02] = 0xff;
    sdcard[offset + 0x03] = 0x0f;

    sdcard[offset + 0x04] = 0xff;
    sdcard[offset + 0x05] = 0xff;
    sdcard[offset + 0x06] = 0xff;
    sdcard[offset + 0x07] = 0xff;

    // jump to next sector / fat table and repeat this data
    offset += SECTOR_SIZE;
  }

  // DIRECTORY ENTRIES
  // =================
  // volume label
  offset = PARTITION1_START + 3 * SECTOR_SIZE;
  sdcard[offset + 0x00] = 'M';
  sdcard[offset + 0x01] = 'E';
  sdcard[offset + 0x02] = 'G';
  sdcard[offset + 0x03] = 'A';
  sdcard[offset + 0x04] = '6';
  sdcard[offset + 0x05] = '5';
  sdcard[offset + 0x06] = ' ';
  sdcard[offset + 0x07] = ' ';
  sdcard[offset + 0x08] = ' ';
  sdcard[offset + 0x09] = ' ';
  sdcard[offset + 0x0a] = ' ';
  sdcard[offset + 0x0b] = 0x08; // file attribute (0x08 = volume label)
}

// my read/write sector mock functions
// (I probably ought to put them in a separate file)
int read_sector(const unsigned int sector_number, unsigned char* buffer, int useCache, int readAhead)
{
  if (sector_number >= SDSIZE / 512)
    return -1;

  for (int k = 0; k < SECTOR_SIZE; k++) {
    buffer[k] = sdcard[sector_number * SECTOR_SIZE + k];
  }
  return 0;
}

int write_sector(const unsigned int sector_number, unsigned char* buffer)
{
  if (sector_number >= SDSIZE / 512)
    return -1;

  for (int k = 0; k < SECTOR_SIZE; k++) {
    sdcard[sector_number * SECTOR_SIZE + k] = buffer[k];
  }
  return 0;
}

// my tests
namespace mega65_ftp {

TEST(Mega65FtpTest, ParseSimplePutCommands)
{
  char src[1024];
  char dst[1024];
  int ret = parse_command("put test.d81 TEST.D81", "put %s %s", src, dst);
  ASSERT_EQ(2, ret);
  EXPECT_STREQ("test.d81", src);
  EXPECT_STREQ("TEST.D81", dst);
}

TEST(Mega65FtpTest, ParsePutCommandWithDoubleQuotedSpaces)
{
  char src[1024];
  char dst[1024];
  int ret = parse_command("put \"my test file.d81\" TEST.D81", "put %s %s", src, dst);
  ASSERT_EQ(2, ret);
  EXPECT_STREQ("my test file.d81", src);
  EXPECT_STREQ("TEST.D81", dst);
}

TEST(Mega65FtpTest, ParseSectorCommandWithNumericParam)
{
  int num;
  int ret = parse_command("sector 123", "sector %d", &num);
  ASSERT_EQ(1, ret);
  EXPECT_EQ(123, num);
}

TEST(Mega65FtpTest, DummyCommandWithNumericAndString)
{
  int num;
  char str[1024];
  int ret = parse_command("test ABC 123", "test %s %d", str, &num);
  ASSERT_EQ(2, ret);
  EXPECT_STREQ("ABC", str);
  EXPECT_EQ(123, num);
}

TEST(Mega65FtpTest, DummyCommandWithStringAndNumeric)
{
  int num;
  char str[1024];
  int ret = parse_command("test 123 ABC", "test %d %s", &num, str);
  ASSERT_EQ(2, ret);
  EXPECT_EQ(123, num);
  EXPECT_STREQ("ABC", str);
}

TEST(Mega65FtpTest, GetCommandExpectTwoParamGivenOne)
{
  char strSrc[1024];
  char strDest[1024];
  int ret = parse_command("get TEST.D81", "get %s %s", &strSrc, &strDest);
  ASSERT_EQ(1, ret);
  ASSERT_STREQ("TEST.D81", strSrc);
}

void generate_dummy_file(const char* name, int size)
{
  FILE* f = fopen(name, "wb");
  for (int i = 0; i < size; i++) {
    fputc(i % 256, f);
  }
  fclose(f);
}

void delete_local_file(const char* name)
{
  remove(name);
}

class Mega65FtpTestFixture : public ::testing::Test {
  protected:
  char* file4kb = "4kbtest.tmp";
  char* file8kb = "8kbtest.d81";

  void SetUp() override
  {
    // suppress the chatty output
    ::testing::internal::CaptureStderr();
    ::testing::internal::CaptureStdout();

    generate_dummy_file(file4kb, 4096);
    generate_dummy_file(file8kb, 8192);
  }

  void TearDown() override
  {
    testing::internal::GetCapturedStderr();
    testing::internal::GetCapturedStdout();

    // cleanup
    delete_local_file(file4kb);
    delete_local_file(file8kb);
  }
};

TEST_F(Mega65FtpTestFixture, PutCommandWritesFileToContiguousClusters)
{
  init_sdcard_data();

  // sector = 512 bytes
  // cluster = 8 sectors = 8 x 512 = 4096 bytes (4kb)

  // upload three 4kb files, consuming the clusters this way:
  //
  // CLUSTER       2          3          4          5          6      ...
  //         +----------+----------+----------+----------+----------+-----
  //         | 4KB1.TMP | 4KB2.TMP | 4BK3.TMP |    ---   |    ---   | ...
  //         +----------+----------+----------+----------+----------+-----
  upload_file(file4kb, "4kb1.tmp");
  upload_file(file4kb, "4kb2.tmp");
  upload_file(file4kb, "4kb3.tmp");

  // make a single cluster gap in the fat table
  // CLUSTER       2          3          4          5          6      ...
  //         +----------+----------+----------+----------+----------+-----
  //         | 4KB1.TMP |    ---   | 4BK3.TMP |    ---   |    ---   | ...
  //         +----------+----------+----------+----------+----------+-----
  delete_file("4kb2.tmp");

  // write a two cluster file
  // CLUSTER       2          3          4          5          6      ...
  //         +----------+----------+----------+----------+----------+-----
  //         | 4KB1.TMP |    ---   | 4BK3.TMP |     8KBTEST.D81     | ...
  //         +----------+----------+----------+----------+----------+-----
  upload_file(file8kb, file8kb);

  ASSERT_EQ(0, is_fragmented("4kb1.tmp"));
  ASSERT_EQ(0, is_fragmented("4kb3.tmp"));
  ASSERT_EQ(0, is_fragmented(file8kb));
}

} // namespace mega65_ftp
