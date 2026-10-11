#ifndef MQTLS_TEST_SUPPORT_FAKE_TRANSPORT_HPP
#define MQTLS_TEST_SUPPORT_FAKE_TRANSPORT_HPP

#include "mqtls/transport.hpp"

#include <cstddef>
#include <memory>
#include <utility>

namespace mqtls::test {

/// @brief A `socketpair(2)`-backed `mqtls::transport::ByteTransport`, for
/// testing the core (tls::Bio, SecureChannel) without POSIX message queues
/// or any other real transport (this is the core's
/// test double, not part of `mqtls::mq`).
///
/// Two linked instances, from make_pair(), behave like an ordinary
/// bidirectional byte stream between them; beyond that, each instance can
/// be told to misbehave in specific, deterministic ways.
class FakeTransport final : public transport::ByteTransport {
public:
  /// @brief Creates two FakeTransport instances connected to each other by
  /// a `socketpair(2)`.
  /// @return A connected pair; aborts the test process if the underlying
  /// `socketpair(2)` call fails (there is no sensible way to run a test
  /// that needs one without it).
  static std::pair<std::unique_ptr<FakeTransport>, std::unique_ptr<FakeTransport>> make_pair();

  FakeTransport(const FakeTransport&) = delete;
  FakeTransport& operator=(const FakeTransport&) = delete;
  FakeTransport(FakeTransport&&) = delete;
  FakeTransport& operator=(FakeTransport&&) = delete;

  /// @brief Closes the underlying socket.
  ~FakeTransport() override;

  Result<std::size_t> write_some(mqtls::span<const std::byte> data) override;
  Result<std::size_t> read_some(mqtls::span<std::byte> buffer) override;
  [[nodiscard]] int read_fd() const noexcept override;
  [[nodiscard]] int write_fd() const noexcept override;
  [[nodiscard]] std::size_t pending_read() const noexcept override;

  /// @brief Caps every future write_some() call at `max_bytes`, simulating
  /// a transport that only ever accepts small chunks at a time. 0 means
  /// uncapped (the default).
  void limit_write_size(std::size_t max_bytes) noexcept;

  /// @brief Makes the `n`-th call to write_some() from now (1 = the very
  /// next call) return 0 (its documented "would block" outcome) instead of
  /// touching the socket; every other call behaves normally. 0 disables
  /// this (the default).
  void force_write_would_block_on_call(int n) noexcept;

  /// @brief Same as force_write_would_block_on_call(), for read_some().
  void force_read_would_block_on_call(int n) noexcept;

  /// @brief Makes the `n`-th call to write_some() from now, and every call
  /// after it, return errc::io_error instead of touching the socket
  /// (matching the real contract: once a transport fails, it stays
  /// failed). 0 disables this (the default).
  void fail_write_from_call(int n) noexcept;

  /// @brief Same as fail_write_from_call(), for read_some().
  void fail_read_from_call(int n) noexcept;

private:
  explicit FakeTransport(int fd) noexcept;

  int fd_;
  std::size_t write_limit_ = 0;
  int write_call_count_ = 0;
  int read_call_count_ = 0;
  int force_write_wouldblock_at_ = 0;
  int force_read_wouldblock_at_ = 0;
  int fail_write_from_ = 0;
  int fail_read_from_ = 0;
};

}  // namespace mqtls::test

#endif  // MQTLS_TEST_SUPPORT_FAKE_TRANSPORT_HPP
