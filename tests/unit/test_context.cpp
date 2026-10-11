#include <gtest/gtest.h>
#include <openssl/ssl.h>

#include <fstream>

#include "test_pki.hpp"
#include "tls/context.hpp"

namespace {

mqtls::TlsConfig MakeConfig(const mqtls::test::TestCert& cert, const mqtls::test::TestCert& ca) {
  mqtls::TlsConfig config;
  config.ca_file = ca.cert_file;
  config.cert_file = cert.cert_file;
  config.key_file = cert.key_file;
  return config;
}

}  // namespace

TEST(ContextTest, ValidConfigSucceedsForBothRoles) {
  mqtls::test::TestPki pki;
  const auto config = MakeConfig(pki.unit(100), pki.ca());

  EXPECT_TRUE(mqtls::tls::Context::make(config, mqtls::Role::server).has_value());
  EXPECT_TRUE(mqtls::tls::Context::make(config, mqtls::Role::client).has_value());
}

TEST(ContextTest, ServerRoleRequiresPeerCertificate) {
  mqtls::test::TestPki pki;
  const auto config = MakeConfig(pki.unit(100), pki.ca());

  auto context = mqtls::tls::Context::make(config, mqtls::Role::server);
  ASSERT_TRUE(context.has_value());
  EXPECT_EQ(SSL_CTX_get_verify_mode(context->native()),
            SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT);
}

TEST(ContextTest, ClientRoleDoesNotRequireFailIfNoPeerCert) {
  mqtls::test::TestPki pki;
  const auto config = MakeConfig(pki.unit(100), pki.ca());

  auto context = mqtls::tls::Context::make(config, mqtls::Role::client);
  ASSERT_TRUE(context.has_value());
  EXPECT_EQ(SSL_CTX_get_verify_mode(context->native()), SSL_VERIFY_PEER);
}

TEST(ContextTest, NonexistentCaFileFails) {
  mqtls::test::TestPki pki;
  auto config = MakeConfig(pki.unit(100), pki.ca());
  config.ca_file = pki.dir() / "does-not-exist.pem";

  const auto result = mqtls::tls::Context::make(config, mqtls::Role::client);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::tls_config_failed);
}

TEST(ContextTest, CorruptPemCertFails) {
  mqtls::test::TestPki pki;
  auto config = MakeConfig(pki.unit(100), pki.ca());

  const auto garbage_path = pki.dir() / "garbage.pem";
  std::ofstream(garbage_path) << "this is not a PEM certificate\n";
  config.cert_file = garbage_path;

  const auto result = mqtls::tls::Context::make(config, mqtls::Role::client);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::tls_config_failed);
}

TEST(ContextTest, MismatchedKeyAndCertificateFails) {
  mqtls::test::TestPki pki;
  auto config = MakeConfig(pki.unit(100), pki.ca());
  config.key_file = pki.unit(101).key_file;

  const auto result = mqtls::tls::Context::make(config, mqtls::Role::client);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::tls_config_failed);
}

TEST(ContextTest, InvalidCiphersuiteStringFails) {
  mqtls::test::TestPki pki;
  auto config = MakeConfig(pki.unit(100), pki.ca());
  config.ciphersuites = "NOT-A-REAL-CIPHERSUITE";

  const auto result = mqtls::tls::Context::make(config, mqtls::Role::client);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::tls_config_failed);
}

TEST(ContextTest, InvalidConfigIsRejectedBeforeTouchingOpenSsl) {
  mqtls::TlsConfig config;  // ca_file/cert_file/key_file all empty
  const auto result = mqtls::tls::Context::make(config, mqtls::Role::client);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::invalid_argument);
}
