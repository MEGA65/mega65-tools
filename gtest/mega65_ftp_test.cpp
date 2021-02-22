#include "gtest/gtest.h"
#include <stdarg.h>

int parse_command(const char* str, const char* format, ...);

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

} // namespace mega65_ftp
