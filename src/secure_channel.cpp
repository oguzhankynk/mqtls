#include "mqtls/secure_channel.hpp"

#include <openssl/err.h>
#include <openssl/x509v3.h>

#include <array>
#include <atomic>
#include <mutex>
#include <utility>

#include <poll.h>

#include "framing.hpp"
#include "tls/bio.hpp"
#include "tls/context.hpp"
#include "tls/openssl_raii.hpp"
#include "util/deadline.hpp"
#include "util/wait.hpp"

namespace mqtls {

namespace {

/// @brief Maps an SSL_get_error() result that was not SSL_ERROR_NONE into
/// the mqtls::errc value for a hard (non-retryable) SSL-level failure.
/// Callers handle SSL_ERROR_WANT_READ/WRITE themselves.
std::error_code map_hard_ssl_error(int ssl_error) {
  switch (ssl_error) {
    case SSL_ERROR_ZERO_RETURN:
      return make_error_code(errc::peer_closed);
    case SSL_ERROR_SYSCALL:
      return make_error_code(errc::truncated);
    case SSL_ERROR_SSL:
      return make_error_code(errc::protocol_error);
    default:
      return make_error_code(errc::io_error);
  }
}

}  // namespace

/// @brief SecureChannel's actual state and logic; the
/// public class is a thin pimpl wrapper around this.
class SecureChannel::Impl {
public:
  Impl(std::unique_ptr<transport::ByteTransport> transport,
       tls::Context context,
       tls::SslPtr ssl,
       ChannelOptions options,
       util::WakeFd wake)
      : transport_(std::move(transport)),
        context_(std::move(context)),
        ssl_(std::move(ssl)),
        options_(options),
        wake_(std::move(wake)) {}

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
  ~Impl() = default;

  Result<void> send(mqtls::span<const std::byte> msg, Timeout timeout) {
    if (msg.size() > options_.max_message_size) {
      return tl::make_unexpected(make_error_code(errc::message_too_large));
    }

    const std::lock_guard<std::mutex> send_lock(send_mtx_);
    if (closing_.load(std::memory_order_acquire)) {
      return tl::make_unexpected(make_error_code(errc::channel_closed));
    }

    util::Deadline deadline(timeout);
    const auto header = encode_frame_length(static_cast<std::uint32_t>(msg.size()));
    if (auto result = write_all(header, deadline); !result) {
      return result;
    }
    return write_all(msg, deadline);
  }

  Result<std::vector<std::byte>> recv(Timeout timeout) {
    const std::lock_guard<std::mutex> recv_lock(recv_mtx_);
    if (closing_.load(std::memory_order_acquire)) {
      return tl::make_unexpected(make_error_code(errc::channel_closed));
    }

    util::Deadline deadline(timeout);
    std::array<std::byte, kFrameHeaderSize> header{};
    if (auto result = read_exact(header, deadline); !result) {
      return tl::make_unexpected(result.error());
    }

    const std::uint32_t length = decode_frame_length(header);
    if (length > options_.max_message_size) {
      // The byte stream can no longer be trusted (either a misbehaving
      // peer or a desync); do not try to recover, close the channel.
      force_close();
      return tl::make_unexpected(make_error_code(errc::message_too_large));
    }

    std::vector<std::byte> payload(length);
    if (auto result = read_exact(payload, deadline); !result) {
      return tl::make_unexpected(result.error());
    }
    return payload;
  }

  Result<void> close(Timeout timeout) {
    const bool already_closing = closing_.exchange(true, std::memory_order_acq_rel);
    wake_.signal();

    const std::lock_guard<std::mutex> send_lock(send_mtx_);
    const std::lock_guard<std::mutex> recv_lock(recv_mtx_);

    if (already_closing) {
      return {};  // another call already ran (or is running) the shutdown below.
    }

    util::Deadline deadline(timeout);
    // wake_ is already signalled (above), so it cannot be used to bound
    // this wait without resolving instantly; a fresh, never-signalled
    // WakeFd gives a real, deadline-bounded wait here instead.
    auto shutdown_wake = util::WakeFd::create();
    for (;;) {
      int rc = 0;
      int ssl_error = 0;
      {
        const std::lock_guard<std::mutex> ssl_lock(ssl_mtx_);
        rc = SSL_shutdown(ssl_.get());
        if (rc < 0) {
          ssl_error = SSL_get_error(ssl_.get(), rc);
        }
      }
      if (rc >= 0) {
        break;  // close_notify sent (and/or received); best-effort, good enough either way.
      }
      if ((ssl_error != SSL_ERROR_WANT_READ && ssl_error != SSL_ERROR_WANT_WRITE) ||
          deadline.expired() || !shutdown_wake) {
        break;
      }
      const int fd =
          (ssl_error == SSL_ERROR_WANT_READ) ? transport_->read_fd() : transport_->write_fd();
      const short events = (ssl_error == SSL_ERROR_WANT_READ) ? POLLIN : POLLOUT;
      if (!util::wait_for(fd, events, *shutdown_wake, deadline)) {
        break;  // timeout: stop trying, the channel is closing regardless.
      }
    }
    return {};
  }

  [[nodiscard]] std::string peer_subject() const {
    const std::lock_guard<std::mutex> ssl_lock(ssl_mtx_);
    tls::X509Ptr cert(SSL_get1_peer_certificate(ssl_.get()));
    if (!cert) {
      return {};
    }
    constexpr std::size_t kSubjectBufferSize = 256;
    std::array<char, kSubjectBufferSize> buf{};
    X509_NAME_oneline(X509_get_subject_name(cert.get()), buf.data(), static_cast<int>(buf.size()));
    return {buf.data()};
  }

  [[nodiscard]] std::string cipher() const {
    const std::lock_guard<std::mutex> ssl_lock(ssl_mtx_);
    const char* name = SSL_get_cipher_name(ssl_.get());
    return name != nullptr ? std::string(name) : std::string();
  }

  [[nodiscard]] bool is_open() const noexcept {
    return !closing_.load(std::memory_order_acquire);
  }

  /// @brief Runs the handshake (role already set on `ssl`, expected peer
  /// name already applied) before Impl's constructor is even reachable:
  /// this is called by SecureChannel::create() on a half-built Impl's
  /// members directly, since the handshake itself does not need
  /// send_mtx_/recv_mtx_ (nothing else can be using this channel yet).
  Result<void> run_handshake(Timeout handshake_timeout) {
    util::Deadline deadline(handshake_timeout);
    for (;;) {
      int rc = 0;
      int ssl_error = 0;
      {
        const std::lock_guard<std::mutex> ssl_lock(ssl_mtx_);
        rc = SSL_do_handshake(ssl_.get());
        if (rc != 1) {
          ssl_error = SSL_get_error(ssl_.get(), rc);
        }
      }
      if (rc == 1) {
        return {};
      }
      if (auto waited = wait_for_ssl_io(ssl_error, deadline); !waited) {
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
          return waited;  // errc::timeout or errc::channel_closed from wait_for().
        }
        if (SSL_get_verify_result(ssl_.get()) != X509_V_OK) {
          return tl::make_unexpected(make_error_code(errc::certificate_verify_failed));
        }
        return tl::make_unexpected(make_error_code(errc::handshake_failed));
      }
    }
  }

private:
  /// @brief Waits out an SSL_ERROR_WANT_READ/WRITE condition, or maps any
  /// other `ssl_error` straight to its mqtls::errc. Never touches
  /// ssl_mtx_: it must be called with that lock already released (never
  /// wait while holding it).
  Result<void> wait_for_ssl_io(int ssl_error, util::Deadline& deadline) {
    if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
      if (deadline.expired()) {
        return tl::make_unexpected(make_error_code(errc::timeout));
      }
      const int fd =
          (ssl_error == SSL_ERROR_WANT_READ) ? transport_->read_fd() : transport_->write_fd();
      const short events = (ssl_error == SSL_ERROR_WANT_READ) ? POLLIN : POLLOUT;
      return util::wait_for(fd, events, wake_, deadline);
    }
    return tl::make_unexpected(map_hard_ssl_error(ssl_error));
  }

  /// @brief Writes every byte of `data`, retrying through wait_for_ssl_io()
  /// as needed. `deadline` is read each retry, and is the same object
  /// across both of send()'s calls to this (header, then payload), so the
  /// whole message shares one overall budget rather than resetting it
  /// per-call.
  Result<void> write_all(mqtls::span<const std::byte> data, util::Deadline& deadline) {
    std::size_t offset = 0;
    while (offset < data.size()) {
      int written = 0;
      int ssl_error = 0;
      {
        const std::lock_guard<std::mutex> ssl_lock(ssl_mtx_);
        if (closing_.load(std::memory_order_acquire)) {
          return tl::make_unexpected(make_error_code(errc::channel_closed));
        }
        const int rc =
            SSL_write(ssl_.get(), data.data() + offset, static_cast<int>(data.size() - offset));
        if (rc > 0) {
          written = rc;
        } else {
          ssl_error = SSL_get_error(ssl_.get(), rc);
        }
      }
      if (written > 0) {
        offset += static_cast<std::size_t>(written);
        continue;
      }
      if (auto waited = wait_for_ssl_io(ssl_error, deadline); !waited) {
        return waited;
      }
    }
    return {};
  }

  /// @brief Reads until `buffer` is completely filled, retrying through
  /// wait_for_ssl_io() as needed, bounded by `deadline` throughout.
  Result<void> read_exact(mqtls::span<std::byte> buffer, util::Deadline& deadline) {
    std::size_t offset = 0;
    while (offset < buffer.size()) {
      int got = 0;
      int ssl_error = 0;
      {
        const std::lock_guard<std::mutex> ssl_lock(ssl_mtx_);
        if (closing_.load(std::memory_order_acquire)) {
          return tl::make_unexpected(make_error_code(errc::channel_closed));
        }
        const int rc =
            SSL_read(ssl_.get(), buffer.data() + offset, static_cast<int>(buffer.size() - offset));
        if (rc > 0) {
          got = rc;
        } else {
          ssl_error = SSL_get_error(ssl_.get(), rc);
        }
      }
      if (got > 0) {
        offset += static_cast<std::size_t>(got);
        continue;
      }
      if (auto waited = wait_for_ssl_io(ssl_error, deadline); !waited) {
        return waited;
      }
    }
    return {};
  }

  /// @brief Used internally when recv() detects an unrecoverable protocol
  /// violation (an oversized declared length): signals closing_/wake_ so
  /// any other in-flight call unwinds, without the public close()'s
  /// best-effort close_notify exchange or its lock acquisition (recv() is
  /// already holding recv_mtx_ at the point this is called).
  void force_close() noexcept {
    closing_.store(true, std::memory_order_release);
    wake_.signal();
  }

  std::unique_ptr<transport::ByteTransport> transport_;
  tls::Context context_;
  tls::SslPtr ssl_;
  ChannelOptions options_;
  util::WakeFd wake_;
  mutable std::mutex ssl_mtx_;
  std::mutex send_mtx_;
  std::mutex recv_mtx_;
  std::atomic<bool> closing_{false};
};

Result<SecureChannel> SecureChannel::create(std::unique_ptr<transport::ByteTransport> transport,
                                            Role role,
                                            const std::string& expected_peer_name,
                                            const TlsConfig& tls_config,
                                            const ChannelOptions& options) {
  if (!transport || expected_peer_name.empty()) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  if (auto validated = validate(options); !validated) {
    return tl::make_unexpected(validated.error());
  }

  auto context = tls::Context::make(tls_config, role);
  if (!context) {
    return tl::make_unexpected(context.error());
  }

  tls::SslPtr ssl(SSL_new(context->native()));
  if (!ssl) {
    ::ERR_clear_error();
    return tl::make_unexpected(make_error_code(errc::handshake_failed));
  }

  BIO* bio = tls::make_bio(*transport);
  if (bio == nullptr) {
    return tl::make_unexpected(make_error_code(errc::handshake_failed));
  }
  SSL_set_bio(ssl.get(), bio, bio);  // ssl now owns this BIO (one reference, rbio == wbio).

  if (role == Role::server) {
    SSL_set_accept_state(ssl.get());
  } else {
    SSL_set_connect_state(ssl.get());
  }

  if (X509_VERIFY_PARAM_set1_host(SSL_get0_param(ssl.get()), expected_peer_name.c_str(), 0) != 1) {
    ::ERR_clear_error();
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }

  auto wake = util::WakeFd::create();
  if (!wake) {
    return tl::make_unexpected(wake.error());
  }

  auto impl = std::make_unique<Impl>(
      std::move(transport), std::move(*context), std::move(ssl), options, std::move(*wake));
  if (auto handshake = impl->run_handshake(options.handshake_timeout); !handshake) {
    return tl::make_unexpected(handshake.error());
  }

  return SecureChannel(std::move(impl));
}

SecureChannel::SecureChannel(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

SecureChannel::SecureChannel(SecureChannel&& other) noexcept = default;

SecureChannel& SecureChannel::operator=(SecureChannel&& other) noexcept {
  if (this != &other) {
    if (impl_) {
      impl_->close(std::chrono::seconds(1));
    }
    impl_ = std::move(other.impl_);
  }
  return *this;
}

SecureChannel::~SecureChannel() {
  if (impl_) {
    impl_->close(std::chrono::seconds(1));
  }
}

Result<void> SecureChannel::send(mqtls::span<const std::byte> msg, Timeout timeout) {
  return impl_->send(msg, timeout);
}

Result<std::vector<std::byte>> SecureChannel::recv(Timeout timeout) {
  return impl_->recv(timeout);
}

Result<void> SecureChannel::close(Timeout timeout) {
  return impl_->close(timeout);
}

std::string SecureChannel::peer_subject() const {
  return impl_->peer_subject();
}

std::string SecureChannel::cipher() const {
  return impl_->cipher();
}

bool SecureChannel::is_open() const noexcept {
  return impl_->is_open();
}

}  // namespace mqtls
