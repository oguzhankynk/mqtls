#include "mqtls/secure_channel.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "fake_transport.hpp"
#include "test_pki.hpp"

namespace {

using mqtls::ChannelOptions;
using mqtls::Role;
using mqtls::SecureChannel;
using mqtls::TlsConfig;
using mqtls::test::FakeTransport;

std::vector<std::byte> BytesFrom(const std::string& s) {
  std::vector<std::byte> out(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    out[i] = static_cast<std::byte>(s[i]);
  }
  return out;
}

std::string StringFrom(const std::vector<std::byte>& bytes) {
  std::string out(bytes.size(), '\0');
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    out[i] = static_cast<char>(bytes[i]);
  }
  return out;
}

class SecureChannelFakeTest : public ::testing::Test {
protected:
  struct Pair {
    mqtls::Result<SecureChannel> server;
    mqtls::Result<SecureChannel> client;
  };

  Pair Establish(ChannelOptions server_options = {}, ChannelOptions client_options = {}) {
    // Not destructured directly into `auto [a, b]`: Clang 14 (used by the
    // `tidy` preset) does not yet accept capturing a structured binding by
    // name in a lambda, even though C++20 allows it.
    auto transports = FakeTransport::make_pair();
    auto& server_transport = transports.first;
    auto& client_transport = transports.second;
    const auto server_cert = pki_.unit(100);
    const auto client_cert = pki_.unit(101);

    TlsConfig server_tls;
    server_tls.ca_file = pki_.ca().cert_file;
    server_tls.cert_file = server_cert.cert_file;
    server_tls.key_file = server_cert.key_file;
    TlsConfig client_tls = server_tls;
    client_tls.cert_file = client_cert.cert_file;
    client_tls.key_file = client_cert.key_file;

    Pair result{tl::make_unexpected(mqtls::make_error_code(mqtls::errc::io_error)),
                tl::make_unexpected(mqtls::make_error_code(mqtls::errc::io_error))};

    std::thread server_thread([&] {
      result.server = SecureChannel::create(
          std::move(server_transport), Role::server, "unit-101", server_tls, server_options);
    });
    std::thread client_thread([&] {
      result.client = SecureChannel::create(
          std::move(client_transport), Role::client, "unit-100", client_tls, client_options);
    });
    server_thread.join();
    client_thread.join();
    return result;
  }

  mqtls::test::TestPki pki_;
};

}  // namespace

TEST_F(SecureChannelFakeTest, HandshakeSucceedsAndReportsCipherAndPeerSubject) {
  auto pair = Establish();
  ASSERT_TRUE(pair.server.has_value());
  ASSERT_TRUE(pair.client.has_value());

  EXPECT_EQ(pair.server->cipher(), "TLS_AES_256_GCM_SHA384");
  EXPECT_EQ(pair.client->cipher(), "TLS_AES_256_GCM_SHA384");
  EXPECT_NE(pair.server->peer_subject().find("unit-101"), std::string::npos);
  EXPECT_NE(pair.client->peer_subject().find("unit-100"), std::string::npos);
  EXPECT_TRUE(pair.server->is_open());
  EXPECT_TRUE(pair.client->is_open());
}

TEST_F(SecureChannelFakeTest, SendThenRecvDeliversWholeMessageBothDirections) {
  auto pair = Establish();
  ASSERT_TRUE(pair.server.has_value());
  ASSERT_TRUE(pair.client.has_value());

  ASSERT_TRUE(pair.client->send(BytesFrom("hello from client")).has_value());
  auto received_by_server = pair.server->recv();
  ASSERT_TRUE(received_by_server.has_value());
  EXPECT_EQ(StringFrom(*received_by_server), "hello from client");

  ASSERT_TRUE(pair.server->send(BytesFrom("hello from server")).has_value());
  auto received_by_client = pair.client->recv();
  ASSERT_TRUE(received_by_client.has_value());
  EXPECT_EQ(StringFrom(*received_by_client), "hello from server");
}

TEST_F(SecureChannelFakeTest, EmptyMessageRoundTrips) {
  auto pair = Establish();
  ASSERT_TRUE(pair.server.has_value());
  ASSERT_TRUE(pair.client.has_value());

  ASSERT_TRUE(pair.client->send(mqtls::span<const std::byte>{}).has_value());
  auto received = pair.server->recv();
  ASSERT_TRUE(received.has_value());
  EXPECT_TRUE(received->empty());
}

TEST_F(SecureChannelFakeTest, LargeMessageOverMultipleTlsRecordsRoundTrips) {
  auto pair = Establish();
  ASSERT_TRUE(pair.server.has_value());
  ASSERT_TRUE(pair.client.has_value());

  std::vector<std::byte> payload(40000);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::byte>(i % 256);
  }

  ASSERT_TRUE(pair.client->send(payload).has_value());
  auto received = pair.server->recv();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(*received, payload);
}

TEST_F(SecureChannelFakeTest, SendOverMaxMessageSizeIsRejectedButChannelStaysOpen) {
  ChannelOptions options;
  options.max_message_size = 8;
  auto pair = Establish(options, options);
  ASSERT_TRUE(pair.server.has_value());
  ASSERT_TRUE(pair.client.has_value());

  const std::vector<std::byte> too_big(9);
  const auto result = pair.client->send(too_big);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::message_too_large);
  EXPECT_TRUE(pair.client->is_open());

  const std::vector<std::byte> ok_size(8);
  EXPECT_TRUE(pair.client->send(ok_size).has_value());
}

TEST_F(SecureChannelFakeTest, RecvClosesChannelOnOversizedDeclaredLength) {
  ChannelOptions server_options;  // server (the sender here) allows big messages
  server_options.max_message_size = 1U << 16;
  ChannelOptions client_options;  // client (the receiver) only tolerates small ones
  client_options.max_message_size = 16;

  auto pair = Establish(server_options, client_options);
  ASSERT_TRUE(pair.server.has_value());
  ASSERT_TRUE(pair.client.has_value());

  const std::vector<std::byte> payload(100);
  ASSERT_TRUE(pair.server->send(payload).has_value());

  const auto result = pair.client->recv();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::message_too_large);
  EXPECT_FALSE(pair.client->is_open());
}

TEST_F(SecureChannelFakeTest, RecvTimesOutWhenNothingArrives) {
  auto pair = Establish();
  ASSERT_TRUE(pair.server.has_value());
  ASSERT_TRUE(pair.client.has_value());

  const auto start = std::chrono::steady_clock::now();
  const auto result = pair.server->recv(std::chrono::milliseconds(100));
  const auto elapsed = std::chrono::steady_clock::now() - start;

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::timeout);
  EXPECT_LT(elapsed, std::chrono::seconds(2));
}

TEST_F(SecureChannelFakeTest, CloseWakesAThreadBlockedInRecv) {
  auto pair = Establish();
  ASSERT_TRUE(pair.server.has_value());
  ASSERT_TRUE(pair.client.has_value());

  std::promise<mqtls::Result<std::vector<std::byte>>> recv_promise;
  auto recv_future = recv_promise.get_future();
  std::thread waiter([&] { recv_promise.set_value(pair.server->recv(std::chrono::seconds(30))); });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_TRUE(pair.server->close().has_value());

  ASSERT_EQ(recv_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  const auto result = recv_future.get();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::channel_closed);
  waiter.join();
}

TEST_F(SecureChannelFakeTest, SendAfterCloseReturnsChannelClosed) {
  auto pair = Establish();
  ASSERT_TRUE(pair.server.has_value());
  ASSERT_TRUE(pair.client.has_value());

  ASSERT_TRUE(pair.client->close().has_value());
  const auto result = pair.client->send(BytesFrom("x"));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::channel_closed);
}

TEST_F(SecureChannelFakeTest, CloseIsIdempotent) {
  auto pair = Establish();
  ASSERT_TRUE(pair.server.has_value());
  EXPECT_TRUE(pair.server->close().has_value());
  EXPECT_TRUE(pair.server->close().has_value());
  EXPECT_TRUE(pair.server->close().has_value());
}

TEST_F(SecureChannelFakeTest, PeerCloseIsSeenAsPeerClosedByTheOtherSide) {
  auto pair = Establish();
  ASSERT_TRUE(pair.server.has_value());
  ASSERT_TRUE(pair.client.has_value());

  std::promise<mqtls::Result<std::vector<std::byte>>> recv_promise;
  auto recv_future = recv_promise.get_future();
  std::thread waiter([&] { recv_promise.set_value(pair.server->recv(std::chrono::seconds(10))); });

  ASSERT_TRUE(pair.client->close().has_value());

  ASSERT_EQ(recv_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  const auto result = recv_future.get();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::peer_closed);
  waiter.join();
}

TEST_F(SecureChannelFakeTest, ConcurrentSendAndRecvWorkInBothDirections) {
  auto pair = Establish();
  ASSERT_TRUE(pair.server.has_value());
  ASSERT_TRUE(pair.client.has_value());

  constexpr int kMessages = 50;
  std::thread server_sender([&] {
    for (int i = 0; i < kMessages; ++i) {
      ASSERT_TRUE(pair.server->send(BytesFrom("s" + std::to_string(i))).has_value());
    }
  });
  std::thread client_sender([&] {
    for (int i = 0; i < kMessages; ++i) {
      ASSERT_TRUE(pair.client->send(BytesFrom("c" + std::to_string(i))).has_value());
    }
  });

  for (int i = 0; i < kMessages; ++i) {
    auto from_client = pair.server->recv();
    ASSERT_TRUE(from_client.has_value());
    EXPECT_EQ(StringFrom(*from_client), "c" + std::to_string(i));
  }
  for (int i = 0; i < kMessages; ++i) {
    auto from_server = pair.client->recv();
    ASSERT_TRUE(from_server.has_value());
    EXPECT_EQ(StringFrom(*from_server), "s" + std::to_string(i));
  }

  server_sender.join();
  client_sender.join();
}
