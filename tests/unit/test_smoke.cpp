// Pipeline smoke test: proves that the mqtls library target, its public
// include path, and the GoogleTest integration all work end to end.
#include "mqtls/version.hpp"

#include <gtest/gtest.h>

#include <cstring>

TEST(Smoke, VersionIsNonEmpty) {
  const char* v = mqtls::version();
  ASSERT_NE(v, nullptr);
  EXPECT_GT(std::strlen(v), 0U);
}

TEST(Smoke, VersionMatchesProjectVersion) {
  EXPECT_STREQ(mqtls::version(), MQTLS_TEST_PROJECT_VERSION);
}
