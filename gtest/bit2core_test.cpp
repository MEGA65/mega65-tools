#include "gtest/gtest.h"
#include <stdarg.h>
#include <stdio.h>

extern int real_main(int argc, char **argv);
extern int get_model_id(const char *m65targetname);
extern char *find_fpga_part_from_m65targetname(const char *m65targetname);

// my tests
namespace bit2core {

int call_bit2core_with_args(char *m65targetname, char *bitfile, char *corfile)
{
  char *argv[] = { "bit2core", m65targetname, bitfile, "core_name_field", "core_version_field", corfile, NULL };
  return real_main(6, argv);
}

void generate_dummy_bit_file(const char *name, const char *m65targetname)
{
  char *fpga_part = find_fpga_part_from_m65targetname(m65targetname);

  FILE *f = fopen(name, "wb");
  // write xilinx magic header
  fputc(0x00, f);
  fputc(0x09, f);
  fprintf(f, "%c%c%c%c%c%c%c%c", 0x0f, 0xf0, 0x0f, 0xf0, 0x0f, 0xf0, 0x0f, 0xf0);
  fputc(0x00, f);

  // magic char 'a'
  fputc(0x00, f);
  fputc(0x01, f);
  fputc('a', f);

  // design name
  fputc(0x00, f);
  fputc(0x07, f);
  fprintf(f, "abc123");
  fputc(0x00, f);

  // magic char 'b'
  fputc('b', f);

  // fpga part-id
  int len = strlen(fpga_part) + 1;
  fputc((len >> 8) & 0xff, f);
  fputc(len & 0xff, f);
  fprintf(f, fpga_part);
  fputc(0x00, f);

  // put some extra/random stuff at the end of this
  for (int i = 0; i < 10000; i++) {
    fputc(i % 256, f);
  }
  fclose(f);
}

int extract_model_id_from_core_file(const char *name)
{
  FILE *f = fopen(name, "rb");
  fseek(f, 0x70, SEEK_SET);
  int ret = fgetc(f);
  fclose(f);

  return ret;
}

class Bit2coreTestFixture : public ::testing::Test {
  protected:
  void SetUp() override
  {
    // suppress the chatty output
    ::testing::internal::CaptureStderr();
    ::testing::internal::CaptureStdout();
  }

  void TearDown() override
  {
    testing::internal::GetCapturedStderr();
    testing::internal::GetCapturedStdout();

    // cleanup to remove the dummy .bit and .cor files
    remove("foo.bit");
    remove("foo.cor");
  }
};

TEST_F(Bit2coreTestFixture, ShouldAddModelIdIntoHeader)
{

  char m65targetnames[][80] = { "mega65r1", "mega65r2", "mega65r3", "megaphoner1", "nexys4", "nexys4ddr", "nexys4ddrwidget",
    "wukonga100t" };
  int m65target_cnt = sizeof(m65targetnames) / sizeof(m65targetnames[0]);

  for (int k = 0; k < m65target_cnt; k++) {
    generate_dummy_bit_file("foo.bit", m65targetnames[k]);

    int expected_model_id = get_model_id(m65targetnames[k]);

    call_bit2core_with_args(m65targetnames[k], "foo.bit", "foo.cor");

    int actual_model_id = extract_model_id_from_core_file("foo.cor");

    ASSERT_EQ(expected_model_id, actual_model_id);
  }
}

TEST_F(Bit2coreTestFixture, ShouldFailIfFpgaPartDoesNotSuitM65Target)
{
  generate_dummy_bit_file("foo.bit", "nexys4");
  int exitcode = call_bit2core_with_args("wukonga100t", "foo.bit", "foo.cor");
  ASSERT_EQ(-4, exitcode);
}

}
