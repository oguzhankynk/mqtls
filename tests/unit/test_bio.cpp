#include "mqtls/span.hpp"

#include <gtest/gtest.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cstddef>

#include "fake_transport.hpp"
#include "test_pki.hpp"
#include "tls/bio.hpp"
#include "tls/context.hpp"
#include "tls/openssl_raii.hpp"

namespace {

using mqtls::test::FakeTransport;

/// @brief A ByteTransport stub that reports a fixed pending_read() value
/// and otherwise does nothing; isolates the BIO_CTRL_PENDING plumbing test
/// from any real transport's actual buffering behavior.
class FixedPendingTransport final : public mqtls::transport::ByteTransport {
public:
  explicit FixedPendingTransport(std::size_t pending) : pending_(pending) {}

  mqtls::Result<std::size_t> write_some(mqtls::span<const std::byte>) override {
    return std::size_t{0};
  }
  mqtls::Result<std::size_t> read_some(mqtls::span<std::byte>) override {
    return std::size_t{0};
  }
  int read_fd() const noexcept override {
    return -1;
  }
  int write_fd() const noexcept override {
    return -1;
  }
  std::size_t pending_read() const noexcept override {
    return pending_;
  }

private:
  std::size_t pending_;
};

/// @brief Drives both sides of a handshake with a simple alternating loop,
/// relying on the FakeTransport pair's real kernel socket buffering to make
/// progress; fails (returns false) on any error other than WANT_READ/WRITE,
/// or if neither side finishes within a generous iteration cap.
bool DriveHandshake(SSL* a, SSL* b) {
  bool a_done = false;
  bool b_done = false;
  for (int i = 0; i < 10000 && !(a_done && b_done); ++i) {
    if (!a_done) {
      const int rc = SSL_do_handshake(a);
      if (rc == 1) {
        a_done = true;
      } else {
        const int err = SSL_get_error(a, rc);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
          return false;
        }
      }
    }
    if (!b_done) {
      const int rc = SSL_do_handshake(b);
      if (rc == 1) {
        b_done = true;
      } else {
        const int err = SSL_get_error(b, rc);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
          return false;
        }
      }
    }
  }
  return a_done && b_done;
}

}  // namespace

TEST(BioTest, ReadSetsRetryFlagOnWouldBlock) {
  auto [a, b] = FakeTransport::make_pair();
  (void)b;
  BIO* bio = mqtls::tls::make_bio(*a);
  ASSERT_NE(bio, nullptr);

  char buf[16];
  const int n = BIO_read(bio, buf, sizeof(buf));
  EXPECT_LT(n, 0);
  EXPECT_TRUE(BIO_should_retry(bio) != 0);
  EXPECT_TRUE(BIO_should_read(bio) != 0);

  BIO_free(bio);
}

TEST(BioTest, ReadClearsRetryFlagOnSuccessAndReturnsData) {
  auto [a, b] = FakeTransport::make_pair();
  const std::byte payload[] = {std::byte{7}, std::byte{8}};
  ASSERT_TRUE(b->write_some(payload).has_value());

  BIO* bio = mqtls::tls::make_bio(*a);
  ASSERT_NE(bio, nullptr);

  char buf[16];
  const int n = BIO_read(bio, buf, sizeof(buf));
  EXPECT_EQ(n, 2);
  EXPECT_FALSE(BIO_should_retry(bio));
  EXPECT_EQ(static_cast<unsigned char>(buf[0]), 7);
  EXPECT_EQ(static_cast<unsigned char>(buf[1]), 8);

  BIO_free(bio);
}

TEST(BioTest, WriteSetsRetryFlagOnWouldBlock) {
  auto [a, b] = FakeTransport::make_pair();
  (void)b;
  a->force_write_would_block_on_call(1);
  BIO* bio = mqtls::tls::make_bio(*a);
  ASSERT_NE(bio, nullptr);

  const char data[] = "x";
  const int n = BIO_write(bio, data, 1);
  EXPECT_LT(n, 0);
  EXPECT_TRUE(BIO_should_retry(bio) != 0);
  EXPECT_TRUE(BIO_should_write(bio) != 0);

  BIO_free(bio);
}

TEST(BioTest, ShortWriteIsReportedAsPartialSuccessNotError) {
  auto [a, b] = FakeTransport::make_pair();
  (void)b;
  a->limit_write_size(3);
  BIO* bio = mqtls::tls::make_bio(*a);
  ASSERT_NE(bio, nullptr);

  const char data[] = "abcdefgh";
  const int n = BIO_write(bio, data, 8);
  EXPECT_EQ(n, 3);
  EXPECT_FALSE(BIO_should_retry(bio));

  BIO_free(bio);
}

TEST(BioTest, TruncatedReadIsAHardFailureWithNoRetry) {
  auto [a, b] = FakeTransport::make_pair();
  b.reset();  // peer closes its end

  BIO* bio = mqtls::tls::make_bio(*a);
  ASSERT_NE(bio, nullptr);

  char buf[16];
  const int n = BIO_read(bio, buf, sizeof(buf));
  EXPECT_LT(n, 0);
  EXPECT_FALSE(BIO_should_retry(bio));

  BIO_free(bio);
}

TEST(BioTest, PendingForwardsTheTransportsValue) {
  FixedPendingTransport transport(42);
  BIO* bio = mqtls::tls::make_bio(transport);
  ASSERT_NE(bio, nullptr);

  EXPECT_EQ(BIO_ctrl_pending(bio), 42);

  BIO_free(bio);
}

TEST(BioTest, FullHandshakeSucceedsOverAFakeTransportPair) {
  mqtls::test::TestPki pki;
  const auto server_cert = pki.unit(100);
  const auto client_cert = pki.unit(101);

  mqtls::TlsConfig server_config;
  server_config.ca_file = pki.ca().cert_file;
  server_config.cert_file = server_cert.cert_file;
  server_config.key_file = server_cert.key_file;
  mqtls::TlsConfig client_config = server_config;
  client_config.cert_file = client_cert.cert_file;
  client_config.key_file = client_cert.key_file;

  auto server_ctx = mqtls::tls::Context::make(server_config, mqtls::Role::server);
  auto client_ctx = mqtls::tls::Context::make(client_config, mqtls::Role::client);
  ASSERT_TRUE(server_ctx.has_value());
  ASSERT_TRUE(client_ctx.has_value());

  auto [server_transport, client_transport] = FakeTransport::make_pair();

  mqtls::tls::SslPtr server_ssl(SSL_new(server_ctx->native()));
  mqtls::tls::SslPtr client_ssl(SSL_new(client_ctx->native()));
  ASSERT_TRUE(server_ssl);
  ASSERT_TRUE(client_ssl);

  BIO* server_bio = mqtls::tls::make_bio(*server_transport);
  BIO* client_bio = mqtls::tls::make_bio(*client_transport);
  ASSERT_NE(server_bio, nullptr);
  ASSERT_NE(client_bio, nullptr);
  SSL_set_bio(server_ssl.get(), server_bio, server_bio);
  SSL_set_bio(client_ssl.get(), client_bio, client_bio);

  SSL_set_accept_state(server_ssl.get());
  SSL_set_connect_state(client_ssl.get());
  ASSERT_EQ(X509_VERIFY_PARAM_set1_host(SSL_get0_param(client_ssl.get()), "unit-100", 0), 1);
  ASSERT_EQ(X509_VERIFY_PARAM_set1_host(SSL_get0_param(server_ssl.get()), "unit-101", 0), 1);

  ASSERT_TRUE(DriveHandshake(server_ssl.get(), client_ssl.get()));
  EXPECT_STREQ(SSL_get_version(server_ssl.get()), "TLSv1.3");
  EXPECT_EQ(SSL_get_verify_result(server_ssl.get()), X509_V_OK);
  EXPECT_EQ(SSL_get_verify_result(client_ssl.get()), X509_V_OK);
}
