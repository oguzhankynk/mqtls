#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <thread>
#include <vector>

#include "channel_fixture.hpp"
#include "test_pki.hpp"
#include "unique_queue_name.hpp"

namespace {

using mqtls::ChannelOptions;
using mqtls::TlsConfig;
using mqtls::test::EstablishBothSides;
using mqtls::test::MakeConfigPair;

TlsConfig TlsFor(const mqtls::test::TestCert& cert, const mqtls::test::TestCert& ca) {
  TlsConfig config;
  config.ca_file = ca.cert_file;
  config.cert_file = cert.cert_file;
  config.key_file = cert.key_file;
  return config;
}

class SendRecvIntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    prefix_ = mqtls::test::unique_queue_prefix();
    auto [a, b] = MakeConfigPair(prefix_);
    auto pair = EstablishBothSides(
        a, TlsFor(pki_.unit(100), pki_.ca()), b, TlsFor(pki_.unit(101), pki_.ca()));
    ASSERT_TRUE(pair.local.has_value());
    ASSERT_TRUE(pair.peer.has_value());
    local_.emplace(std::move(*pair.local));
    peer_.emplace(std::move(*pair.peer));
  }

  mqtls::SecureChannel& local() {
    return *local_;
  }
  mqtls::SecureChannel& peer() {
    return *peer_;
  }

  mqtls::test::TestPki pki_;
  std::string prefix_;

private:
  // SecureChannel has no default constructor; optional<> defers
  // construction to SetUp(), once a successfully-established channel
  // exists to move from.
  std::optional<mqtls::SecureChannel> local_;
  std::optional<mqtls::SecureChannel> peer_;
};

}  // namespace

TEST_F(SendRecvIntegrationTest, EmptyMessageRoundTrips) {
  ASSERT_TRUE(local().send(mqtls::span<const std::byte>{}).has_value());
  auto received = peer().recv();
  ASSERT_TRUE(received.has_value());
  EXPECT_TRUE(received->empty());
}

TEST_F(SendRecvIntegrationTest, OneByteMessageRoundTrips) {
  const std::byte payload[] = {std::byte{42}};
  ASSERT_TRUE(local().send(payload).has_value());
  auto received = peer().recv();
  ASSERT_TRUE(received.has_value());
  ASSERT_EQ(received->size(), 1U);
  EXPECT_EQ((*received)[0], std::byte{42});
}

TEST_F(SendRecvIntegrationTest, MessageLargerThanOneQueueMessageRoundTrips) {
  // Default mq message_size is 8192; send well over 16 KiB so it must cross
  // several queue messages and, inside OpenSSL, likely several TLS records.
  std::vector<std::byte> payload(40000);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::byte>(i % 256);
  }
  ASSERT_TRUE(local().send(payload).has_value());
  auto received = peer().recv();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(*received, payload);
}

TEST_F(SendRecvIntegrationTest, AllByteValuesRoundTripUncorrupted) {
  std::vector<std::byte> payload(256);
  for (int i = 0; i < 256; ++i) {
    payload[static_cast<std::size_t>(i)] = static_cast<std::byte>(i);
  }
  ASSERT_TRUE(local().send(payload).has_value());
  auto received = peer().recv();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(*received, payload);
}

TEST_F(SendRecvIntegrationTest, ManyMessagesArriveInOrder) {
  // The underlying queue only holds a handful of messages at a time
  // (MqPeerConfig::max_messages, default 10), so all 500 sends must happen
  // concurrently with the reads draining them, not before — sending them
  // all up front would deadlock as soon as the queue fills up, waiting for
  // a reader that only starts once every send() has already returned.
  constexpr int kCount = 500;
  std::thread sender([this] {
    for (int i = 0; i < kCount; ++i) {
      const std::byte payload[2] = {static_cast<std::byte>(i / 256),
                                    static_cast<std::byte>(i % 256)};
      ASSERT_TRUE(local().send(payload).has_value());
    }
  });

  for (int i = 0; i < kCount; ++i) {
    auto received = peer().recv();
    ASSERT_TRUE(received.has_value());
    ASSERT_EQ(received->size(), 2U);
    EXPECT_EQ(static_cast<int>((*received)[0]) * 256 + static_cast<int>((*received)[1]), i);
  }
  sender.join();
}

TEST_F(SendRecvIntegrationTest, RecvTimesOutWithoutData) {
  const auto result = peer().recv(std::chrono::milliseconds(100));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::timeout);
}
