#ifndef MQTLS_SECURE_CHANNEL_HPP
#define MQTLS_SECURE_CHANNEL_HPP

#include "mqtls/config.hpp"
#include "mqtls/error.hpp"
#include "mqtls/span.hpp"
#include "mqtls/transport.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace mqtls {

/// @brief A thread-safe, framed, mutually-authenticated TLS 1.3 byte-stream
/// channel over any mqtls::transport::ByteTransport.
///
/// Move-only; moving a SecureChannel while another thread is concurrently
/// using it (send()/recv()/close()) is undefined behavior, same as for any
/// other move-only handle under concurrent use.
class SecureChannel {
public:
  /// @brief Runs the TLS 1.3 handshake over an already-connected transport
  /// and, on success, returns a channel ready for send()/recv().
  ///
  /// This knows nothing about how `transport` got connected or how
  /// `expected_peer_name` was decided — that is entirely up to whichever
  /// transport module set the connection up; `mqtls::mq` is one such
  /// module, see `mqtls::mq::establish()`.
  ///
  /// @param transport          An already-connected transport; this channel
  /// takes ownership of it.
  /// @param role               Which side of the handshake to play.
  /// @param expected_peer_name The identity the peer's certificate must
  /// carry (checked via SAN DNS, falling back to CN) — both roles verify
  /// the peer this way; must not be empty.
  /// @param tls_config         Certificate/key files and algorithm profile.
  /// @param options            Message-size limit and handshake deadline.
  /// @return The channel; errc::invalid_argument for a bad argument
  /// (including a null `transport`, an empty `expected_peer_name`, or a
  /// `tls_config`/`options` that fails its own validate());
  /// errc::tls_config_failed if the TLS context itself could not be built;
  /// errc::certificate_verify_failed if the peer's certificate failed
  /// verification; errc::handshake_failed for any other handshake failure;
  /// errc::timeout if `options.handshake_timeout` passed first.
  /// @par Thread safety
  /// Safe to call from any thread.
  /// @par Blocking
  /// Blocks (subject to `options.handshake_timeout`) until the handshake
  /// completes or fails.
  static Result<SecureChannel> create(std::unique_ptr<transport::ByteTransport> transport,
                                      Role role,
                                      const std::string& expected_peer_name,
                                      const TlsConfig& tls_config,
                                      const ChannelOptions& options = {});

  SecureChannel(const SecureChannel&) = delete;
  SecureChannel& operator=(const SecureChannel&) = delete;

  /// @brief Transfers ownership of the underlying channel state.
  /// @param other The SecureChannel to move from; left with no channel to operate on.
  SecureChannel(SecureChannel&& other) noexcept;

  /// @brief Closes this object's own channel (if any) and takes ownership of the other one's.
  /// @param other The SecureChannel to move from; left with no channel to operate on.
  /// @return `*this`.
  SecureChannel& operator=(SecureChannel&& other) noexcept;

  /// @brief Closes the channel if still open (best-effort close_notify, see close()).
  ~SecureChannel();

  /// @brief Sends `msg` as one whole application message.
  /// @param msg     The message; must not exceed ChannelOptions::max_message_size.
  /// @param timeout How long to wait for the send to complete.
  /// @return `{}` on success; errc::message_too_large if `msg` is too big
  /// (the channel stays open); errc::channel_closed if close() was called
  /// (concurrently or previously); errc::timeout if `timeout` passed first;
  /// errc::peer_closed / errc::truncated / errc::protocol_error /
  /// errc::io_error for a failed underlying TLS/transport operation (the
  /// channel should be considered unusable afterwards; call close()).
  /// @par Thread safety
  /// Safe to call concurrently with recv(), with other send() calls
  /// (serialized — a message is never split across callers), and with close().
  /// @par Blocking
  /// Blocks until sent, failed, or `timeout` passes.
  Result<void> send(mqtls::span<const std::byte> msg, Timeout timeout = kInfinite);

  /// @brief Receives one whole application message.
  /// @param timeout How long to wait for a complete message to arrive.
  /// @return The message; errc::message_too_large if the peer's declared
  /// message length exceeds ChannelOptions::max_message_size (the channel
  /// is closed, since the byte stream can no longer be trusted);
  /// errc::channel_closed if close() was called; errc::timeout if `timeout`
  /// passed first; errc::peer_closed if the peer sent a clean TLS
  /// close_notify; errc::truncated / errc::protocol_error / errc::io_error
  /// for other transport/TLS failures.
  /// @par Thread safety
  /// Safe to call concurrently with send() and with close(); concurrent
  /// recv() calls are serialized (a message is never split across callers).
  /// @par Blocking
  /// Blocks until a full message arrives, failed, or `timeout` passes.
  Result<std::vector<std::byte>> recv(Timeout timeout = kInfinite);

  /// @brief Closes the channel: wakes any thread blocked in send()/recv()
  /// (with errc::channel_closed), then makes a best-effort attempt to
  /// exchange a TLS close_notify. Idempotent and safe to call more than
  /// once, including concurrently with itself.
  /// @param timeout Upper bound on the close_notify exchange; the channel
  /// is considered closed regardless of whether it completes in time.
  /// @return Always `{}`: closing itself cannot meaningfully fail from the
  /// caller's perspective (a failed close_notify exchange is not reported
  /// as an error).
  /// @par Thread safety
  /// Safe to call from any thread, including concurrently with itself,
  /// send() and recv().
  /// @par Blocking
  /// Blocks until send()/recv() calls already in flight have unwound and,
  /// bounded by `timeout`, for the close_notify exchange.
  Result<void> close(Timeout timeout = std::chrono::seconds(1));

  /// @brief The verified peer certificate's subject name.
  /// @return The subject, in OpenSSL's one-line form; empty if unavailable.
  /// @par Thread safety
  /// Safe to call from any thread.
  [[nodiscard]] std::string peer_subject() const;

  /// @brief The negotiated TLS ciphersuite name.
  /// @return e.g. `"TLS_AES_256_GCM_SHA384"`; empty if unavailable.
  /// @par Thread safety
  /// Safe to call from any thread.
  [[nodiscard]] std::string cipher() const;

  /// @brief Whether close() has not (yet) been called on this channel.
  /// @return `true` if still open.
  /// @par Thread safety
  /// Safe to call from any thread.
  [[nodiscard]] bool is_open() const noexcept;

private:
  class Impl;
  explicit SecureChannel(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

}  // namespace mqtls

#endif  // MQTLS_SECURE_CHANNEL_HPP
