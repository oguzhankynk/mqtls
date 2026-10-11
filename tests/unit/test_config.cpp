#include "mqtls/config.hpp"

#include <gtest/gtest.h>

namespace {

mqtls::TlsConfig MakeValidTlsConfig() {
  mqtls::TlsConfig config;
  config.ca_file = "ca.pem";
  config.cert_file = "cert.pem";
  config.key_file = "key.pem";
  return config;
}

}  // namespace

TEST(TlsConfigTest, ValidConfigPasses) {
  EXPECT_TRUE(mqtls::validate(MakeValidTlsConfig()).has_value());
}

TEST(TlsConfigTest, EmptyCaFileIsRejected) {
  auto config = MakeValidTlsConfig();
  config.ca_file.clear();
  EXPECT_FALSE(mqtls::validate(config).has_value());
}

TEST(TlsConfigTest, EmptyCertFileIsRejected) {
  auto config = MakeValidTlsConfig();
  config.cert_file.clear();
  EXPECT_FALSE(mqtls::validate(config).has_value());
}

TEST(TlsConfigTest, EmptyKeyFileIsRejected) {
  auto config = MakeValidTlsConfig();
  config.key_file.clear();
  EXPECT_FALSE(mqtls::validate(config).has_value());
}

TEST(TlsConfigTest, EmptyAlgorithmStringsAreRejected) {
  auto ciphers = MakeValidTlsConfig();
  ciphers.ciphersuites.clear();
  EXPECT_FALSE(mqtls::validate(ciphers).has_value());

  auto groups = MakeValidTlsConfig();
  groups.groups.clear();
  EXPECT_FALSE(mqtls::validate(groups).has_value());

  auto sigalgs = MakeValidTlsConfig();
  sigalgs.sigalgs.clear();
  EXPECT_FALSE(mqtls::validate(sigalgs).has_value());
}

TEST(ChannelOptionsTest, DefaultsAreValid) {
  EXPECT_TRUE(mqtls::validate(mqtls::ChannelOptions{}).has_value());
}

TEST(ChannelOptionsTest, ZeroMaxMessageSizeIsRejected) {
  mqtls::ChannelOptions options;
  options.max_message_size = 0;
  EXPECT_FALSE(mqtls::validate(options).has_value());
}

TEST(ChannelOptionsTest, InfiniteHandshakeTimeoutIsAccepted) {
  mqtls::ChannelOptions options;
  options.handshake_timeout = mqtls::kInfinite;
  EXPECT_TRUE(mqtls::validate(options).has_value());
}

TEST(ChannelOptionsTest, NegativeNonInfiniteTimeoutIsRejected) {
  mqtls::ChannelOptions options;
  options.handshake_timeout = mqtls::Timeout{-2};
  const auto result = mqtls::validate(options);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::invalid_argument);
}

TEST(ChannelOptionsTest, ZeroHandshakeTimeoutIsRejected) {
  mqtls::ChannelOptions options;
  options.handshake_timeout = mqtls::Timeout::zero();
  EXPECT_FALSE(mqtls::validate(options).has_value());
}
