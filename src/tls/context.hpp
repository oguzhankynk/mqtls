#ifndef MQTLS_TLS_CONTEXT_HPP
#define MQTLS_TLS_CONTEXT_HPP

#include "mqtls/config.hpp"
#include "mqtls/error.hpp"

#include "tls/openssl_raii.hpp"

namespace mqtls::tls {

/// @brief Owns a fully-configured `SSL_CTX*`: TLS 1.3
/// only, the ciphersuite/group/sigalg profile from TlsConfig, mandatory
/// mutual authentication, and no renegotiation/compression/tickets.
///
/// Knows nothing about `role`'s origin or about any particular peer's
/// identity — `SecureChannel::create()` (not Context) applies the expected
/// peer name to each `SSL*` it creates from this context, since that is
/// per-connection, not per-context, state.
class Context {
public:
  /// @brief Builds and configures a new TLS context.
  /// @param config The certificate/key files and algorithm profile to apply.
  /// @param role   Which side of the handshake this context will be used
  /// for; only affects whether a missing peer certificate is fatal
  /// (servers always require one; mTLS is mandatory either
  /// way, since a client always sends its own certificate when asked).
  /// @return The context; errc::invalid_argument if `config` itself fails
  /// validate(); errc::tls_config_failed if OpenSSL rejected the algorithm
  /// profile or the certificate/key/CA files (missing file, corrupt PEM,
  /// key/certificate mismatch, ...).
  /// @par Thread safety
  /// Safe to call from any thread.
  /// @par Blocking
  /// Does filesystem I/O (loading the CA/certificate/key files); never
  /// blocks on network or synchronization.
  static Result<Context> make(const TlsConfig& config, Role role);

  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  /// @brief Transfers ownership of the underlying `SSL_CTX*`.
  Context(Context&&) noexcept = default;

  /// @brief Frees this object's own `SSL_CTX*` (if any) and takes ownership
  /// of the other one's.
  /// @return `*this`.
  Context& operator=(Context&&) noexcept = default;

  /// @brief Frees the underlying `SSL_CTX*`, if any.
  ~Context() = default;

  /// @brief The underlying `SSL_CTX*`, for building `SSL*` objects from.
  /// @return A valid pointer, owned by this object.
  /// @par Thread safety
  /// Safe to call from any thread.
  [[nodiscard]] SSL_CTX* native() const noexcept;

private:
  explicit Context(SslCtxPtr ctx) noexcept;

  SslCtxPtr ctx_;
};

}  // namespace mqtls::tls

#endif  // MQTLS_TLS_CONTEXT_HPP
