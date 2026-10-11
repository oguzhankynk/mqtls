#include "test_pki.hpp"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <sys/stat.h>
#include <vector>

namespace mqtls::test {

namespace {

// ---------------------------------------------------------------------------
// RAII for the raw OpenSSL handles this file touches (project rule: never
// hold a naked OpenSSL/POSIX resource).
// ---------------------------------------------------------------------------

struct EvpPkeyDeleter {
  void operator()(EVP_PKEY* key) const noexcept {
    EVP_PKEY_free(key);
  }
};
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

struct X509Deleter {
  void operator()(X509* cert) const noexcept {
    X509_free(cert);
  }
};
using X509Ptr = std::unique_ptr<X509, X509Deleter>;

struct X509ExtensionDeleter {
  void operator()(X509_EXTENSION* ext) const noexcept {
    X509_EXTENSION_free(ext);
  }
};
using X509ExtensionPtr = std::unique_ptr<X509_EXTENSION, X509ExtensionDeleter>;

struct BioDeleter {
  void operator()(BIO* bio) const noexcept {
    BIO_free(bio);
  }
};
using BioPtr = std::unique_ptr<BIO, BioDeleter>;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

constexpr long kOneDaySeconds = 24L * 3600;
constexpr long kThirtyDaysSeconds = 30 * kOneDaySeconds;
constexpr long kOneYearSeconds = 365 * kOneDaySeconds;
constexpr long kTenYearsSeconds = 10 * 365 * kOneDaySeconds;

[[noreturn]] void ThrowOpenSslError(const std::string& what) {
  char buf[256];
  ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
  throw std::runtime_error(what + ": " + buf);
}

std::filesystem::path MakeTempDir() {
  std::string templ = (std::filesystem::temp_directory_path() / "mqtls_test_pki_XXXXXX").string();
  std::vector<char> buf(templ.begin(), templ.end());
  buf.push_back('\0');
  if (::mkdtemp(buf.data()) == nullptr) {
    throw std::runtime_error(std::string("mkdtemp failed: ") + std::strerror(errno));
  }
  return std::filesystem::path(buf.data());
}

EvpPkeyPtr GenerateEcKey() {
  EvpPkeyPtr key(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-384"));
  if (!key) {
    ThrowOpenSslError("generate EC P-384 key");
  }
  return key;
}

EvpPkeyPtr GenerateRsaKey() {
  EvpPkeyPtr key(EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", static_cast<std::size_t>(2048)));
  if (!key) {
    ThrowOpenSslError("generate RSA-2048 key");
  }
  return key;
}

void WriteKeyPem(const std::filesystem::path& path, EVP_PKEY* key) {
  {
    BioPtr bio(BIO_new_file(path.c_str(), "w"));
    if (!bio) {
      ThrowOpenSslError("open " + path.string());
    }
    if (PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
      ThrowOpenSslError("write key " + path.string());
    }
  }  // bio closed/flushed here, before chmod
  if (::chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) {
    throw std::runtime_error("chmod 0600 failed for " + path.string() + ": " +
                             std::strerror(errno));
  }
}

void WriteCertPem(const std::filesystem::path& path, X509* cert) {
  BioPtr bio(BIO_new_file(path.c_str(), "w"));
  if (!bio) {
    ThrowOpenSslError("open " + path.string());
  }
  if (PEM_write_bio_X509(bio.get(), cert) != 1) {
    ThrowOpenSslError("write cert " + path.string());
  }
}

EvpPkeyPtr LoadKeyPem(const std::filesystem::path& path) {
  BioPtr bio(BIO_new_file(path.c_str(), "r"));
  if (!bio) {
    ThrowOpenSslError("open " + path.string());
  }
  EvpPkeyPtr key(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (!key) {
    ThrowOpenSslError("read key " + path.string());
  }
  return key;
}

X509Ptr LoadCertPem(const std::filesystem::path& path) {
  BioPtr bio(BIO_new_file(path.c_str(), "r"));
  if (!bio) {
    ThrowOpenSslError("open " + path.string());
  }
  X509Ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  if (!cert) {
    ThrowOpenSslError("read cert " + path.string());
  }
  return cert;
}

void SetCommonName(X509* cert, const std::string& common_name) {
  X509_NAME* name = X509_get_subject_name(cert);
  X509_NAME_add_entry_by_txt(
      name, "O", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("mqtls-test"), -1, -1, 0);
  X509_NAME_add_entry_by_txt(name,
                             "CN",
                             MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>(common_name.c_str()),
                             -1,
                             -1,
                             0);
}

/// @brief Builds one X.509v3 extension via the OpenSSL config-string parser
/// and attaches it to `subject`. `issuer` is only consulted by extensions
/// that reference it (none of the ones this file uses do, but passing the
/// real issuer keeps the X509V3_CTX accurate).
void AddExtension(X509* subject, X509* issuer, int nid, const std::string& value) {
  X509V3_CTX ctx;
  X509V3_set_ctx_nodb(&ctx);
  X509V3_set_ctx(&ctx, issuer, subject, nullptr, nullptr, 0);
  X509ExtensionPtr ext(X509V3_EXT_nconf_nid(nullptr, &ctx, nid, value.c_str()));
  if (!ext) {
    ThrowOpenSslError("build extension " + std::to_string(nid) + "=" + value);
  }
  if (X509_add_ext(subject, ext.get(), -1) != 1) {
    ThrowOpenSslError("add extension " + std::to_string(nid));
  }
}

/// @brief Builds a fresh, self-signed CA certificate/key pair and writes it
/// as `<dir>/<file_tag>.{crt,key}`.
TestCert CreateCa(const std::filesystem::path& dir,
                  const std::string& file_tag,
                  const std::string& common_name,
                  std::uint64_t serial) {
  EvpPkeyPtr key = GenerateEcKey();

  X509Ptr cert(X509_new());
  if (!cert) {
    ThrowOpenSslError("X509_new");
  }
  X509_set_version(cert.get(), 2);  // X.509v3
  ASN1_INTEGER_set_int64(X509_get_serialNumber(cert.get()), static_cast<std::int64_t>(serial));
  X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60);
  X509_gmtime_adj(X509_getm_notAfter(cert.get()), kTenYearsSeconds);
  X509_set_pubkey(cert.get(), key.get());
  SetCommonName(cert.get(), common_name);
  X509_set_issuer_name(cert.get(), X509_get_subject_name(cert.get()));  // self-signed

  AddExtension(cert.get(), cert.get(), NID_basic_constraints, "critical,CA:TRUE");
  AddExtension(cert.get(), cert.get(), NID_key_usage, "critical,keyCertSign,cRLSign");

  if (X509_sign(cert.get(), key.get(), EVP_sha384()) == 0) {
    ThrowOpenSslError("sign CA cert " + file_tag);
  }

  TestCert out;
  out.cert_file = dir / (file_tag + ".crt");
  out.key_file = dir / (file_tag + ".key");
  WriteKeyPem(out.key_file, key.get());
  WriteCertPem(out.cert_file, cert.get());
  return out;
}

}  // namespace

TestPki::TestPki()
    : dir_(MakeTempDir()),
      next_serial_(1),
      ca_(CreateCa(dir_, "ca", "mqtls test CA", next_serial_++)) {}

TestPki::~TestPki() {
  std::error_code ec;
  std::filesystem::remove_all(dir_, ec);  // best-effort; destructors must not throw
}

const TestCert& TestPki::foreign_ca() {
  if (!foreign_ca_) {
    foreign_ca_ = std::make_unique<TestCert>(
        CreateCa(dir_, "foreign_ca", "mqtls test foreign CA", next_serial_++));
  }
  return *foreign_ca_;
}

TestCert TestPki::MakeLeaf(const std::string& file_tag,
                           const std::string& common_name,
                           const std::string& san_dns,
                           const std::string& eku,
                           const TestCert& issuer,
                           bool rsa_subject_key,
                           long not_before_offset_seconds,
                           long not_after_offset_seconds) {
  EvpPkeyPtr subject_key = rsa_subject_key ? GenerateRsaKey() : GenerateEcKey();
  X509Ptr issuer_cert = LoadCertPem(issuer.cert_file);
  EvpPkeyPtr issuer_key = LoadKeyPem(issuer.key_file);

  X509Ptr cert(X509_new());
  if (!cert) {
    ThrowOpenSslError("X509_new");
  }
  const std::uint64_t serial = next_serial_++;
  X509_set_version(cert.get(), 2);  // X.509v3
  ASN1_INTEGER_set_int64(X509_get_serialNumber(cert.get()), static_cast<std::int64_t>(serial));
  X509_gmtime_adj(X509_getm_notBefore(cert.get()), not_before_offset_seconds);
  X509_gmtime_adj(X509_getm_notAfter(cert.get()), not_after_offset_seconds);
  X509_set_pubkey(cert.get(), subject_key.get());
  SetCommonName(cert.get(), common_name);
  X509_set_issuer_name(cert.get(), X509_get_subject_name(issuer_cert.get()));

  AddExtension(cert.get(), issuer_cert.get(), NID_basic_constraints, "critical,CA:FALSE");
  AddExtension(
      cert.get(), issuer_cert.get(), NID_key_usage, "critical,digitalSignature,keyEncipherment");
  if (!eku.empty()) {
    AddExtension(cert.get(), issuer_cert.get(), NID_ext_key_usage, eku);
  }
  if (!san_dns.empty()) {
    AddExtension(cert.get(), issuer_cert.get(), NID_subject_alt_name, "DNS:" + san_dns);
  }

  if (X509_sign(cert.get(), issuer_key.get(), EVP_sha384()) == 0) {
    ThrowOpenSslError("sign " + file_tag);
  }

  TestCert out;
  out.cert_file = dir_ / (file_tag + "-" + std::to_string(serial) + ".crt");
  out.key_file = dir_ / (file_tag + "-" + std::to_string(serial) + ".key");
  WriteKeyPem(out.key_file, subject_key.get());
  WriteCertPem(out.cert_file, cert.get());
  return out;
}

TestCert TestPki::unit(std::uint32_t id) {
  const std::string name = "unit-" + std::to_string(id);
  return MakeLeaf(name,
                  name,
                  name,
                  "serverAuth,clientAuth",
                  ca_,
                  /*rsa_subject_key=*/false,
                  -60,
                  kOneYearSeconds);
}

TestCert TestPki::unit_signed_by_foreign_ca(std::uint32_t id) {
  const std::string name = "unit-" + std::to_string(id);
  return MakeLeaf("foreign-" + name,
                  name,
                  name,
                  "serverAuth,clientAuth",
                  foreign_ca(),
                  /*rsa_subject_key=*/false,
                  -60,
                  kOneYearSeconds);
}

TestCert TestPki::expired(std::uint32_t id) {
  const std::string name = "unit-" + std::to_string(id);
  return MakeLeaf("expired-" + name,
                  name,
                  name,
                  "serverAuth,clientAuth",
                  ca_,
                  /*rsa_subject_key=*/false,
                  -2 * kOneYearSeconds,
                  -kOneYearSeconds);
}

TestCert TestPki::not_yet_valid(std::uint32_t id) {
  const std::string name = "unit-" + std::to_string(id);
  return MakeLeaf("not-yet-valid-" + name,
                  name,
                  name,
                  "serverAuth,clientAuth",
                  ca_,
                  /*rsa_subject_key=*/false,
                  kThirtyDaysSeconds,
                  kOneYearSeconds + kThirtyDaysSeconds);
}

TestCert TestPki::wrong_unit(std::uint32_t actual_id) {
  const std::string name = "unit-" + std::to_string(actual_id);
  return MakeLeaf("wrong-unit-" + name,
                  name,
                  name,
                  "serverAuth,clientAuth",
                  ca_,
                  /*rsa_subject_key=*/false,
                  -60,
                  kOneYearSeconds);
}

TestCert TestPki::wrong_eku(std::uint32_t id) {
  const std::string name = "unit-" + std::to_string(id);
  return MakeLeaf("wrong-eku-" + name,
                  name,
                  name,
                  "clientAuth",
                  ca_,
                  /*rsa_subject_key=*/false,
                  -60,
                  kOneYearSeconds);
}

TestCert TestPki::rsa_unit(std::uint32_t id) {
  const std::string name = "unit-" + std::to_string(id);
  return MakeLeaf("rsa-" + name,
                  name,
                  name,
                  "serverAuth,clientAuth",
                  ca_,
                  /*rsa_subject_key=*/true,
                  -60,
                  kOneYearSeconds);
}

}  // namespace mqtls::test
