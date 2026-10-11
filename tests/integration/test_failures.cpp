#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "channel_fixture.hpp"
#include "mq/message_queue.hpp"
#include "test_pki.hpp"
#include "unique_queue_name.hpp"

namespace {

using mqtls::ChannelOptions;
using mqtls::test::EstablishBothSides;
using mqtls::test::MakeConfigPair;

mqtls::TlsConfig TlsFor(const mqtls::test::TestCert& cert, const mqtls::test::TestCert& ca) {
  mqtls::TlsConfig config;
  config.ca_file = ca.cert_file;
  config.cert_file = cert.cert_file;
  config.key_file = cert.key_file;
  return config;
}

class FailureIntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    prefix_ = mqtls::test::unique_queue_prefix();
  }

  mqtls::test::TestPki pki_;
  std::string prefix_;
};

}  // namespace

TEST_F(FailureIntegrationTest, MessageAtExactlyMaxSizeSucceedsOneOverFails) {
  auto [a, b] = MakeConfigPair(prefix_);
  ChannelOptions options;
  options.max_message_size = 100;

  auto pair = EstablishBothSides(
      a, TlsFor(pki_.unit(100), pki_.ca()), b, TlsFor(pki_.unit(101), pki_.ca()), options);
  ASSERT_TRUE(pair.local.has_value());
  ASSERT_TRUE(pair.peer.has_value());

  const std::vector<std::byte> at_limit(100);
  EXPECT_TRUE(pair.local->send(at_limit).has_value());
  auto received = pair.peer->recv();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->size(), 100U);

  const std::vector<std::byte> over_limit(101);
  const auto result = pair.local->send(over_limit);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::message_too_large);
  EXPECT_TRUE(pair.local->is_open());
}

TEST_F(FailureIntegrationTest, NoQueuesAreLeftBehindAfterBothSidesClose) {
  auto [a, b] = MakeConfigPair(prefix_);
  {
    auto pair = EstablishBothSides(
        a, TlsFor(pki_.unit(100), pki_.ca()), b, TlsFor(pki_.unit(101), pki_.ca()));
    ASSERT_TRUE(pair.local.has_value());
    ASSERT_TRUE(pair.peer.has_value());
    EXPECT_TRUE(pair.local->close().has_value());
    EXPECT_TRUE(pair.peer->close().has_value());
  }  // SecureChannel destructors (best-effort close) and QueuePair
     // destructors (TX queue unlink) have now all run.

  const auto a_tx =
      mqtls::mq::MessageQueue::open(mqtls::mq::tx_queue_name(a), mqtls::mq::Direction::write_only);
  const auto b_tx =
      mqtls::mq::MessageQueue::open(mqtls::mq::tx_queue_name(b), mqtls::mq::Direction::write_only);
  EXPECT_FALSE(a_tx.has_value());
  EXPECT_FALSE(b_tx.has_value());
}
