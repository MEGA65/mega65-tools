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

} // namespace mega65_ftp
