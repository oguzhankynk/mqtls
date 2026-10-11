// Unit tests for tests/support/test_pki.{hpp,cpp}.
//
// Each profile TestPki can produce is checked here with plain OpenSSL
// X509_verify_cert() (the same primitive mqtls::tls::Context/SecureChannel
// build on): positive profiles must verify with
// X509_V_OK, negative profiles must fail with the specific X509_V_ERR_*
// code the scenario is supposed to exercise. This file is expected to be
// wired into tests/CMakeLists.txt by a later task; it include()s
// "support/test_pki.hpp" assuming tests/ is on the include path.

#include <gtest/gtest.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "support/test_pki.hpp"

namespace {

struct X509Deleter {
  void operator()(X509* cert) const noexcept {
    X509_free(cert);
  }
};
using X509Ptr = std::unique_ptr<X509, X509Deleter>;

struct X509StoreDeleter {
  void operator()(X509_STORE* store) const noexcept {
    X509_STORE_free(store);
  }
};
using X509StorePtr = std::unique_ptr<X509_STORE, X509StoreDeleter>;

struct X509StoreCtxDeleter {
  void operator()(X509_STORE_CTX* ctx) const noexcept {
    X509_STORE_CTX_free(ctx);
  }
};
using X509StoreCtxPtr = std::unique_ptr<X509_STORE_CTX, X509StoreCtxDeleter>;

struct BioDeleter {
  void operator()(BIO* bio) const noexcept {
    BIO_free(bio);
  }
};
using BioPtr = std::unique_ptr<BIO, BioDeleter>;

X509Ptr LoadCert(const std::filesystem::path& path) {
  BioPtr bio(BIO_new_file(path.c_str(), "r"));
  if (!bio) {
    return nullptr;
  }
  return X509Ptr(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

/// @brief The outcome of one X509_verify_cert() call: whether it passed, and
/// the X509_V_ERR_* / X509_V_OK code the verifier ended up with.
struct VerifyOutcome {
  bool ok = false;
  int error = -1;
};

/// @brief Verifies `leaf_path` against a trust store built from
/// `trusted_ca_paths`, optionally also checking a hostname (as
/// SecureChannel::create() does via X509_VERIFY_PARAM_set1_host) and/or a
/// certificate purpose (as the EKU check a TLS server/client role implies).
VerifyOutcome VerifyCert(const std::filesystem::path& leaf_path,
                         const std::vector<std::filesystem::path>& trusted_ca_paths,
                         const std::optional<std::string>& expected_host = std::nullopt,
                         int purpose = -1) {
  X509Ptr leaf = LoadCert(leaf_path);
  if (!leaf) {
    ADD_FAILURE() << "failed to load leaf certificate " << leaf_path;
    return {};
  }

  X509StorePtr store(X509_STORE_new());
  for (const auto& ca_path : trusted_ca_paths) {
    X509Ptr ca_cert = LoadCert(ca_path);
    if (!ca_cert) {
      ADD_FAILURE() << "failed to load CA certificate " << ca_path;
      return {};
    }
    // X509_STORE_add_cert up-refs; ca_cert's own X509Ptr can be freed.
    if (X509_STORE_add_cert(store.get(), ca_cert.get()) != 1) {
      ADD_FAILURE() << "X509_STORE_add_cert failed for " << ca_path;
      return {};
    }
  }

  X509StoreCtxPtr ctx(X509_STORE_CTX_new());
  if (X509_STORE_CTX_init(ctx.get(), store.get(), leaf.get(), nullptr) != 1) {
    ADD_FAILURE() << "X509_STORE_CTX_init failed";
    return {};
  }
  if (purpose != -1) {
    X509_STORE_CTX_set_purpose(ctx.get(), purpose);
  }
  if (expected_host) {
    X509_VERIFY_PARAM* param = X509_STORE_CTX_get0_param(ctx.get());
    X509_VERIFY_PARAM_set1_host(param, expected_host->c_str(), 0);
  }

  const int rc = X509_verify_cert(ctx.get());
  const int error = X509_STORE_CTX_get_error(ctx.get());
  return {rc == 1, error};
}

}  // namespace

TEST(TestPkiTest, ValidUnitCertVerifiesAgainstCaAndExpectedHost) {
  mqtls::test::TestPki pki;
  const auto cert = pki.unit(100);

  const auto outcome = VerifyCert(cert.cert_file, {pki.ca().cert_file}, "unit-100");
  EXPECT_TRUE(outcome.ok);
  EXPECT_EQ(outcome.error, X509_V_OK);
}

TEST(TestPkiTest, ValidUnitCertPassesBothServerAndClientPurpose) {
  // D16: a unit's certificate must work in both TLS roles.
  mqtls::test::TestPki pki;
  const auto cert = pki.unit(100);

  const auto server =
      VerifyCert(cert.cert_file, {pki.ca().cert_file}, std::nullopt, X509_PURPOSE_SSL_SERVER);
  EXPECT_TRUE(server.ok);
  EXPECT_EQ(server.error, X509_V_OK);

  const auto client =
      VerifyCert(cert.cert_file, {pki.ca().cert_file}, std::nullopt, X509_PURPOSE_SSL_CLIENT);
  EXPECT_TRUE(client.ok);
  EXPECT_EQ(client.error, X509_V_OK);
}

TEST(TestPkiTest, ForeignCaSignedCertFailsAgainstRealCa) {
  mqtls::test::TestPki pki;
  const auto cert = pki.unit_signed_by_foreign_ca(100);

  const auto outcome = VerifyCert(cert.cert_file, {pki.ca().cert_file}, "unit-100");
  EXPECT_FALSE(outcome.ok);
  EXPECT_EQ(outcome.error, X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY);
}

TEST(TestPkiTest, ForeignCaSignedCertVerifiesAgainstForeignCa) {
  mqtls::test::TestPki pki;
  const auto cert = pki.unit_signed_by_foreign_ca(100);

  const auto outcome = VerifyCert(cert.cert_file, {pki.foreign_ca().cert_file}, "unit-100");
  EXPECT_TRUE(outcome.ok);
  EXPECT_EQ(outcome.error, X509_V_OK);
}

TEST(TestPkiTest, ExpiredCertIsRejected) {
  mqtls::test::TestPki pki;
  const auto cert = pki.expired(100);

  const auto outcome = VerifyCert(cert.cert_file, {pki.ca().cert_file});
  EXPECT_FALSE(outcome.ok);
  EXPECT_EQ(outcome.error, X509_V_ERR_CERT_HAS_EXPIRED);
}

TEST(TestPkiTest, NotYetValidCertIsRejected) {
  mqtls::test::TestPki pki;
  const auto cert = pki.not_yet_valid(100);

  const auto outcome = VerifyCert(cert.cert_file, {pki.ca().cert_file});
  EXPECT_FALSE(outcome.ok);
  EXPECT_EQ(outcome.error, X509_V_ERR_CERT_NOT_YET_VALID);
}

TEST(TestPkiTest, WrongUnitCertFailsHostnameCheckAgainstExpectedPeer) {
  // Scenario: a test expects to be talking to unit-101 but the peer
  // presents a certificate for unit-999.
  mqtls::test::TestPki pki;
  const auto cert = pki.wrong_unit(999);

  const auto outcome = VerifyCert(cert.cert_file, {pki.ca().cert_file}, "unit-101");
  EXPECT_FALSE(outcome.ok);
  EXPECT_EQ(outcome.error, X509_V_ERR_HOSTNAME_MISMATCH);

  // The same certificate does verify against the host it actually claims.
  const auto self_check = VerifyCert(cert.cert_file, {pki.ca().cert_file}, "unit-999");
  EXPECT_TRUE(self_check.ok);
}

TEST(TestPkiTest, WrongEkuCertFailsServerPurposeCheck) {
  mqtls::test::TestPki pki;
  const auto cert = pki.wrong_eku(100);

  const auto as_server =
      VerifyCert(cert.cert_file, {pki.ca().cert_file}, std::nullopt, X509_PURPOSE_SSL_SERVER);
  EXPECT_FALSE(as_server.ok);
  EXPECT_EQ(as_server.error, X509_V_ERR_INVALID_PURPOSE);

  // clientAuth is still present, so the client purpose passes.
  const auto as_client =
      VerifyCert(cert.cert_file, {pki.ca().cert_file}, std::nullopt, X509_PURPOSE_SSL_CLIENT);
  EXPECT_TRUE(as_client.ok);
}

TEST(TestPkiTest, RsaUnitCertChainStillVerifiesAgainstEcCa) {
  // The ECDSA+SHA384-sigalg mismatch this profile exists for is a TLS
  // handshake-time concern (tls::Context's SSL_CTX_set1_sigalgs_list),
  // not something X509_verify_cert itself checks: chain validity only
  // depends on signatures matching, not on the negotiated TLS signature
  // algorithm. This test documents that boundary.
  mqtls::test::TestPki pki;
  const auto cert = pki.rsa_unit(100);

  const auto outcome = VerifyCert(cert.cert_file, {pki.ca().cert_file}, "unit-100");
  EXPECT_TRUE(outcome.ok);
  EXPECT_EQ(outcome.error, X509_V_OK);
}

TEST(TestPkiTest, KeyFilesAreWrittenWithMode0600) {
  mqtls::test::TestPki pki;
  const auto cert = pki.unit(100);

  for (const auto& key_path : {pki.ca().key_file, cert.key_file}) {
    const auto perms = std::filesystem::status(key_path).permissions();
    EXPECT_EQ(perms & std::filesystem::perms::owner_read, std::filesystem::perms::owner_read);
    EXPECT_EQ(perms & std::filesystem::perms::owner_write, std::filesystem::perms::owner_write);
    EXPECT_EQ(perms & std::filesystem::perms::group_all, std::filesystem::perms::none);
    EXPECT_EQ(perms & std::filesystem::perms::others_all, std::filesystem::perms::none);
  }
}

TEST(TestPkiTest, RepeatedCallsForSameIdProduceDistinctFiles) {
  mqtls::test::TestPki pki;
  const auto first = pki.unit(100);
  const auto second = pki.unit(100);
  EXPECT_NE(first.cert_file, second.cert_file);
  EXPECT_NE(first.key_file, second.key_file);
  EXPECT_TRUE(std::filesystem::exists(first.cert_file));
  EXPECT_TRUE(std::filesystem::exists(second.cert_file));
}

TEST(TestPkiTest, DestructorRemovesTemporaryDirectory) {
  std::filesystem::path dir;
  {
    mqtls::test::TestPki pki;
    dir = pki.dir();
    ASSERT_TRUE(std::filesystem::exists(dir));
  }
  EXPECT_FALSE(std::filesystem::exists(dir));
}
