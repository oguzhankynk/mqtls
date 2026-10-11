#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "channel_fixture.hpp"
#include "mq/message_queue.hpp"
#include "test_pki.hpp"
#include "unique_queue_name.hpp"

namespace {

using mqtls::ChannelOptions;
using mqtls::TlsConfig;
using mqtls::mq::MqPeerConfig;
using mqtls::test::EstablishBothSides;
using mqtls::test::MakeConfigPair;

TlsConfig TlsFor(const mqtls::test::TestCert& cert, const mqtls::test::TestCert& ca) {
  TlsConfig config;
  config.ca_file = ca.cert_file;
  config.cert_file = cert.cert_file;
  config.key_file = cert.key_file;
  return config;
}

class HandshakeIntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    prefix_ = mqtls::test::unique_queue_prefix();
  }

  mqtls::test::TestPki pki_;
  std::string prefix_;
};

}  // namespace

TEST_F(HandshakeIntegrationTest, SucceedsOverRealQueuesInBothStartOrders) {
  auto [config_a, config_b] = MakeConfigPair(prefix_);
  const auto tls_a = TlsFor(pki_.unit(100), pki_.ca());
  const auto tls_b = TlsFor(pki_.unit(101), pki_.ca());

  auto pair = EstablishBothSides(config_a, tls_a, config_b, tls_b);
  ASSERT_TRUE(pair.local.has_value());
  ASSERT_TRUE(pair.peer.has_value());
}

TEST_F(HandshakeIntegrationTest, LocalUnitEqualToPeerUnitIsRejected) {
  MqPeerConfig config;
  config.queue_prefix = prefix_;
  config.local_unit_id = 100;
  config.peer_unit_id = 100;

  const auto result = mqtls::mq::establish(config, TlsFor(pki_.unit(100), pki_.ca()));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::invalid_argument);
}

TEST_F(HandshakeIntegrationTest, TimesOutWhenPeerNeverAppearsAndLeavesNoQueueBehind) {
  MqPeerConfig config;
  config.queue_prefix = prefix_;
  config.local_unit_id = 100;
  config.peer_unit_id = 101;
  config.peer_wait_timeout = std::chrono::milliseconds(100);

  const auto result = mqtls::mq::establish(config, TlsFor(pki_.unit(100), pki_.ca()));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::timeout);

  const auto leftover = mqtls::mq::MessageQueue::open(mqtls::mq::tx_queue_name(config),
                                                      mqtls::mq::Direction::write_only);
  EXPECT_FALSE(leftover.has_value());
}

TEST_F(HandshakeIntegrationTest, StaleOwnQueueIsClearedBeforeConnecting) {
  auto [config_a, config_b] = MakeConfigPair(prefix_);
  // Leave a leftover queue under config_a's own name, as if a previous,
  // uncleanly-terminated run had created it.
  auto stale = mqtls::mq::MessageQueue::create(
      mqtls::mq::tx_queue_name(config_a), 10, 8192, 0600, mqtls::mq::Direction::write_only, true);
  ASSERT_TRUE(stale.has_value());

  const auto tls_a = TlsFor(pki_.unit(100), pki_.ca());
  const auto tls_b = TlsFor(pki_.unit(101), pki_.ca());
  auto pair = EstablishBothSides(config_a, tls_a, config_b, tls_b);
  EXPECT_TRUE(pair.local.has_value());
  EXPECT_TRUE(pair.peer.has_value());
}

TEST_F(HandshakeIntegrationTest, ClientCertificateFromAForeignCaIsRejectedByTheServer) {
  auto [config_a, config_b] = MakeConfigPair(prefix_);
  const auto tls_a = TlsFor(pki_.unit(100), pki_.ca());
  // b presents a certificate signed by a different CA than a trusts.
  auto tls_b = TlsFor(pki_.unit_signed_by_foreign_ca(101), pki_.ca());

  auto pair = EstablishBothSides(config_a, tls_a, config_b, tls_b);
  // The server (a, the lower unit ID) is the one that verifies the
  // client's certificate, so it is the side guaranteed to observe the
  // failure synchronously, during the handshake call itself: in TLS 1.3
  // with mutual authentication, the client sends its own
  // Certificate/CertificateVerify/Finished and, per the protocol,
  // considers its own side of the handshake complete right after sending
  // Finished — without waiting for the server's verdict on that
  // certificate. The server's rejection (a fatal alert) only reaches the
  // client on a later read. So pair.peer (the client, presenting the bad
  // certificate here) legitimately reporting success is not itself a bug;
  // what matters is that the verifying side (the server) correctly rejects.
  ASSERT_FALSE(pair.local.has_value());
  EXPECT_EQ(pair.local.error(), mqtls::errc::certificate_verify_failed);
}

TEST_F(HandshakeIntegrationTest, PeerCertificateWithWrongUnitIdentityIsRejected) {
  auto [config_a, config_b] = MakeConfigPair(prefix_);
  const auto tls_a = TlsFor(pki_.unit(100), pki_.ca());
  // b's certificate claims to be some other unit, not 101 (what a expects).
  auto tls_b = TlsFor(pki_.wrong_unit(999), pki_.ca());

  auto pair = EstablishBothSides(config_a, tls_a, config_b, tls_b);
  EXPECT_FALSE(pair.local.has_value());
  if (!pair.local.has_value()) {
    EXPECT_EQ(pair.local.error(), mqtls::errc::certificate_verify_failed);
  }
}

TEST_F(HandshakeIntegrationTest, ServerRoleRejectsClientOnlyEkuCertificate) {
  // Lower unit ID (100) is always the TLS server; give it a clientAuth-only
  // certificate, which a real server-side handshake cannot present with.
  auto [config_a, config_b] = MakeConfigPair(prefix_);
  auto tls_a = TlsFor(pki_.wrong_eku(100), pki_.ca());
  const auto tls_b = TlsFor(pki_.unit(101), pki_.ca());

  auto pair = EstablishBothSides(config_a, tls_a, config_b, tls_b);
  EXPECT_FALSE(pair.local.has_value());
  EXPECT_FALSE(pair.peer.has_value());
}

TEST_F(HandshakeIntegrationTest, ExpiredCertificateIsRejected) {
  auto [config_a, config_b] = MakeConfigPair(prefix_);
  const auto tls_a = TlsFor(pki_.unit(100), pki_.ca());
  auto tls_b = TlsFor(pki_.expired(101), pki_.ca());

  auto pair = EstablishBothSides(config_a, tls_a, config_b, tls_b);
  EXPECT_FALSE(pair.local.has_value());
}

TEST_F(HandshakeIntegrationTest, RsaSubjectKeyFailsTheEcdsaSigalgRequirementOnTheServer) {
  auto [config_a, config_b] = MakeConfigPair(prefix_);
  const auto tls_a = TlsFor(pki_.unit(100), pki_.ca());
  auto tls_b = TlsFor(pki_.rsa_unit(101), pki_.ca());

  auto pair = EstablishBothSides(config_a, tls_a, config_b, tls_b);
  // Same asymmetry as ClientCertificateFromAForeignCaIsRejectedByTheServer
  // above: the client's own certificate's key type only matters once the
  // server tries to verify the signature algorithm it used, which the
  // client (b, with the RSA key) may not observe before its own
  // handshake call already returned success.
  ASSERT_FALSE(pair.local.has_value());
}
