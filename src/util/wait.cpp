#include "util/wait.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <sys/eventfd.h>
#include <utility>

#include <poll.h>
#include <unistd.h>

namespace mqtls::util {

Result<WakeFd> WakeFd::create() {
  const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (fd < 0) {
    return tl::make_unexpected(make_error_code(errc::io_error));
  }
  return WakeFd{fd};
}

WakeFd::WakeFd(WakeFd&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

WakeFd& WakeFd::operator=(WakeFd&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

WakeFd::~WakeFd() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

// EAGAIN means the counter is already saturated, i.e. already signalled:
// that is the idempotent case this function documents, not an error. Any
// other failure has no good recovery here (this function is noexcept and
// exists purely to nudge waiters), so it is silently ignored. Mutates the
// kernel-side eventfd counter, so despite touching no member of *this, it
// is not a query.
// NOLINTNEXTLINE(readability-make-member-function-const)
void WakeFd::signal() noexcept {
  const std::uint64_t one = 1;
  while (::write(fd_, &one, sizeof(one)) < 0 && errno == EINTR) {
  }
}

// Same rationale as signal() above: mutates the kernel-side eventfd, not a query.
// NOLINTNEXTLINE(readability-make-member-function-const)
void WakeFd::drain() noexcept {
  std::uint64_t value = 0;
  while (::read(fd_, &value, sizeof(value)) < 0 && errno == EINTR) {
  }
}

Result<void> wait_for(int fd, short events, const WakeFd& wake, const Deadline& deadline) {
  for (;;) {
    std::array<pollfd, 2> pfds = {{
        {fd, events, 0},
        {wake.fd(), POLLIN, 0},
    }};
    const int timeout_ms = deadline.remaining_ms();
    const int rc = ::poll(pfds.data(), pfds.size(), timeout_ms);
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      return tl::make_unexpected(make_error_code(errc::io_error));
    }
    if (rc == 0) {
      return tl::make_unexpected(make_error_code(errc::timeout));
    }
    if (pfds[1].revents != 0) {
      return tl::make_unexpected(make_error_code(errc::channel_closed));
    }
    if (pfds[0].revents != 0) {
      return {};
    }
    // Neither descriptor actually reported an event (can happen with
    // POLLERR/POLLHUP-only wakeups on unrelated fds in rare cases); loop and
    // let the deadline keep making progress rather than returning early.
  }
}

}  // namespace mqtls::util
