#include "mqtls/mq/config.hpp"

#include <algorithm>
#include <climits>

namespace mqtls::mq {

namespace {

// Linux's mqueue pseudo-filesystem (/dev/mqueue) enforces this like any other
// filesystem component; mq_open(3) gives no separate, documented limit.
constexpr std::size_t kQueueNameMax = NAME_MAX;

// mq_open(3) rejects anything smaller in practice on Linux; also keeps
// message_size comfortably above the 4-byte framing header SecureChannel
// will add on top.
constexpr long kMinimumMessageSize = 512;

}  // namespace

std::string tx_queue_name(const MqPeerConfig& config) {
  return config.queue_prefix + std::to_string(config.local_unit_id) + "_tx";
}

std::string rx_queue_name(const MqPeerConfig& config) {
  return config.queue_prefix + std::to_string(config.peer_unit_id) + "_tx";
}

Role tls_role(const MqPeerConfig& config) noexcept {
  return config.local_unit_id < config.peer_unit_id ? Role::server : Role::client;
}

std::string expected_peer_name(const MqPeerConfig& config) {
  return config.peer_name_prefix + std::to_string(config.peer_unit_id);
}

Result<void> validate(const MqPeerConfig& config) {
  if (config.local_unit_id == config.peer_unit_id) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  if (config.queue_prefix.empty() || config.queue_prefix.front() != '/') {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  if (config.queue_prefix.find('/', 1) != std::string::npos) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  if (config.max_messages <= 0 || config.message_size < kMinimumMessageSize) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  if (config.peer_name_prefix.empty()) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  if (config.peer_poll_interval <= Timeout::zero()) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  if (config.peer_wait_timeout != kInfinite && config.peer_wait_timeout <= Timeout::zero()) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  // Both names share the same prefix length; the longer unit ID dominates.
  const std::size_t longest_name =
      std::max(tx_queue_name(config).size(), rx_queue_name(config).size());
  if (longest_name > kQueueNameMax) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  return {};
}

}  // namespace mqtls::mq
