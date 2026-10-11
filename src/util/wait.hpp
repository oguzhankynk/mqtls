#ifndef MQTLS_UTIL_WAIT_HPP
#define MQTLS_UTIL_WAIT_HPP

#include "mqtls/error.hpp"

#include "util/deadline.hpp"

namespace mqtls::util {

/// @brief A one-shot, poll()-able wakeup signal backed by Linux `eventfd(2)`.
///
/// Used to interrupt a thread blocked in wait_for() when another thread
/// calls `SecureChannel::close()`.
class WakeFd {
public:
  /// @brief Creates the underlying eventfd(2) descriptor (non-blocking, close-on-exec).
  /// @return The WakeFd, or errc::io_error if eventfd(2) failed.
  /// @par Thread safety
  /// Safe to call from any thread.
  static Result<WakeFd> create();

  WakeFd(const WakeFd&) = delete;
  WakeFd& operator=(const WakeFd&) = delete;

  /// @brief Transfers ownership of `other`'s descriptor; `other` is left empty.
  /// @param other The WakeFd to move from.
  /// @par Thread safety
  /// Moving a WakeFd that another thread is concurrently signalling, draining,
  /// or polling via wait_for() is undefined behavior, same as for any other
  /// move-only handle.
  WakeFd(WakeFd&& other) noexcept;

  /// @brief Closes this object's own descriptor (if any) and takes ownership
  /// of `other`'s; `other` is left empty.
  /// @param other The WakeFd to move from.
  /// @return `*this`.
  /// @par Thread safety
  /// Same caveat as the move constructor above.
  WakeFd& operator=(WakeFd&& other) noexcept;

  /// @brief Closes the underlying descriptor, if any.
  ~WakeFd();

  /// @brief Signals the wakeup, waking every thread currently in wait_for()
  /// on this WakeFd. Idempotent: signalling an already-signalled WakeFd is a
  /// no-op until the next drain().
  /// @par Thread safety
  /// Safe to call concurrently with itself, drain(), and wait_for() on this
  /// object, from any thread.
  /// @par Blocking
  /// Never blocks.
  void signal() noexcept;

  /// @brief Clears a pending signal so the WakeFd can be reused.
  /// @par Thread safety
  /// Safe to call concurrently with signal(). Not safe to call concurrently
  /// with another drain() on the same object.
  /// @par Blocking
  /// Never blocks.
  void drain() noexcept;

  /// @brief The underlying file descriptor, valid for use with poll(2).
  /// @return A valid eventfd(2) descriptor, owned by this object.
  /// @par Thread safety
  /// Safe to call from any thread.
  [[nodiscard]] int fd() const noexcept {
    return fd_;
  }

private:
  explicit WakeFd(int fd) noexcept : fd_(fd) {}

  int fd_;
};

/// @brief Waits for `fd` to become ready for `events`, or for `wake` to be
/// signalled, or for `deadline` to pass, whichever happens first.
///
/// If `fd` becomes ready in the same call where `wake` was also signalled,
/// the signal takes priority: this is what lets `SecureChannel::close()`
/// reliably interrupt a thread that is otherwise about to proceed.
///
/// @param fd       The descriptor to poll (e.g. a message queue's native fd).
/// @param events   A poll(2) event mask, e.g. `POLLIN` or `POLLOUT`.
/// @param wake     Checked alongside `fd`; signal() on another thread wakes this call.
/// @param deadline Upper bound on how long to wait.
/// @return `{}` once `fd` is ready; errc::timeout if `deadline` passed first;
/// errc::channel_closed if `wake` was signalled first; errc::io_error if
/// poll(2) itself failed for a reason other than EINTR.
/// @par Thread safety
/// Safe to call concurrently from multiple threads on the same `fd`/`wake`
/// pair; every waiting thread is woken independently (`poll(2)` semantics).
/// @par Blocking
/// Blocks until one of the above happens. Retries internally on `EINTR`,
/// re-reading `deadline`'s remaining time rather than restarting it.
Result<void> wait_for(int fd, short events, const WakeFd& wake, const Deadline& deadline);

}  // namespace mqtls::util

#endif  // MQTLS_UTIL_WAIT_HPP
