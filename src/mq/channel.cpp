#include "mqtls/mq/channel.hpp"

#include <memory>
#include <utility>

#include "mq/queue_pair.hpp"
#include "util/deadline.hpp"

namespace mqtls::mq {

Result<SecureChannel> establish(const MqPeerConfig& peer_config,
                                const TlsConfig& tls_config,
                                const ChannelOptions& options) {
  if (auto validated = validate(peer_config); !validated) {
    return tl::make_unexpected(validated.error());
  }

  auto queue_pair =
      QueuePair::establish(peer_config, util::Deadline(peer_config.peer_wait_timeout));
  if (!queue_pair) {
    return tl::make_unexpected(queue_pair.error());
  }

  auto transport = std::make_unique<QueuePair>(std::move(*queue_pair));
  return SecureChannel::create(std::move(transport),
                               tls_role(peer_config),
                               expected_peer_name(peer_config),
                               tls_config,
                               options);
}

}  // namespace mqtls::mq
