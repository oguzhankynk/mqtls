#ifndef MQTLS_TEST_SUPPORT_CHANNEL_FIXTURE_HPP
#define MQTLS_TEST_SUPPORT_CHANNEL_FIXTURE_HPP

#include "mqtls/mq.hpp"

namespace mqtls::test {

/// @brief The result of running mq::establish() for both ends of a
/// connection concurrently (see EstablishBothSides()).
struct ChannelPair {
  Result<SecureChannel> local;
  Result<SecureChannel> peer;
};

/// @brief Builds a matching pair of mq::MqPeerConfig for unit IDs 100 and
/// 101 sharing `queue_prefix` (typically from unique_queue_prefix(), so
/// tests never collide).
std::pair<mq::MqPeerConfig, mq::MqPeerConfig> MakeConfigPair(const std::string& queue_prefix,
                                                             mq::UnitId local_id = 100,
                                                             mq::UnitId peer_id = 101);

/// @brief Runs `mq::establish(local_config, local_tls, options)` and
/// `mq::establish(peer_config, peer_tls, options)` concurrently (each on
/// its own thread, since both sides' establish() blocks until the other's
/// queue appears and the handshake completes) and returns both outcomes.
ChannelPair EstablishBothSides(const mq::MqPeerConfig& local_config,
                               const TlsConfig& local_tls,
                               const mq::MqPeerConfig& peer_config,
                               const TlsConfig& peer_tls,
                               const ChannelOptions& options = {});

}  // namespace mqtls::test

#endif  // MQTLS_TEST_SUPPORT_CHANNEL_FIXTURE_HPP
