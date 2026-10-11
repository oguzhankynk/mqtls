#ifndef MQTLS_MQ_QUEUE_PAIR_HPP
#define MQTLS_MQ_QUEUE_PAIR_HPP

#include "mqtls/mq/config.hpp"
#include "mqtls/transport.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include "mq/message_queue.hpp"
#include "util/deadline.hpp"
#include "util/wait.hpp"

namespace mqtls::mq {

/// @brief The POSIX message-queue implementation of
/// `mqtls::transport::ByteTransport`: this unit's own
/// TX queue, and the peer's TX queue opened for reading.
///
/// establish() performs the whole connection-setup dance (create our TX,
/// wait for the peer's); once constructed, a QueuePair behaves like any
/// other ByteTransport and the core never needs to know it is one.
class QueuePair final : public transport::ByteTransport {
public:
  /// @brief Creates this unit's TX queue and waits for the peer's to appear.
  ///
  /// This unit's TX queue is created with `O_EXCL` first; if one already
  /// exists (a previous run's leftover), it is removed and recreated, since
  /// this process is about to become its sole owner. The peer's queue
  /// cannot be `poll()`-ed while it doesn't exist yet, so this polls
  /// `MqPeerConfig::peer_poll_interval` between attempts to open it.
  ///
  /// @param config   Identifies both ends and the queues' attributes.
  /// @param deadline How long to wait for the peer's queue to appear.
  /// @param cancel   If non-null, signalling it aborts the wait early with
  /// errc::channel_closed (checked between poll attempts, not inside a
  /// single `mq_open` call, which cannot itself be interrupted this way).
  /// @return The connected pair; errc::invalid_argument if `config` fails
  /// validate(); errc::timeout if the peer's queue never appeared;
  /// errc::channel_closed if `cancel` was signalled; otherwise a
  /// `std::error_code` from the underlying `mq::MessageQueue` call that
  /// failed. On any failure, this unit's own TX queue is removed again if
  /// it was created during this call.
  /// @par Thread safety
  /// Safe to call from any thread.
  /// @par Blocking
  /// Blocks (via sleeping/waiting, never inside a single syscall) until
  /// connected, cancelled, or `deadline` passes.
  static Result<QueuePair> establish(const MqPeerConfig& config,
                                     util::Deadline deadline,
                                     const util::WakeFd* cancel = nullptr);

  QueuePair(const QueuePair&) = delete;
  QueuePair& operator=(const QueuePair&) = delete;

  /// @brief Transfers ownership, including which side unlinks the TX queue
  /// on destruction; `other` no longer will.
  /// @param other The QueuePair to move from.
  QueuePair(QueuePair&& other) noexcept;

  /// @brief Same as the move constructor, after first releasing whatever
  /// this object owned.
  /// @param other The QueuePair to move from.
  /// @return `*this`.
  QueuePair& operator=(QueuePair&& other) noexcept;

  /// @brief Removes this unit's own TX queue (`mq_unlink(3)`), unless this
  /// object was moved from; the peer's queue, which this object only ever
  /// read from, is just closed.
  ~QueuePair() override;

  Result<std::size_t> write_some(mqtls::span<const std::byte> data) override;
  Result<std::size_t> read_some(mqtls::span<std::byte> buffer) override;
  [[nodiscard]] int read_fd() const noexcept override;
  [[nodiscard]] int write_fd() const noexcept override;
  [[nodiscard]] std::size_t pending_read() const noexcept override;

private:
  QueuePair(MessageQueue tx, MessageQueue rx, std::string tx_name);

  MessageQueue tx_;
  MessageQueue rx_;
  std::string tx_name_;
  std::vector<std::byte> rx_scratch_;
  std::size_t rx_pos_ = 0;
  std::size_t rx_used_ = 0;
};

}  // namespace mqtls::mq

#endif  // MQTLS_MQ_QUEUE_PAIR_HPP
