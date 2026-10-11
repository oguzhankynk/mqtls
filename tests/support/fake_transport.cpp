#include "fake_transport.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <sys/socket.h>

#include <fcntl.h>
#include <unistd.h>

namespace mqtls::test {

std::pair<std::unique_ptr<FakeTransport>, std::unique_ptr<FakeTransport>>
FakeTransport::make_pair() {
  int fds[2] = {-1, -1};
  const int rc = ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds);
  assert(rc == 0 && "socketpair() failed; nothing a test can do about this");
  (void)rc;
  return {std::unique_ptr<FakeTransport>(new FakeTransport(fds[0])),
          std::unique_ptr<FakeTransport>(new FakeTransport(fds[1]))};
}

FakeTransport::FakeTransport(int fd) noexcept : fd_(fd) {}

FakeTransport::~FakeTransport() {
  ::close(fd_);
}

Result<std::size_t> FakeTransport::write_some(mqtls::span<const std::byte> data) {
  ++write_call_count_;
  if (fail_write_from_ > 0 && write_call_count_ >= fail_write_from_) {
    return tl::make_unexpected(make_error_code(errc::io_error));
  }
  if (force_write_wouldblock_at_ > 0 && write_call_count_ == force_write_wouldblock_at_) {
    return std::size_t{0};
  }

  std::size_t to_write = data.size();
  if (write_limit_ > 0) {
    to_write = std::min(to_write, write_limit_);
  }

  // MSG_NOSIGNAL: writing to a socket whose peer already closed its end
  // must surface as EPIPE (mapped to errc::io_error below), not as a
  // SIGPIPE that kills the whole process (the default disposition, and
  // the actual failure mode observed before this fix was added).
  const ssize_t n = ::send(fd_, data.data(), to_write, MSG_NOSIGNAL);
  if (n < 0) {
    // EAGAIN == EWOULDBLOCK on Linux; listing both triggers -Wlogical-op.
    if (errno == EAGAIN) {
      return std::size_t{0};
    }
    return tl::make_unexpected(make_error_code(errc::io_error));
  }
  return static_cast<std::size_t>(n);
}

Result<std::size_t> FakeTransport::read_some(mqtls::span<std::byte> buffer) {
  ++read_call_count_;
  if (fail_read_from_ > 0 && read_call_count_ >= fail_read_from_) {
    return tl::make_unexpected(make_error_code(errc::io_error));
  }
  if (force_read_wouldblock_at_ > 0 && read_call_count_ == force_read_wouldblock_at_) {
    return std::size_t{0};
  }

  if (buffer.empty()) {
    return std::size_t{0};
  }

  const ssize_t n = ::read(fd_, buffer.data(), buffer.size());
  if (n < 0) {
    // EAGAIN == EWOULDBLOCK on Linux; listing both triggers -Wlogical-op.
    if (errno == EAGAIN) {
      return std::size_t{0};
    }
    return tl::make_unexpected(make_error_code(errc::io_error));
  }
  if (n == 0) {
    // On a stream socket, a 0-byte read after the fd was reported readable
    // is a positive end-of-stream signal (the peer closed it), not "no data
    // yet" (ByteTransport's "no data" vs.
    // "no more data" note). Reporting this as a plain 0 would make the
    // core's read loop spin at 100% CPU retrying forever.
    return tl::make_unexpected(make_error_code(errc::truncated));
  }
  return static_cast<std::size_t>(n);
}

int FakeTransport::read_fd() const noexcept {
  return fd_;
}

int FakeTransport::write_fd() const noexcept {
  return fd_;
}

std::size_t FakeTransport::pending_read() const noexcept {
  // This fake never buffers beyond the kernel socket buffer itself, so it
  // has nothing additional to report; a real byte-chunking transport (e.g.
  // mqtls::mq::QueuePair) overrides this meaningfully.
  return std::size_t{0};
}

void FakeTransport::limit_write_size(std::size_t max_bytes) noexcept {
  write_limit_ = max_bytes;
}

void FakeTransport::force_write_would_block_on_call(int n) noexcept {
  force_write_wouldblock_at_ = n;
}

void FakeTransport::force_read_would_block_on_call(int n) noexcept {
  force_read_wouldblock_at_ = n;
}

void FakeTransport::fail_write_from_call(int n) noexcept {
  fail_write_from_ = n;
}

void FakeTransport::fail_read_from_call(int n) noexcept {
  fail_read_from_ = n;
}

}  // namespace mqtls::test
