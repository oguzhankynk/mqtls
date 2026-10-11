#ifndef MQTLS_CONFIG_HPP
#define MQTLS_CONFIG_HPP

#include "mqtls/error.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>

namespace mqtls {

/// @brief Which side of the TLS handshake a channel plays.
///
/// How a role is decided is entirely up to whichever transport module sets
/// up the connection — the core neither knows nor cares;
/// it is simply told which role to play.
enum class Role {
  server,  ///< Calls `SSL_set_accept_state()` internally.
  client,  ///< Calls `SSL_set_connect_state()` internally.
};

/// @brief Certificate material and the TLS algorithm profile for a channel.
///
/// This type carries no notion of peer identity: the string a channel
/// verifies its peer's certificate against is passed directly to
/// SecureChannel::create() by whichever transport module established the
/// connection. A unit may play either TLS role
/// depending on which peer it talks to, so certificates should carry both
/// the `serverAuth` and `clientAuth` extended key usages.
struct TlsConfig {
  /// @brief PEM file with the trusted CA certificate(s).
  std::filesystem::path ca_file;

  /// @brief PEM file with this unit's certificate (chain).
  std::filesystem::path cert_file;

  /// @brief PEM file with this unit's private key, matching `cert_file`.
  std::filesystem::path key_file;

  /// @brief TLS 1.3 ciphersuite list, as accepted by `SSL_CTX_set_ciphersuites(3)`.
  std::string ciphersuites = "TLS_AES_256_GCM_SHA384";

  /// @brief Accepted key-exchange groups, as accepted by `SSL_CTX_set1_groups_list(3)`.
  std::string groups = "P-384";

  /// @brief Accepted signature algorithms, as accepted by `SSL_CTX_set1_sigalgs_list(3)`.
  std::string sigalgs = "ECDSA+SHA384";
};

/// @brief Checks `config` against the contract documented on TlsConfig's
/// fields that can be verified without touching the filesystem or OpenSSL
/// (non-empty paths and algorithm strings). Certificate/key loading errors
/// are reported separately, by `tls::Context::make()`, as errc::tls_config_failed.
/// @param config The configuration to check.
/// @return `{}` if valid; otherwise errc::invalid_argument.
/// @par Thread safety
/// Safe to call from any thread; reads only `config`.
Result<void> validate(const TlsConfig& config);

/// @brief Per-channel limits and deadlines that apply once a transport is
/// already connected.
///
/// This deliberately excludes anything about *finding* a peer (e.g. how
/// long to wait for one to appear): that is a transport-specific connection
/// step that happens before SecureChannel::create() is ever called.
struct ChannelOptions {
  /// @brief Largest application message SecureChannel::send()/recv() will
  /// transmit or accept. Exceeding it is reported as errc::message_too_large
  ///; on recv(), since this means the channel can no
  /// longer trust the byte stream, the channel is also closed. Must fit in
  /// 32 bits (validate() enforces this): SecureChannel's frame header can't
  /// encode anything larger.
  std::size_t max_message_size = 1U << 20;

  /// @brief How long the TLS handshake itself may take, once the transport
  /// is connected.
  Timeout handshake_timeout = std::chrono::seconds(10);
};

/// @brief Checks `options` against the contract documented on
/// ChannelOptions's fields (all values must be positive; `handshake_timeout`
/// may additionally be kInfinite).
/// @param options The options to check.
/// @return `{}` if valid; otherwise errc::invalid_argument.
/// @par Thread safety
/// Safe to call from any thread; reads only `options`.
Result<void> validate(const ChannelOptions& options);

}  // namespace mqtls

#endif  // MQTLS_CONFIG_HPP
