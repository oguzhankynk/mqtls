#ifndef MQTLS_MQ_MESSAGE_QUEUE_HPP
#define MQTLS_MQ_MESSAGE_QUEUE_HPP

#include "mqtls/error.hpp"
#include "mqtls/span.hpp"

#include <cstddef>
#include <string>
#include <sys/types.h>  // mode_t

#include <mqueue.h>

namespace mqtls::mq {

/// @brief Which end of a POSIX message queue a MessageQueue handle opens.
enum class Direction {
  read_only,   ///< Opened `O_RDONLY`.
  write_only,  ///< Opened `O_WRONLY`.
};

/// @brief A move-only RAII handle around a POSIX message queue (`mq_open(3)`).
///
/// Always non-blocking (`O_NONBLOCK`) and close-on-exec (`O_CLOEXEC`):
/// mqtls's own wait loop (`mqtls::util::wait_for`) supplies the blocking
/// behavior via `poll(2)` on native_fd().
class MessageQueue {
public:
  /// @brief Creates a new queue, or attaches to an existing one with matching attributes.
  ///
  /// If the queue already exists and `exclusive` is false, this attaches to
  /// it and checks that its `mq_msgsize` matches `message_size`; mismatch is
  /// reported as errc::queue_attr_mismatch rather than silently using
  /// whatever size the existing queue happens to have.
  ///
  /// @param name          The queue's name, as `mq_open(3)` expects it (leading `/`).
  /// @param max_messages  Requested `mq_attr::mq_maxmsg`, used only if the queue does not yet
  /// exist.
  /// @param message_size  Requested `mq_attr::mq_msgsize`; always checked against the queue's
  /// actual value.
  /// @param mode          Filesystem permission bits, used only if the queue does not yet exist.
  /// @param direction     Which end to open.
  /// @param exclusive     If true, adds `O_EXCL`: fails if the queue already exists (the
  ///                      failure's `std::error_code` is then equivalent to
  ///                      `std::errc::file_exists`, letting a caller detect and handle a
  ///                      stale queue itself, e.g. mq::QueuePair).
  /// @return The handle; or a `std::error_code` from `mq_open(3)`'s `errno` (generic
  /// category — compare against `std::errc` values, e.g. `std::errc::file_exists`), or
  /// errc::queue_attr_mismatch.
  /// @par Thread safety
  /// Safe to call from any thread.
  /// @par Blocking
  /// Never blocks (the syscall itself is not a blocking open).
  static Result<MessageQueue> create(const std::string& name,
                                     long max_messages,
                                     long message_size,
                                     mode_t mode,
                                     Direction direction,
                                     bool exclusive = false);

  /// @brief Opens an existing queue; never creates one.
  /// @param name      The queue's name, as `mq_open(3)` expects it.
  /// @param direction Which end to open.
  /// @return The handle; or a `std::error_code` from `mq_open(3)`'s `errno`
  /// (generic category — a non-existent queue is equivalent to
  /// `std::errc::no_such_file_or_directory`).
  /// @par Thread safety
  /// Safe to call from any thread.
  /// @par Blocking
  /// Never blocks.
  static Result<MessageQueue> open(const std::string& name, Direction direction);

  /// @brief Removes a queue's name (`mq_unlink(3)`); does not require it to be open.
  /// @param name The queue's name to remove.
  /// @return `{}` on success; otherwise a `std::error_code` from `errno`.
  /// @par Thread safety
  /// Safe to call from any thread.
  /// @par Blocking
  /// Never blocks.
  static Result<void> unlink(const std::string& name);

  MessageQueue(const MessageQueue&) = delete;
  MessageQueue& operator=(const MessageQueue&) = delete;

  /// @brief Transfers ownership of `other`'s descriptor; `other` becomes closed.
  /// @param other The MessageQueue to move from.
  MessageQueue(MessageQueue&& other) noexcept;

  /// @brief Closes this object's own descriptor (if any) and takes ownership
  /// of `other`'s; `other` becomes closed.
  /// @param other The MessageQueue to move from.
  /// @return `*this`.
  MessageQueue& operator=(MessageQueue&& other) noexcept;

  /// @brief Closes the underlying descriptor, if any (`mq_close(3)`). Does
  /// not unlink the queue's name.
  ~MessageQueue();

  /// @brief Attempts to enqueue `data` without blocking (`mq_send(3)`).
  /// @param data The message to send; must not exceed message_size().
  /// @return The number of bytes sent (always `data.size()` on success —
  /// POSIX message queues do not do partial sends); errc::invalid_argument
  /// if `data.size() > message_size()`; otherwise a `std::error_code` from
  /// `errno` (generic category — a full queue is equivalent to
  /// `std::errc::resource_unavailable_try_again`, i.e. `EAGAIN`).
  /// @par Thread safety
  /// Safe to call concurrently with try_receive() on the same object, but
  /// not with another try_send().
  /// @par Blocking
  /// Never blocks.
  Result<std::size_t> try_send(mqtls::span<const std::byte> data);

  /// @brief Attempts to dequeue one message without blocking (`mq_receive(3)`).
  /// @param buffer Destination for the message; must be at least message_size() bytes.
  /// @return The number of bytes received; errc::invalid_argument if
  /// `buffer.size() < message_size()`; otherwise a `std::error_code` from
  /// `errno` (generic category — an empty queue is equivalent to
  /// `std::errc::resource_unavailable_try_again`, i.e. `EAGAIN`).
  /// @par Thread safety
  /// Safe to call concurrently with try_send() on the same object, but not
  /// with another try_receive().
  /// @par Blocking
  /// Never blocks.
  Result<std::size_t> try_receive(mqtls::span<std::byte> buffer);

  /// @brief The underlying file descriptor, valid for use with `poll(2)`.
  /// @return A valid descriptor, owned by this object.
  [[nodiscard]] int native_fd() const noexcept;

  /// @brief This queue's actual `mq_attr::mq_msgsize`.
  /// @return The maximum message size, in bytes.
  [[nodiscard]] long message_size() const noexcept;

private:
  MessageQueue(mqd_t fd, long message_size) noexcept;

  mqd_t fd_;
  long message_size_;
};

}  // namespace mqtls::mq

#endif  // MQTLS_MQ_MESSAGE_QUEUE_HPP
