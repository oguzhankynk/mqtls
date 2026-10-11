#include "mqtls/error.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <system_error>

namespace {

constexpr std::array<mqtls::errc, 14> kAllErrc = {
    mqtls::errc::ok,
    mqtls::errc::timeout,
    mqtls::errc::channel_closed,
    mqtls::errc::peer_closed,
    mqtls::errc::truncated,
    mqtls::errc::queue_open_failed,
    mqtls::errc::queue_attr_mismatch,
    mqtls::errc::invalid_argument,
    mqtls::errc::handshake_failed,
    mqtls::errc::certificate_verify_failed,
    mqtls::errc::tls_config_failed,
    mqtls::errc::message_too_large,
    mqtls::errc::protocol_error,
    mqtls::errc::io_error,
};

}  // namespace

TEST(Error, ImplicitlyConvertsToStdErrorCode) {
  // Exercises the std::is_error_code_enum<mqtls::errc> specialization: this
  // would not compile if make_error_code() were not found by ADL.
  const std::error_code ec = mqtls::errc::timeout;
  EXPECT_EQ(ec, mqtls::errc::timeout);
  EXPECT_NE(ec, mqtls::errc::ok);
}

TEST(Error, CategoryNameIsStable) {
  EXPECT_STREQ(mqtls::mqtls_category().name(), "mqtls");
  EXPECT_EQ(&mqtls::mqtls_category(), &mqtls::mqtls_category());
}

TEST(Error, EveryValueHasADistinctNonEmptyMessage) {
  std::array<std::string, kAllErrc.size()> messages;
  for (std::size_t i = 0; i < kAllErrc.size(); ++i) {
    const std::error_code ec = kAllErrc[i];
    messages[i] = ec.message();
    EXPECT_FALSE(messages[i].empty()) << "errc index " << i << " has an empty message";
  }
  for (std::size_t i = 0; i < messages.size(); ++i) {
    for (std::size_t j = i + 1; j < messages.size(); ++j) {
      EXPECT_NE(messages[i], messages[j])
          << "errc indices " << i << " and " << j << " share a message";
    }
  }
}

TEST(Error, OkComparesEqualToDefaultConstructedErrorCode) {
  const std::error_code ok = mqtls::errc::ok;
  EXPECT_FALSE(static_cast<bool>(ok));
}

TEST(Error, NonOkComparesTrueInBooleanContext) {
  const std::error_code ec = mqtls::errc::io_error;
  EXPECT_TRUE(static_cast<bool>(ec));
}

TEST(Result, HoldsValueOnSuccess) {
  const mqtls::Result<int> r = 42;
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
}

TEST(Result, HoldsErrorOnFailure) {
  const mqtls::Result<int> r = tl::make_unexpected(mqtls::make_error_code(mqtls::errc::timeout));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), mqtls::errc::timeout);
}

TEST(Timeout, KInfiniteIsNegative) {
  EXPECT_LT(mqtls::kInfinite.count(), 0);
}
