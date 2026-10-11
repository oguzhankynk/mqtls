#ifndef MQTLS_MQ_CONFIG_HPP
#define MQTLS_MQ_CONFIG_HPP

#include "mqtls/config.hpp"
#include "mqtls/error.hpp"

#include <cstdint>
#include <string>
#include <sys/types.h>  // mode_t

/// @brief The POSIX message-queue transport binding: the
/// first, and currently only, concrete implementation of
/// `mqtls::transport::ByteTransport`. Everything in this namespace is
/// specific to POSIX mqueues; the core (`mqtls`) has no dependency on it.
namespace mqtls::mq {

/// @brief A peer's numeric identity. Determines both the
/// message queue names and, by comparison with the local unit's ID, the TLS
/// role (tls_role()). This identity scheme is specific to this transport
/// module, not a core mqtls concept.
using UnitId = std::uint32_t;

/// @brief Identifies the two ends of a peer-to-peer mqtls connection over
/// POSIX message queues, and the queues that carry it.
///
/// Each unit creates and owns exactly one queue, named `<queue_prefix><id>_tx`
/// (tx_queue_name()), and writes to it. It reads from the peer's TX queue
/// (rx_queue_name()), opening it once the peer has created it. There is no
/// `_rx`-named queue.
struct MqPeerConfig {
  /// @brief This process's unit ID. Used to build this process's TX queue name.
  UnitId local_unit_id{};

  /// @brief The peer's unit ID. Used to build the queue this process reads
  /// from, and to determine the TLS role via tls_role(). Must differ from
  /// `local_unit_id`.
  UnitId peer_unit_id{};

  /// @brief Prefix for both queue names. Must start with `/` and contain no
  /// other `/`, per `mq_open(3)`'s naming rules.
  std::string queue_prefix = "/tls_msgQ_id_";

  /// @brief Queue depth, passed as `mq_attr::mq_maxmsg`. Must not exceed
  /// `/proc/sys/fs/mqueue/msg_max` for a non-privileged process.
  long max_messages = 10;

  /// @brief Maximum bytes per queue message, passed as `mq_attr::mq_msgsize`.
  /// Must not exceed `/proc/sys/fs/mqueue/msgsize_max` for a non-privileged
  /// process.
  long message_size = 8192;

  /// @brief Filesystem permission bits the TX queue is created with.
  mode_t mode = 0600;

  /// @brief Prefix used to compute the expected peer identity
  /// (expected_peer_name()): the peer's certificate must carry
  /// `peer_name_prefix + to_string(peer_unit_id)` as a subject alternative
  /// name (or, failing that, as its CN).
  std::string peer_name_prefix = "unit-";

  /// @brief How long establish() waits for the peer's TX queue to appear
  /// before giving up with errc::timeout. kInfinite waits forever.
  Timeout peer_wait_timeout = kInfinite;

  /// @brief How often establish() retries opening the peer's TX queue while
  /// waiting (queue absence can't be poll()ed directly,
  /// so this is a bounded retry loop, not a blocking wait).
  Timeout peer_poll_interval = std::chrono::milliseconds(50);
};

/// @brief Checks `config` against the contract documented on MqPeerConfig's
/// fields, including that the longer of tx_queue_name()/rx_queue_name()
/// fits within `NAME_MAX`.
/// @param config The configuration to check.
/// @return `{}` if valid; otherwise errc::invalid_argument.
/// @par Thread safety
/// Safe to call from any thread; reads only `config`.
Result<void> validate(const MqPeerConfig& config);

/// @brief The name of the queue this unit creates and writes to.
/// @param config The configuration to derive the name from.
/// @return `config.queue_prefix + to_string(config.local_unit_id) + "_tx"`.
/// @par Thread safety
/// Safe to call from any thread.
std::string tx_queue_name(const MqPeerConfig& config);

/// @brief The name of the queue this unit reads from: the peer's TX queue.
/// @param config The configuration to derive the name from.
/// @return `config.queue_prefix + to_string(config.peer_unit_id) + "_tx"`.
/// @par Thread safety
/// Safe to call from any thread.
std::string rx_queue_name(const MqPeerConfig& config);

/// @brief The TLS role implied by `config`: the unit with
/// the lower `local_unit_id`/`peer_unit_id` is always the server.
/// @param config The configuration to derive the role from.
/// @return Role::server if `local_unit_id < peer_unit_id`, else Role::client.
/// @pre `config.local_unit_id != config.peer_unit_id` (undefined which role
/// is returned otherwise; validate() rejects such a config).
/// @par Thread safety
/// Safe to call from any thread.
[[nodiscard]] Role tls_role(const MqPeerConfig& config) noexcept;

/// @brief The peer identity `config` expects,
/// passed directly to the core as `SecureChannel::create()`'s
/// `expected_peer_name` parameter.
/// @param config The configuration to derive the name from.
/// @return `config.peer_name_prefix + to_string(config.peer_unit_id)`.
/// @par Thread safety
/// Safe to call from any thread.
std::string expected_peer_name(const MqPeerConfig& config);

}  // namespace mqtls::mq

#endif  // MQTLS_MQ_CONFIG_HPP
