#ifndef MQTLS_TRANSPORT_HPP
#define MQTLS_TRANSPORT_HPP

#include "mqtls/error.hpp"
#include "mqtls/span.hpp"

#include <cstddef>

/// @brief The pluggability seam between mqtls's core (TLS, framing,
/// thread-safety) and whatever physical communication protocol carries the
/// bytes.
///
/// `mqtls::mq` (POSIX message queues) is the first implementation this
/// library ships, but it is not special: anything that implements
/// ByteTransport — a UART link, a shared-memory ring buffer, a CAN bus
/// binding — can be handed to `SecureChannel::create()` without changing a
/// single line of the core.
namespace mqtls::transport {

/// @brief A non-blocking, byte-stream transport that mqtls's TLS layer reads
/// from and writes to.
///
/// Implementations are not required to preserve message boundaries from the
/// underlying medium (a transport's own "messages" or
/// "frames", if it has them, are not TLS records and not mqtls's own
/// application-level framing). They must, however, behave like a reliable,
/// ordered, non-blocking byte stream: every byte written, in order, must
/// eventually be delivered to the matching read_some() calls on the other
/// end, with no corruption or reordering.
///
/// All methods must be safe to call from the single thread that currently
/// owns the owning SecureChannel's TLS operations; mqtls never calls two
/// methods on the same ByteTransport concurrently (SecureChannel's own
/// `ssl_mtx_` serializes that).
///
/// @par On "no data" vs. "no more data will ever come"
/// Some media (POSIX message queues among them) have no way to detect that
/// a peer is gone short of a deadline elapsing; others (stream sockets, a
/// plausible future UART binding) can detect it positively — a `read()`
/// returning 0 after the descriptor was reported ready, rather than
/// `EAGAIN`. read_some() distinguishes these: returning `0` means "nothing
/// available *yet*, keep retrying until the caller's own deadline", while
/// returning the error errc::truncated means "confirmed, retrying is
/// pointless". Implementations that cannot tell the difference (mqtls::mq)
/// simply never return errc::truncated; callers on top of such a transport
/// will instead see an abrupt disconnect surface as errc::timeout once
/// their deadline passes. Getting this distinction wrong in a new
/// transport — e.g. mapping a stream socket's EOF to `0` instead of
/// errc::truncated — turns every read on a closed connection into a
/// zero-sleep retry loop that spins at 100% CPU until the deadline (or
/// forever, for a caller using mqtls::kInfinite).
class ByteTransport {
public:
  ByteTransport() = default;
  ByteTransport(const ByteTransport&) = delete;
  ByteTransport& operator=(const ByteTransport&) = delete;
  ByteTransport(ByteTransport&&) = delete;
  ByteTransport& operator=(ByteTransport&&) = delete;

  /// @brief Destroys the transport, releasing whatever resources it owns
  /// (e.g. closing file descriptors). Implementations that own a named
  /// resource other instances could re-create (such as a queue this end is
  /// responsible for) should remove it here.
  virtual ~ByteTransport() = default;

  /// @brief Writes as much of `data` as can be accepted right now, without blocking.
  /// @param data The bytes to write; may be partially consumed.
  /// @return The number of bytes actually written (0 is valid and means
  /// "nothing accepted, try again later"); errc::io_error if the transport
  /// has failed and cannot be used again.
  /// @par Thread safety
  /// See the class-level contract: not called concurrently with any other
  /// method on the same object.
  /// @par Blocking
  /// Must never block; returns immediately with whatever progress was made.
  virtual Result<std::size_t> write_some(mqtls::span<const std::byte> data) = 0;

  /// @brief Reads as many bytes as are available right now into `buffer`,
  /// without blocking.
  /// @param buffer Destination for the bytes read.
  /// @return The number of bytes actually read; 0 means nothing is
  /// available yet (see the class-level "no data" vs. "no more data"
  /// note — this is not end-of-stream); errc::truncated if the transport
  /// has positively confirmed no more data will ever arrive; errc::io_error
  /// for any other failure, after which the transport cannot be used again.
  /// @par Thread safety
  /// See the class-level contract: not called concurrently with any other
  /// method on the same object.
  /// @par Blocking
  /// Must never block; returns immediately with whatever data was available.
  virtual Result<std::size_t> read_some(mqtls::span<std::byte> buffer) = 0;

  /// @brief A file descriptor that becomes readable (via `poll(2)`'s
  /// `POLLIN`) when read_some() would make progress.
  /// @return A valid, pollable file descriptor, owned by this object.
  /// @par Thread safety
  /// Safe to call from any thread.
  [[nodiscard]] virtual int read_fd() const noexcept = 0;

  /// @brief A file descriptor that becomes writable (via `poll(2)`'s
  /// `POLLOUT`) when write_some() would make progress. May be the same
  /// descriptor as read_fd().
  /// @return A valid, pollable file descriptor, owned by this object.
  /// @par Thread safety
  /// Safe to call from any thread.
  [[nodiscard]] virtual int write_fd() const noexcept = 0;

  /// @brief Bytes already read from the underlying medium but not yet
  /// returned by read_some() (e.g. the remainder of an oversized chunk an
  /// implementation had to read in one go). Used for `BIO_CTRL_PENDING`.
  /// @return The number of such bytes; 0 if none.
  /// @par Thread safety
  /// See the class-level contract: not called concurrently with any other
  /// method on the same object.
  [[nodiscard]] virtual std::size_t pending_read() const noexcept = 0;
};

}  // namespace mqtls::transport

#endif  // MQTLS_TRANSPORT_HPP
