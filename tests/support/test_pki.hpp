#ifndef MQTLS_TEST_SUPPORT_TEST_PKI_HPP
#define MQTLS_TEST_SUPPORT_TEST_PKI_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

/// @brief Test-only support code (not part of the installed `mqtls`/`mqtls_mq`
/// targets): helpers the unit/integration tests link against.
namespace mqtls::test {

/// @brief One generated PEM certificate and the private key that matches it.
///
/// Both files live under the `TestPki` instance that created them and are
/// deleted when that instance is destroyed.
struct TestCert {
  std::filesystem::path cert_file;  ///< PEM certificate (a single certificate, no chain).
  std::filesystem::path key_file;   ///< PEM private key, written with mode 0600.
};

/// @brief Generates and owns a throwaway PKI tree covering the scenarios
/// mqtls's handshake tests need (valid CA, unit identities, a foreign CA,
/// expired / not-yet-valid / wrong-identity / wrong-EKU / wrong-key-type
/// leaves), using the OpenSSL 3 EVP/X509 builder API — never shelling out
/// to the `openssl` CLI.
///
/// Every certificate/key pair this class produces is written as PEM files
/// under one process-unique temporary directory (see dir()); the destructor
/// removes that directory (and therefore every file it ever wrote).
///
/// All certificates use `secp384r1`/P-384 ECDSA keys and SHA-384 signatures,
/// matching mqtls's TLS profile — except rsa_unit(), which
/// deliberately gives the *subject* an RSA-2048 key, for
/// ECDSA+SHA384-sigalg-mismatch tests.
///
/// @par Thread safety
/// Not thread-safe: a single TestPki instance must not be used concurrently
/// from more than one thread. Independent TestPki instances (e.g. one per
/// test, as GoogleTest already gives each test its own stack frame) do not
/// interfere with each other.
///
/// @code
/// mqtls::test::TestPki pki;
/// const mqtls::test::TestCert server = pki.unit(100);
/// const mqtls::test::TestCert client = pki.unit(101);
/// // server.cert_file / server.key_file / pki.ca().cert_file are ready to
/// // hand to mqtls::TlsConfig.
/// @endcode
class TestPki {
public:
  /// @brief Creates a fresh temporary directory and a self-signed test CA
  /// inside it (the `valid_ca` profile, exposed via ca()).
  /// @throws std::runtime_error if the directory or the CA could not be
  /// created (OpenSSL failure, filesystem permission error, ...).
  TestPki();

  TestPki(const TestPki&) = delete;
  TestPki& operator=(const TestPki&) = delete;
  TestPki(TestPki&&) = delete;
  TestPki& operator=(TestPki&&) = delete;

  /// @brief Recursively removes this instance's temporary directory,
  /// deleting every certificate and key it ever wrote.
  /// @par Blocking
  /// Does filesystem I/O; never throws (removal failures are ignored —
  /// there is nothing a destructor could usefully do about them).
  ~TestPki();

  /// @brief The self-signed test CA created by the constructor
  /// (profile `valid_ca`).
  [[nodiscard]] const TestCert& ca() const noexcept {
    return ca_;
  }

  /// @brief A second, self-signed CA unrelated to ca(), created on first
  /// use (profile `foreign_ca`) — for "signed by the wrong issuer" tests.
  /// @throws std::runtime_error on OpenSSL/filesystem failure.
  [[nodiscard]] const TestCert& foreign_ca();

  /// @brief A leaf certificate signed by ca(), with CN and SAN
  /// `DNS:unit-<id>`, EKU `serverAuth, clientAuth` (the
  /// identity scheme `mqtls::mq` expects of a peer).
  /// @param id The unit id to encode (e.g. 100 produces CN/SAN `unit-100`).
  /// @throws std::runtime_error on OpenSSL/filesystem failure.
  [[nodiscard]] TestCert unit(std::uint32_t id);

  /// @brief Same identity/EKU profile as unit(id), but signed by
  /// foreign_ca() instead of ca() — for "peer's chain doesn't lead to our
  /// trusted CA" verification tests.
  /// @throws std::runtime_error on OpenSSL/filesystem failure.
  [[nodiscard]] TestCert unit_signed_by_foreign_ca(std::uint32_t id);

  /// @brief A unit(id)-shaped certificate signed by ca() whose validity
  /// period already ended (`notBefore` and `notAfter` are both in the past).
  /// @throws std::runtime_error on OpenSSL/filesystem failure.
  [[nodiscard]] TestCert expired(std::uint32_t id);

  /// @brief A unit(id)-shaped certificate signed by ca() whose validity
  /// period has not started yet (`notBefore` is in the future).
  /// @throws std::runtime_error on OpenSSL/filesystem failure.
  [[nodiscard]] TestCert not_yet_valid(std::uint32_t id);

  /// @brief A certificate signed by ca() whose CN/SAN encode `actual_id`
  /// — for tests where a channel expects one unit id but the peer presents
  /// a different one (expected to fail hostname verification against the
  /// id the test actually expects).
  /// @throws std::runtime_error on OpenSSL/filesystem failure.
  [[nodiscard]] TestCert wrong_unit(std::uint32_t actual_id);

  /// @brief A unit(id)-shaped certificate signed by ca(), but with EKU
  /// `clientAuth` only (no `serverAuth`) — rejected when the holder is
  /// asked to act as a TLS server.
  /// @throws std::runtime_error on OpenSSL/filesystem failure.
  [[nodiscard]] TestCert wrong_eku(std::uint32_t id);

  /// @brief Same identity/EKU profile as unit(id), signed by ca(), but the
  /// *subject's* key is RSA-2048 rather than EC P-384 — for
  /// ECDSA+SHA384-sigalg-mismatch tests.
  /// @throws std::runtime_error on OpenSSL/filesystem failure.
  [[nodiscard]] TestCert rsa_unit(std::uint32_t id);

  /// @brief The temporary directory every file above was written under.
  [[nodiscard]] const std::filesystem::path& dir() const noexcept {
    return dir_;
  }

private:
  /// @brief Builds, signs (with `issuer`'s key), and writes one leaf
  /// certificate; shared by every profile above except ca()/foreign_ca().
  /// @param file_tag Unique-per-call basename (profile methods combine this
  /// with an internal serial number so repeated calls never collide).
  /// @param common_name CN value.
  /// @param san_dns If non-empty, added as a `subjectAltName = DNS:<value>`
  /// extension.
  /// @param eku If non-empty, an `extendedKeyUsage` extension with this
  /// OpenSSL config value (e.g. `"serverAuth,clientAuth"`).
  /// @param issuer The CA that signs this leaf.
  /// @param rsa_subject_key If true, the subject's own key is RSA-2048
  /// instead of EC P-384.
  /// @param not_before_offset_seconds `notBefore` = now + this many seconds.
  /// @param not_after_offset_seconds `notAfter` = now + this many seconds.
  TestCert MakeLeaf(const std::string& file_tag,
                    const std::string& common_name,
                    const std::string& san_dns,
                    const std::string& eku,
                    const TestCert& issuer,
                    bool rsa_subject_key,
                    long not_before_offset_seconds,
                    long not_after_offset_seconds);

  std::filesystem::path dir_;
  std::uint64_t next_serial_ = 1;
  TestCert ca_;
  std::unique_ptr<TestCert> foreign_ca_;
};

}  // namespace mqtls::test

#endif  // MQTLS_TEST_SUPPORT_TEST_PKI_HPP
