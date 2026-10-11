#include "mq/message_queue.hpp"

#include <cerrno>
#include <system_error>

#include <fcntl.h>

namespace mqtls::mq {

namespace {

constexpr mqd_t kInvalidFd = -1;

int direction_flag(Direction direction) {
  return direction == Direction::read_only ? O_RDONLY : O_WRONLY;
}

}  // namespace

// Matches the public, documented parameter order in message_queue.hpp;
// reordering a named factory function's parameters to "fix" this would just
// move the risk. A line-range suppression is used here, rather than one
// tied to a single line, because clang-tidy anchors this diagnostic to
// whichever parameter starts the swappable group rather than to the
// function's own line, and clang-format may wrap this signature across
// several lines.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
Result<MessageQueue> MessageQueue::create(const std::string& name,
                                          long max_messages,
                                          long message_size,
                                          mode_t mode,
                                          Direction direction,
                                          bool exclusive) {
  // NOLINTEND(bugprone-easily-swappable-parameters)
  mq_attr attr{};
  attr.mq_maxmsg = max_messages;
  attr.mq_msgsize = message_size;

  int flags = direction_flag(direction) | O_CREAT | O_NONBLOCK | O_CLOEXEC;
  if (exclusive) {
    flags |= O_EXCL;
  }

  const mqd_t fd = ::mq_open(name.c_str(), flags, mode, &attr);
  if (fd == kInvalidFd) {
    return tl::make_unexpected(std::error_code(errno, std::generic_category()));
  }

  if (exclusive) {
    // We just created this queue ourselves, with these exact attributes.
    return MessageQueue(fd, message_size);
  }

  // The queue may have already existed (mq_open(3) ignores `attr` in that
  // case); confirm it actually matches what the caller asked for rather
  // than silently using whatever size it happens to have.
  mq_attr actual{};
  if (::mq_getattr(fd, &actual) != 0) {
    const auto ec = std::error_code(errno, std::generic_category());
    ::mq_close(fd);
    return tl::make_unexpected(ec);
  }
  if (actual.mq_msgsize != message_size) {
    ::mq_close(fd);
    return tl::make_unexpected(make_error_code(errc::queue_attr_mismatch));
  }
  return MessageQueue(fd, message_size);
}

Result<MessageQueue> MessageQueue::open(const std::string& name, Direction direction) {
  const int flags = direction_flag(direction) | O_NONBLOCK | O_CLOEXEC;

  const mqd_t fd = ::mq_open(name.c_str(), flags);
  if (fd == kInvalidFd) {
    return tl::make_unexpected(std::error_code(errno, std::generic_category()));
  }

  mq_attr actual{};
  if (::mq_getattr(fd, &actual) != 0) {
    const auto ec = std::error_code(errno, std::generic_category());
    ::mq_close(fd);
    return tl::make_unexpected(ec);
  }
  return MessageQueue(fd, actual.mq_msgsize);
}

Result<void> MessageQueue::unlink(const std::string& name) {
  if (::mq_unlink(name.c_str()) != 0) {
    return tl::make_unexpected(std::error_code(errno, std::generic_category()));
  }
  return {};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- same rationale as create() above.
MessageQueue::MessageQueue(mqd_t fd, long message_size) noexcept
    : fd_(fd), message_size_(message_size) {}

MessageQueue::MessageQueue(MessageQueue&& other) noexcept
    : fd_(other.fd_), message_size_(other.message_size_) {
  other.fd_ = kInvalidFd;
}

MessageQueue& MessageQueue::operator=(MessageQueue&& other) noexcept {
  if (this != &other) {
    if (fd_ != kInvalidFd) {
      ::mq_close(fd_);
    }
    fd_ = other.fd_;
    message_size_ = other.message_size_;
    other.fd_ = kInvalidFd;
  }
  return *this;
}

MessageQueue::~MessageQueue() {
  if (fd_ != kInvalidFd) {
    ::mq_close(fd_);
  }
}

// Mutates the kernel-side queue (consumes a slot), so it is not a query
// despite not reassigning any of this object's own members.
// NOLINTNEXTLINE(readability-make-member-function-const)
Result<std::size_t> MessageQueue::try_send(mqtls::span<const std::byte> data) {
  if (static_cast<long>(data.size()) > message_size_) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  int rc = 0;
  do {
    rc = ::mq_send(fd_, reinterpret_cast<const char*>(data.data()), data.size(), 0);
  } while (rc != 0 && errno == EINTR);
  if (rc != 0) {
    return tl::make_unexpected(std::error_code(errno, std::generic_category()));
  }
  return data.size();
}

// NOLINTNEXTLINE(readability-make-member-function-const) -- same rationale as try_send() above.
Result<std::size_t> MessageQueue::try_receive(mqtls::span<std::byte> buffer) {
  if (static_cast<long>(buffer.size()) < message_size_) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  ssize_t n = 0;
  do {
    n = ::mq_receive(fd_, reinterpret_cast<char*>(buffer.data()), buffer.size(), nullptr);
  } while (n < 0 && errno == EINTR);
  if (n < 0) {
    return tl::make_unexpected(std::error_code(errno, std::generic_category()));
  }
  return static_cast<std::size_t>(n);
}

int MessageQueue::native_fd() const noexcept {
  return static_cast<int>(fd_);
}

long MessageQueue::message_size() const noexcept {
  return message_size_;
}

}  // namespace mqtls::mq
