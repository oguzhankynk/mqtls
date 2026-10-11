#include "tls/context.hpp"

#include <openssl/err.h>

#include <utility>

namespace mqtls::tls {

namespace {

Result<void> configure_algorithms(SSL_CTX* ctx, const TlsConfig& config) {
  if (SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION) != 1 ||
      SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION) != 1) {
    return tl::make_unexpected(make_error_code(errc::tls_config_failed));
  }
  if (SSL_CTX_set_ciphersuites(ctx, config.ciphersuites.c_str()) != 1 ||
      SSL_CTX_set1_groups_list(ctx, config.groups.c_str()) != 1 ||
      SSL_CTX_set1_sigalgs_list(ctx, config.sigalgs.c_str()) != 1) {
    return tl::make_unexpected(make_error_code(errc::tls_config_failed));
  }
  return {};
}

Result<void> load_credentials(SSL_CTX* ctx, const TlsConfig& config) {
  if (SSL_CTX_load_verify_locations(ctx, config.ca_file.c_str(), nullptr) != 1) {
    return tl::make_unexpected(make_error_code(errc::tls_config_failed));
  }
  if (SSL_CTX_use_certificate_chain_file(ctx, config.cert_file.c_str()) != 1) {
    return tl::make_unexpected(make_error_code(errc::tls_config_failed));
  }
  if (SSL_CTX_use_PrivateKey_file(ctx, config.key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
    return tl::make_unexpected(make_error_code(errc::tls_config_failed));
  }
  if (SSL_CTX_check_private_key(ctx) != 1) {
    return tl::make_unexpected(make_error_code(errc::tls_config_failed));
  }
  return {};
}

}  // namespace

Context::Context(SslCtxPtr ctx) noexcept : ctx_(std::move(ctx)) {}

Result<Context> Context::make(const TlsConfig& config, Role role) {
  if (auto validated = validate(config); !validated) {
    return tl::make_unexpected(validated.error());
  }

  // Every failure path below returns before any OpenSSL error this call
  // raised could be meaningful to a caller (there is no logging facility
  // yet to surface it through); clear it so a later, unrelated OpenSSL
  // call doesn't appear to have failed because of it.
  ::ERR_clear_error();

  SslCtxPtr ctx(SSL_CTX_new(TLS_method()));
  if (!ctx) {
    ::ERR_clear_error();
    return tl::make_unexpected(make_error_code(errc::tls_config_failed));
  }

  if (auto configured = configure_algorithms(ctx.get(), config); !configured) {
    ::ERR_clear_error();
    return tl::make_unexpected(configured.error());
  }

  SSL_CTX_set_options(ctx.get(),
                      SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET);
  SSL_CTX_set_mode(ctx.get(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

  if (auto loaded = load_credentials(ctx.get(), config); !loaded) {
    ::ERR_clear_error();
    return tl::make_unexpected(loaded.error());
  }

  // Mutual TLS, always: SSL_VERIFY_PEER makes either role
  // verify the other side's certificate. SSL_VERIFY_FAIL_IF_NO_PEER_CERT
  // only has meaning for a server (it rejects a handshake where the client
  // didn't present a certificate); a client always sends its own
  // certificate in response to the server's CertificateRequest regardless
  // of its own verify mode, so no equivalent flag exists for that side.
  int verify_mode = SSL_VERIFY_PEER;
  if (role == Role::server) {
    verify_mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
  }
  SSL_CTX_set_verify(ctx.get(), verify_mode, nullptr);

  return Context(std::move(ctx));
}

SSL_CTX* Context::native() const noexcept {
  return ctx_.get();
}

}  // namespace mqtls::tls
