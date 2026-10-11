#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "channel_fixture.hpp"
#include "test_pki.hpp"
#include "unique_queue_name.hpp"

namespace {

using mqtls::test::EstablishBothSides;
using mqtls::test::MakeConfigPair;

mqtls::TlsConfig TlsFor(const mqtls::test::TestCert& cert, const mqtls::test::TestCert& ca) {
  mqtls::TlsConfig config;
  config.ca_file = ca.cert_file;
  config.cert_file = cert.cert_file;
  config.key_file = cert.key_file;
  return config;
}

class ConcurrencyIntegrationTest : public ::testing::Test {
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
  std::optional<mqtls::SecureChannel> local_;
  std::optional<mqtls::SecureChannel> peer_;
};

}  // namespace

TEST_F(ConcurrencyIntegrationTest, ManySendersFromMultipleThreadsAllArriveUncorrupted) {
  constexpr int kThreads = 8;
  constexpr int kPerThread = 200;

  // Each sender runs in its own thread, all sending on the *same* channel
  // (local()), serialized internally by send_mtx_. One reader thread on
  // the peer side counts every byte it receives per sender id, so a
  // corrupted/interleaved frame would show up as a wrong byte count or a
  // wrong sequence value rather than silently passing.
  std::vector<std::thread> senders;
  for (int t = 0; t < kThreads; ++t) {
    senders.emplace_back([this, t] {
      for (int i = 0; i < kPerThread; ++i) {
        const std::byte payload[2] = {static_cast<std::byte>(t), static_cast<std::byte>(i)};
        ASSERT_TRUE(local().send(payload).has_value());
      }
    });
  }

  std::vector<int> next_expected(kThreads, 0);
  for (int received = 0; received < kThreads * kPerThread; ++received) {
    auto message = peer().recv();
    ASSERT_TRUE(message.has_value());
    ASSERT_EQ(message->size(), 2U);
    const int sender = static_cast<int>((*message)[0]);
    const int seq = static_cast<int>((*message)[1]);
    ASSERT_GE(sender, 0);
    ASSERT_LT(sender, kThreads);
    EXPECT_EQ(seq, next_expected[static_cast<std::size_t>(sender)])
        << "out-of-order or lost message from sender " << sender;
    ++next_expected[static_cast<std::size_t>(sender)];
  }

  for (auto& sender_thread : senders) {
    sender_thread.join();
  }
}

TEST_F(ConcurrencyIntegrationTest, SendAndRecvProceedConcurrentlyInBothDirections) {
  constexpr int kCount = 100;
  std::thread local_sender([&] {
    for (int i = 0; i < kCount; ++i) {
      const std::byte payload[1] = {static_cast<std::byte>(i)};
      ASSERT_TRUE(local().send(payload).has_value());
    }
  });
  std::thread peer_sender([&] {
    for (int i = 0; i < kCount; ++i) {
      const std::byte payload[1] = {static_cast<std::byte>(i)};
      ASSERT_TRUE(peer().send(payload).has_value());
    }
  });

  for (int i = 0; i < kCount; ++i) {
    auto from_local = peer().recv();
    ASSERT_TRUE(from_local.has_value());
    EXPECT_EQ(static_cast<int>((*from_local)[0]), i);
  }
  for (int i = 0; i < kCount; ++i) {
    auto from_peer = local().recv();
    ASSERT_TRUE(from_peer.has_value());
    EXPECT_EQ(static_cast<int>((*from_peer)[0]), i);
  }

  local_sender.join();
  peer_sender.join();
}

TEST_F(ConcurrencyIntegrationTest, CloseWhileAnotherThreadIsBlockedInRecvWakesItPromptly) {
  std::atomic<bool> recv_returned{false};
  std::thread waiter([&] {
    auto result = peer().recv(std::chrono::seconds(30));
    recv_returned.store(true);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), mqtls::errc::channel_closed);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_FALSE(recv_returned.load());
  ASSERT_TRUE(peer().close().has_value());

  waiter.join();
  EXPECT_TRUE(recv_returned.load());
}

TEST_F(ConcurrencyIntegrationTest, RecvAfterTimeoutLeavesChannelUsable) {
  const auto timed_out = peer().recv(std::chrono::milliseconds(50));
  ASSERT_FALSE(timed_out.has_value());
  EXPECT_EQ(timed_out.error(), mqtls::errc::timeout);

  const std::byte payload[] = {std::byte{9}};
  ASSERT_TRUE(local().send(payload).has_value());
  const auto received = peer().recv(std::chrono::seconds(5));
  ASSERT_TRUE(received.has_value());
  ASSERT_EQ(received->size(), 1U);
  EXPECT_EQ((*received)[0], std::byte{9});
}
