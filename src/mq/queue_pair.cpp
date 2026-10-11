#include "mq/queue_pair.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <system_error>
#include <thread>
#include <utility>

namespace mqtls::mq {

namespace {

// Best-effort: establish()'s error paths call this to avoid leaking a TX
// queue this process just created but is about to give up on. The result
// is intentionally discarded — there is nothing more useful to do with a
// cleanup failure than what's already happening (returning the original
// error).
void unlink_best_effort(const std::string& name) {
  [[maybe_unused]] auto unused = MessageQueue::unlink(name);
}

Result<MessageQueue> create_own_tx_queue(const MqPeerConfig& config, const std::string& tx_name) {
  auto tx = MessageQueue::create(tx_name,
                                 config.max_messages,
                                 config.message_size,
                                 config.mode,
                                 Direction::write_only,
                                 /*exclusive=*/true);
  if (tx) {
    return tx;
  }
  if (tx.error() != std::errc::file_exists) {
    return tx;
  }
  // A previous run's leftover queue:
  // this process is about to become its sole owner, so remove and retry
  // once. If another process recreates it again in the meantime, that is
  // a configuration error (two processes using the same unit ID), not
  // something to retry indefinitely for.
  if (auto unlinked = MessageQueue::unlink(tx_name); !unlinked) {
    return tl::make_unexpected(unlinked.error());
  }
  return MessageQueue::create(tx_name,
                              config.max_messages,
                              config.message_size,
                              config.mode,
                              Direction::write_only,
                              /*exclusive=*/true);
}

// Waits one poll interval (or until `cancel` is signalled, or `deadline`
// passes, whichever is first) before establish() retries opening the
// peer's queue. Returns errc::channel_closed if cancelled; otherwise `{}`
// (an elapsed poll interval and an elapsed deadline both just mean "try
// again" here — the caller checks the deadline itself on the next loop
// iteration).
Result<void> wait_one_poll_interval(const MqPeerConfig& config,
                                    const util::Deadline& deadline,
                                    const util::WakeFd* cancel) {
  const int remaining_ms = deadline.remaining_ms();
  const int poll_ms = static_cast<int>(config.peer_poll_interval.count());
  const int wait_ms = (remaining_ms < 0) ? poll_ms : std::min(poll_ms, remaining_ms);

  if (cancel == nullptr) {
    std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
    return {};
  }

  // A negative fd is ignored by poll(2) (POSIX-documented behavior): this
  // is purely a cancellable sleep, there is no real descriptor to wait on
  // while the peer's queue doesn't exist yet.
  const util::Deadline sleep_deadline{Timeout{wait_ms}};
  auto waited = util::wait_for(-1, 0, *cancel, sleep_deadline);
  if (!waited && waited.error() == make_error_code(errc::channel_closed)) {
    return tl::make_unexpected(waited.error());
  }
  return {};
}

}  // namespace

Result<QueuePair> QueuePair::establish(const MqPeerConfig& config,
                                       util::Deadline deadline,
                                       const util::WakeFd* cancel) {
  if (auto validated = validate(config); !validated) {
    return tl::make_unexpected(validated.error());
  }

  const std::string tx_name = tx_queue_name(config);
  const std::string rx_name = rx_queue_name(config);

  auto tx = create_own_tx_queue(config, tx_name);
  if (!tx) {
    return tl::make_unexpected(tx.error());
  }

  for (;;) {
    auto rx = MessageQueue::open(rx_name, Direction::read_only);
    if (rx) {
      return QueuePair(std::move(*tx), std::move(*rx), tx_name);
    }
    if (rx.error() != std::errc::no_such_file_or_directory) {
      unlink_best_effort(tx_name);
      return tl::make_unexpected(rx.error());
    }
    if (deadline.expired()) {
      unlink_best_effort(tx_name);
      return tl::make_unexpected(make_error_code(errc::timeout));
    }
    if (auto waited = wait_one_poll_interval(config, deadline, cancel); !waited) {
      unlink_best_effort(tx_name);
      return tl::make_unexpected(waited.error());
    }
  }
}

QueuePair::QueuePair(MessageQueue tx, MessageQueue rx, std::string tx_name)
    : tx_(std::move(tx)),
      rx_(std::move(rx)),
      tx_name_(std::move(tx_name)),
      rx_scratch_(static_cast<std::size_t>(rx_.message_size())) {}

QueuePair::QueuePair(QueuePair&& other) noexcept
    : tx_(std::move(other.tx_)),
      rx_(std::move(other.rx_)),
      tx_name_(std::move(other.tx_name_)),
      rx_scratch_(std::move(other.rx_scratch_)),
      rx_pos_(other.rx_pos_),
      rx_used_(other.rx_used_) {
  other.tx_name_.clear();
}

QueuePair& QueuePair::operator=(QueuePair&& other) noexcept {
  if (this != &other) {
    if (!tx_name_.empty()) {
      unlink_best_effort(tx_name_);
    }
    tx_ = std::move(other.tx_);
    rx_ = std::move(other.rx_);
    tx_name_ = std::move(other.tx_name_);
    rx_scratch_ = std::move(other.rx_scratch_);
    rx_pos_ = other.rx_pos_;
    rx_used_ = other.rx_used_;
    other.tx_name_.clear();
  }
  return *this;
}

QueuePair::~QueuePair() {
  if (!tx_name_.empty()) {
    unlink_best_effort(tx_name_);
  }
}

Result<std::size_t> QueuePair::write_some(mqtls::span<const std::byte> data) {
  const std::size_t chunk = std::min(data.size(), static_cast<std::size_t>(tx_.message_size()));
  auto sent = tx_.try_send(data.subspan(0, chunk));
  if (sent) {
    return sent;
  }
  if (sent.error() == std::errc::resource_unavailable_try_again) {
    return std::size_t{0};
  }
  return tl::make_unexpected(make_error_code(errc::io_error));
}

Result<std::size_t> QueuePair::read_some(mqtls::span<std::byte> buffer) {
  if (rx_pos_ == rx_used_) {
    auto received = rx_.try_receive(rx_scratch_);
    if (!received) {
      if (received.error() == std::errc::resource_unavailable_try_again) {
        return std::size_t{0};
      }
      return tl::make_unexpected(make_error_code(errc::io_error));
    }
    rx_pos_ = 0;
    rx_used_ = *received;
  }

  const std::size_t n = std::min(buffer.size(), rx_used_ - rx_pos_);
  std::copy_n(rx_scratch_.begin() + static_cast<std::ptrdiff_t>(rx_pos_), n, buffer.begin());
  rx_pos_ += n;
  return n;
}

int QueuePair::read_fd() const noexcept {
  return rx_.native_fd();
}

int QueuePair::write_fd() const noexcept {
  return tx_.native_fd();
}

std::size_t QueuePair::pending_read() const noexcept {
  return rx_used_ - rx_pos_;
}

}  // namespace mqtls::mq
