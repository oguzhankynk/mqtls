#ifndef MQTLS_TLS_OPENSSL_RAII_HPP
#define MQTLS_TLS_OPENSSL_RAII_HPP

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <memory>

/// @brief The core's TLS layer: OpenSSL plumbing. Takes a
/// `mqtls::transport::ByteTransport&` and a `mqtls::TlsConfig`/`mqtls::Role`
/// and knows nothing about where the transport's bytes actually go.
namespace mqtls::tls {

/// @brief A `std::unique_ptr` deleter that calls a fixed free function,
/// e.g. `FunctionDeleter<&SSL_CTX_free>`. Used instead of hand-written
/// deleter structs for every OpenSSL handle type below.
template <auto Deleter>
struct FunctionDeleter {
  /// @brief Frees `ptr` via the bound free function, if non-null.
  /// @param ptr The handle to free; safe to be null.
  template <class T>
  void operator()(T* ptr) const noexcept {
    if (ptr != nullptr) {
      Deleter(ptr);
    }
  }
};

/// @brief Owns an `SSL_CTX*`, freeing it with `SSL_CTX_free(3)`.
using SslCtxPtr = std::unique_ptr<SSL_CTX, FunctionDeleter<&SSL_CTX_free>>;

/// @brief Owns an `SSL*`, freeing it with `SSL_free(3)`.
using SslPtr = std::unique_ptr<SSL, FunctionDeleter<&SSL_free>>;

/// @brief Owns a `BIO*`, freeing it with `BIO_free(3)`.
///
/// Only used on BIO error paths before a BIO is handed to `SSL_set_bio()`
/// (which transfers ownership to the `SSL*`); once attached, the `SSL*`
/// owns it and this type must not be used on it again.
using BioPtr = std::unique_ptr<BIO, FunctionDeleter<&BIO_free>>;

/// @brief Owns a `BIO_METHOD*`, freeing it with `BIO_meth_free(3)`.
using BioMethodPtr = std::unique_ptr<BIO_METHOD, FunctionDeleter<&BIO_meth_free>>;

/// @brief Owns an `X509*`, freeing it with `X509_free(3)`.
using X509Ptr = std::unique_ptr<X509, FunctionDeleter<&X509_free>>;

/// @brief Owns an `EVP_PKEY*`, freeing it with `EVP_PKEY_free(3)`.
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, FunctionDeleter<&EVP_PKEY_free>>;

}  // namespace mqtls::tls

#endif  // MQTLS_TLS_OPENSSL_RAII_HPP
