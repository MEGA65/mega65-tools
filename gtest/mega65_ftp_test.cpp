#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include <stdarg.h>
#include <stdio.h>

int parse_command(const char* str, const char* format, ...);
int upload_file(char* name, char* dest_name);
int rename_file_or_dir(char* name, char* dest_name);
int delete_file(char* name);
int download_file(char* dest_name, char* local_name, int showClusters);
int open_file_system(void);
int contains_file_or_dir(char* name);
int is_fragmented(char* filename);
int create_dir(char*);
int show_directory(char* path);
void change_dir(char* path);

#define SECTOR_SIZE 512
#define MBR_SIZE SECTOR_SIZE
#define SECTORS_PER_CLUSTER 8
#define SECTORS_PER_FAT 2
#define CLUSTER_SIZE (SECTOR_SIZE * SECTORS_PER_CLUSTER)
#define PARTITION1_CLUSTER_COUNT 200
#define PARTITION1_START (1 * SECTOR_SIZE)
#define PARTITION1_SIZE (PARTITION1_CLUSTER_COUNT * CLUSTER_SIZE)
#define SDSIZE (MBR_SIZE + PARTITION1_SIZE)

unsigned char sdcard[SDSIZE] = { 0 };

void init_sdcard_data(void)
{
  memset(sdcard, 0, SDSIZE);

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
  *((unsigned int*)&sdcard[PARTITION1_START + 0x24]) = SECTORS_PER_FAT;               // sectors per fat
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

    sdcard[offset + 0x08] = 0xf8; // this is for cluster#2 (root dir - dir entries)
    sdcard[offset + 0x09] = 0xff;
    sdcard[offset + 0x0a] = 0xff;
    sdcard[offset + 0x0b] = 0x0f;

    // jump to next sector / fat table and repeat this data
    offset += SECTOR_SIZE*SECTORS_PER_FAT;
  }

  // DIRECTORY ENTRIES
  // =================
  // volume label
  offset = PARTITION1_START + (1 + SECTORS_PER_FAT*2) * SECTOR_SIZE;
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

void dump_sdcard_to_file(char* fname)
{
  FILE* f = fopen(fname, "wb");
  for (int i = 0; i < SDSIZE; i++) {
    fputc(sdcard[i], f);
  }
  fclose(f);
}

int get_file_size(char* fname)
{
  struct stat file_stats;
  stat(fname, &file_stats);
  return (int)file_stats.st_size;
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

void generate_dummy_file_embed_name(const char* name, int size)
{
  FILE* f = fopen(name, "wb");
  for (int i = 0; i < strlen(name); i++) {
    fputc(name[i], f);
  }
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
  char* file8kb = "8kbtest.tmp";
  bool suppressflag = 0;

  void SetUp() override
  {
    // suppress the chatty output
    CaptureStdOut();

    change_dir("/"); // always start in root folder

    generate_dummy_file(file4kb, 4096);
    generate_dummy_file(file8kb, 8192);
  }

  void CaptureStdOut(void)
  {
    if (!suppressflag) {
      suppressflag = 1;
      testing::internal::CaptureStderr();
      testing::internal::CaptureStdout();
    }
  }

  void ReleaseStdOut(void)
  {
    if (suppressflag) {
      suppressflag = 0;
      testing::internal::GetCapturedStderr();
      testing::internal::GetCapturedStdout();
    }
  }

  std::string RetrieveStdOut(void)
  {
    fflush(stdout);
    std::string output = testing::internal::GetCapturedStdout();
    testing::internal::GetCapturedStderr();
    suppressflag = 0;
    return output;
  }


  void TearDown() override
  {
    ReleaseStdOut();

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

  ReleaseStdOut();
  ASSERT_EQ(0, is_fragmented("4kb1.tmp"));
  ASSERT_EQ(0, is_fragmented("4kb3.tmp"));
  ASSERT_EQ(0, is_fragmented(file8kb));
}

TEST_F(Mega65FtpTestFixture, RenameToNonExistingFilenameShouldBePermitted)
{
  init_sdcard_data();

  upload_file(file4kb, file4kb);
  // dump_sdcard_to_file("sdcard_before.bin");
  rename_file_or_dir(file4kb, file8kb);

  // dump_sdcard_to_file("sdcard_after.bin");
  ReleaseStdOut();
  ASSERT_EQ(1, contains_file_or_dir(file8kb));
}

TEST_F(Mega65FtpTestFixture, RenameToExistingFilenameShouldNotBePermitted)
{
  init_sdcard_data();
  upload_file(file4kb, file4kb);
  upload_file(file8kb, file8kb);
  int ret = rename_file_or_dir(file4kb, file8kb);
  // this should result in an error

  ReleaseStdOut();
  ASSERT_EQ(ret, -2);
}

TEST_F(Mega65FtpTestFixture, CanShowDirContentsForAbsolutePath)
{
  init_sdcard_data();
  create_dir("test");
  // dump_sdcard_to_file("sdcard_before.bin");
  change_dir("/test");
  upload_file(file4kb, file4kb);
  // dump_sdcard_to_file("sdcard_after.bin");
  change_dir("/");
  ReleaseStdOut();

  CaptureStdOut();
  show_directory("/test");

  std::string output = RetrieveStdOut();

  EXPECT_THAT(output, testing::ContainsRegex("4kbtest.tmp"));
}

void upload_dummy_file_with_embedded_name(char* newname, int size)
{
  generate_dummy_file_embed_name(newname, size);
  upload_file(newname, newname);
  delete_local_file(newname);
}

void upload_127_dummy_files_with_embedded_name(void)
{
  char newname[128];
  for (int k = 1; k <= 127; k++) {
    sprintf(newname,"%d.TXT", k);
    upload_dummy_file_with_embedded_name(newname, 256);
  }
}

TEST_F(Mega65FtpTestFixture, RootDirCanHoldMoreThan128Files)
{
  // NOTE: mega65_ftp was limited to only 128 direntries in a single sector.
  // If you tried to write a 129th file, it would fail. We'd like to update the code to allow for this now.

  init_sdcard_data();

  // Let's generate 127 dummy files. These (in addition to the drive name entry) will fill up all the 128 direntries
  upload_127_dummy_files_with_embedded_name();

  ReleaseStdOut();

  // Let's make a 129th dir-entry, which needs to be allocated on a new/free cluster
  CaptureStdOut();
  upload_dummy_file_with_embedded_name("128.TXT", 256);

  std::string output = RetrieveStdOut();

  // dump_sdcard_to_file("sdcard.bin");

  EXPECT_THAT(output, testing::ContainsRegex("Uploaded"));
}

TEST_F(Mega65FtpTestFixture, RootDir128thItemIsADirectory)
{
  init_sdcard_data();

  // Let's generate 127 dummy files. These (in addition to the drive name entry) will fill up all the 128 direntries
  upload_127_dummy_files_with_embedded_name();

  ReleaseStdOut();

  // Let's make a 129th dir-entry, which needs to be allocated on a new/free cluster
  CaptureStdOut();
  create_dir("DIR128");

  std::string output = RetrieveStdOut();

  // dump_sdcard_to_file("sdcard.bin");

  EXPECT_THAT(output, testing::ContainsRegex("Uploaded"));
}

TEST_F(Mega65FtpTestFixture, SubDirCanHoldMoreThan128Files)
{
  init_sdcard_data();

  // Let's generate 127 dummy files. These (in addition to the drive name entry) will fill up all the 128 direntries
  create_dir("MYDIR");
  change_dir("/MYDIR");
  // NOTE: For a sub-directory, the first two direntries are '.' and '..'
  // So we really ought to be writing 126 dummy files in this test to be precise, but 127 files does the job too...
  upload_127_dummy_files_with_embedded_name();

  ReleaseStdOut();

  // Let's make a 129th dir-entry, which needs to be allocated on a new/free cluster
  CaptureStdOut();
  upload_dummy_file_with_embedded_name("128.TXT", 256);

  std::string output = RetrieveStdOut();

  // dump_sdcard_to_file("sdcard.bin");

  EXPECT_THAT(output, testing::ContainsRegex("Uploaded"));
}

TEST_F(Mega65FtpTestFixture, UploadNewLFNShouldOfferShortName)
{
  init_sdcard_data();

  // dump_sdcard_to_file("sdcard_before.bin");

  generate_dummy_file("Long File Name.d81", 4096);
  upload_file("Long File Name.d81", "Long File Name.d81");
  // assess_shortname = "LONGFI~1.D81"

  // Let's assess the dir output to see if it was written

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("LONGFI~1.D81");
  std::string output = RetrieveStdOut();

  // dump_sdcard_to_file("sdcard_after.bin");

  EXPECT_THAT(output, testing::ContainsRegex(" 1 File"));
}

TEST_F(Mega65FtpTestFixture, ReUploadOfVfatFileButLargerStillHasContiguousClusters)
{
  init_sdcard_data();

  // dump_sdcard_to_file("sdcard_before.bin");

  generate_dummy_file_embed_name("Long File Name.d81", 4096);
  generate_dummy_file_embed_name("dummy.txt", 8192);

  upload_file("Long File Name.d81", "Long File Name.d81");
  upload_file("dummy.txt", "dummy.txt");

  // dump_sdcard_to_file("sdcard_after1.bin");

  generate_dummy_file_embed_name("Long File Name.d81", 16384);

  upload_file("Long File Name.d81", "Long File Name.d81");

  // dump_sdcard_to_file("sdcard_after2.bin");

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("LONGFI*.D81");
  std::string output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex(" 1 File"));

  ASSERT_EQ(0, is_fragmented("Long File Name.d81"));
}

// Assure that a short-name that is renamed to a long-name handles dir-entries gracefully
// (E.g., if a shortname entry is squeezed amongst other file entries, then it might need to get deleted and
// then we look for another contiguous+free block of direntries to store vfat-chunks + dos8.3 entry)
TEST_F(Mega65FtpTestFixture, AssureEnoughDirEntriesToSupportLengthenedVfatName)
{
  init_sdcard_data();

  generate_dummy_file_embed_name("short.d81", 4096);
  generate_dummy_file_embed_name("dummy.txt", 8192);

  upload_file("dummy.txt", "dummy.txt");
  upload_file("short.d81", "short.d81");
  upload_file("dummy.txt", "dummy2.txt");

  // dump_sdcard_to_file("sdcard_before.bin");

  rename_file_or_dir("short.d81", "Long File Name.d81");

  // dump_sdcard_to_file("sdcard_after.bin");
}

TEST_F(Mega65FtpTestFixture, UploadNewLFNShouldCreateLFNAndShortNameDirEntries)
{
  init_sdcard_data();
  generate_dummy_file_embed_name("LongFileName.d81", 4096);

  // dump_sdcard_to_file("sdcard_before.bin");

  upload_file("LongFileName.d81", "LongFileName.d81");

  // examine dir-entries for file and assess validity of LFN and 8.3 ShortName
  ReleaseStdOut();
  CaptureStdOut();
  show_directory("LONGFI*.D81");
  std::string output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex(" 1 File"));

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("LongFileName.d81");
  output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex(" 1 File"));

  // dump_sdcard_to_file("sdcard_after.bin");
}

TEST_F(Mega65FtpTestFixture, DeleteLFNShouldDeleteLFNAndShortNameDirEntries)
{
  init_sdcard_data();
  generate_dummy_file_embed_name("LongFileName.d81", 4096);

  // dump_sdcard_to_file("sdcard_before.bin");

  upload_file("LongFileName.d81", "LongFileName.d81");
  // as with test above, assure the dir-entries exist

  // dump_sdcard_to_file("sdcard_after1.bin");

  delete_file("LongFileName.d81");
  // now assess that dir-entries have been removed

  // dump_sdcard_to_file("sdcard_after2.bin");

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("LONGFI*.D81");
  std::string output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex(" 0 File"));

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("LongFileName.d81");
  output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex(" 0 File"));
}

TEST_F(Mega65FtpTestFixture, RenameLFNToAnotherLFNShouldRenameLFNAndShortNameDirEntries)
{
  init_sdcard_data();
  generate_dummy_file_embed_name("LongFileName.d81", 4096);

  // dump_sdcard_to_file("sdcard_before.bin");

  upload_file("LongFileName.d81", "LongFileName.d81");

  // dump_sdcard_to_file("sdcard_after1.bin");

  // as with test above, assure the dir-entries exist
  rename_file_or_dir("LongFileName.d81", "AnotherLongFileName.d81");

  // now assess that dir-entries for LFN and 8.3 shortname have been updated
  // dump_sdcard_to_file("sdcard_after2.bin");

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("LONGFI*.D81");
  std::string output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex(" 0 File"));

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("LongFileName.d81");
  output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex(" 0 File"));

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("ANOTHE~1.D81");
  output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex(" 1 File"));

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("AnotherLongFileName.d81");
  output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex(" 1 File"));
}

TEST_F(Mega65FtpTestFixture, UploadSameLFNWithExistingShortNameShouldOverwrite)
{
  init_sdcard_data();
  generate_dummy_file_embed_name("LongFileName.d81", 4096);
  generate_dummy_file_embed_name("LongFileName2.d81", 8192);

  upload_file("LongFileName.d81", "LongFileName.d81");

  // dump_sdcard_to_file("sdcard_before.bin");

  upload_file("LongFileName2.d81", "LongFileName.d81");

  // dump_sdcard_to_file("sdcard_after.bin");

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("LONGFI*.D81");
  std::string output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex(" 1 File"));

  download_file("LongFileName.d81", "LongFileName.d81", 0);

  // it should match the contents of LongFileName2.d81
  EXPECT_EQ(get_file_size("LongFileName.d81"), get_file_size("LongFileName2.d81"));
}

TEST_F(Mega65FtpTestFixture, UploadDifferentLFNWithExistingShortNameShouldUseDifferentName)
{
  init_sdcard_data();
  generate_dummy_file_embed_name("LongFileName.d81", 4096);
  generate_dummy_file_embed_name("LongFishyFishy.d81", 8192);

  upload_file("LongFileName.d81", "LongFileName.d81"); // This will be 'LONGFI~1.D81'
  // dump_sdcard_to_file("sdcard_before.bin");
  upload_file("LongFishyFishy.d81", "LongFishyFishy.d81");   // This should be 'LONGFI~2.D81'
  // dump_sdcard_to_file("sdcard_after.bin");

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("LONGFI~2.D81");
  std::string output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex(" 1 File"));
  EXPECT_THAT(output, testing::ContainsRegex("LongFishyFishy.d81"));
}

TEST_F(Mega65FtpTestFixture, CanChangeDirectoryIntoLFNDirectory)
{
  init_sdcard_data();
  generate_dummy_file_embed_name("LongFileName.d81", 4096);
  create_dir("LongDirectory");
  change_dir("LongDirectory");
  upload_file("LongFileName.d81", "LongFileName.d81");

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("");
  std::string output = RetrieveStdOut();
  EXPECT_THAT(output, Not(testing::ContainsRegex("LongDirectory")));
  EXPECT_THAT(output, testing::ContainsRegex("LongFileName.d81"));
}

TEST_F(Mega65FtpTestFixture, DisallowCreatingDirectoryWithSameNameButDifferentCasing)
{
  init_sdcard_data();
  create_dir("LongDirectory");
  // dump_sdcard_to_file("sdcard_before.bin");
  create_dir("LoNgDiReCtOrY");
  // dump_sdcard_to_file("sdcard_after.bin");

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("LONGDI*");
  std::string output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex("\n1 Dir"));
}

// Disallow creating of a new file if same file is uploaded again but with different casing
// (it should just replace the existing file)
TEST_F(Mega65FtpTestFixture, UploadSameLFNWithDifferentCasingShouldOverwrite)
{
  init_sdcard_data();
  generate_dummy_file_embed_name("LongFileName.d81", 4096);
  generate_dummy_file_embed_name("LoNgFiLeNaMe2.d81", 8192);

  upload_file("LongFileName.d81", "LongFileName.d81");

  // dump_sdcard_to_file("sdcard_before.bin");

  upload_file("LoNgFiLeNaMe2.d81", "LoNgFiLeNaMe.d81");

  // dump_sdcard_to_file("sdcard_after.bin");

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("LONGFI*.D81");
  std::string output = RetrieveStdOut();
  EXPECT_THAT(output, testing::ContainsRegex(" 1 File"));

  download_file("LongFileName.d81", "LongFileName.d81", 0);

  // it should match the contents of LongFileName2.d81
  EXPECT_EQ(get_file_size("LongFileName.d81"), get_file_size("LoNgFiLeNaMe2.d81"));
}

TEST_F(Mega65FtpTestFixture, AssessIfRenamingVfatDirectoryWorks)
{
  init_sdcard_data();
  create_dir("LongDirectory");
  dump_sdcard_to_file("sdcard_before.bin");
  rename_file_or_dir("LongDirectory", "EvenLongerDirectory");
  dump_sdcard_to_file("sdcard_after.bin");

  ReleaseStdOut();
  CaptureStdOut();
  show_directory("");
  std::string output = RetrieveStdOut();
  EXPECT_THAT(output, Not(testing::ContainsRegex("LongDirectory")));
  EXPECT_THAT(output, testing::ContainsRegex("EvenLongerDirectory"));
  EXPECT_THAT(output, testing::ContainsRegex("\n1 Dir"));
}

// Assess an LFN file of exactly 13 chars long (will it behave properly)

// Assess ~1, ~2, ~3 short-names work into two digit form too (~10, ~11, etc...)

// Assure vfat direntries are created/deleted/renamed successfully when they cross cluster boundaries...

// re-upload the same file with a smaller size and assure orphaned clusters get freed.

} // namespace mega65_ftp
