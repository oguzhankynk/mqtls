#ifndef MQTLS_TLS_BIO_HPP
#define MQTLS_TLS_BIO_HPP

#include "mqtls/transport.hpp"

#include <openssl/bio.h>

namespace mqtls::tls {

/// @brief Creates a `BIO` that reads from and writes to `transport`: every
/// OpenSSL read/write call on it goes straight to
/// `transport.read_some()`/`write_some()`.
///
/// The `BIO` does not take ownership of `transport` — `transport` must
/// outlive the `BIO` and whichever `SSL*` it gets attached to
/// (`SSL_set_bio()`); nothing here ever calls `delete` or any transport
/// method that would release it.
///
/// Would-block (`ByteTransport` returning 0) sets the matching OpenSSL
/// retry flag (`BIO_set_retry_read()`/`BIO_set_retry_write()`) and returns
/// -1, so `SSL_get_error()` reports `SSL_ERROR_WANT_READ`/`WANT_WRITE`. A
/// confirmed end of stream (errc::truncated) or any other
/// transport failure returns -1 without setting a retry flag, so OpenSSL
/// treats it as `SSL_ERROR_SYSCALL`.
///
/// @param transport The transport this `BIO` wraps.
/// @return A new `BIO` with a reference count of 1; `nullptr` if OpenSSL
/// failed to allocate one.
/// @par Thread safety
/// Safe to call from any thread. The *method* backing the returned `BIO` is
/// shared process-wide and created at most once (`std::call_once`); the
/// returned `BIO` itself follows the same single-owning-thread contract as
/// `transport` (`SecureChannel` serializes all SSL/BIO
/// use through `ssl_mtx_`).
BIO* make_bio(transport::ByteTransport& transport);

}  // namespace mqtls::tls

#endif  // MQTLS_TLS_BIO_HPP
