#ifndef MQTLS_MQ_CHANNEL_HPP
#define MQTLS_MQ_CHANNEL_HPP

#include "mqtls/config.hpp"
#include "mqtls/error.hpp"
#include "mqtls/mq/config.hpp"
#include "mqtls/secure_channel.hpp"

namespace mqtls::mq {

/// @brief Establishes a peer-to-peer secure channel over POSIX message
/// queues: creates this unit's own TX queue, waits for the peer's to
/// appear, runs the TLS 1.3 handshake, and returns a channel ready for
/// `send()`/`recv()`. This is the only place that connects the message-
/// queue transport to the core's `SecureChannel`.
/// @param peer_config Identifies both ends, the queues, and how long to
/// wait for the peer.
/// @param tls_config  Certificate/key files and algorithm profile.
/// @param options     Message-size limit and handshake deadline.
/// @return The channel; errc::invalid_argument for a bad `peer_config`;
/// otherwise whatever the underlying queue setup or TLS handshake failed
/// with (including errc::timeout from either stage).
/// @par Thread safety
/// Safe to call from any thread.
/// @par Blocking
/// Blocks until connected and the handshake completes, or either stage's
/// own deadline passes.
Result<SecureChannel> establish(const MqPeerConfig& peer_config,
                                const TlsConfig& tls_config,
                                const ChannelOptions& options = {});

}  // namespace mqtls::mq

#endif  // MQTLS_MQ_CHANNEL_HPP
