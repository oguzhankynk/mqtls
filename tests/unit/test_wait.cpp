#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>

#include <poll.h>
#include <unistd.h>

#include "util/deadline.hpp"
#include "util/wait.hpp"

namespace {

using mqtls::Timeout;
using mqtls::util::Deadline;
using mqtls::util::wait_for;
using mqtls::util::WakeFd;

/// @brief A pipe(2) pair, for exercising wait_for() against a real pollable
/// fd without depending on the POSIX message queue wrapper.
class Pipe {
public:
  Pipe() {
    [[maybe_unused]] const int rc = ::pipe(fds_);
  }
  ~Pipe() {
    ::close(fds_[0]);
    ::close(fds_[1]);
  }
  Pipe(const Pipe&) = delete;
  Pipe& operator=(const Pipe&) = delete;

  [[nodiscard]] int read_fd() const noexcept {
    return fds_[0];
  }
  [[nodiscard]] int write_fd() const noexcept {
    return fds_[1];
  }

private:
  int fds_[2];
};

}  // namespace

TEST(Deadline, InfiniteNeverExpires) {
  const Deadline deadline(mqtls::kInfinite);
  EXPECT_EQ(deadline.remaining_ms(), -1);
  EXPECT_FALSE(deadline.expired());
}

TEST(Deadline, FiniteCountsDownAndExpires) {
  const Deadline deadline(Timeout(20));
  EXPECT_GT(deadline.remaining_ms(), -1);
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  EXPECT_EQ(deadline.remaining_ms(), 0);
  EXPECT_TRUE(deadline.expired());
}

TEST(WakeFdTest, CreateReturnsAPollableFd) {
  auto wake = WakeFd::create();
  ASSERT_TRUE(wake.has_value());
  EXPECT_GE(wake->fd(), 0);
}

TEST(WakeFdTest, SignalThenDrainIsIdempotent) {
  auto wake = WakeFd::create();
  ASSERT_TRUE(wake.has_value());
  wake->signal();
  wake->signal();  // must not block or fail
  wake->drain();
  wake->drain();  // draining an already-drained WakeFd must not block
}

TEST(WaitForTest, ReturnsImmediatelyWhenFdIsReady) {
  const Pipe pipe;
  auto wake = WakeFd::create();
  ASSERT_TRUE(wake.has_value());
  const char byte = 'x';
  ASSERT_EQ(::write(pipe.write_fd(), &byte, 1), 1);

  const auto result = wait_for(pipe.read_fd(), POLLIN, *wake, Deadline(Timeout(1000)));
  EXPECT_TRUE(result.has_value());
}

TEST(WaitForTest, TimesOutWhenNothingIsReady) {
  const Pipe pipe;
  auto wake = WakeFd::create();
  ASSERT_TRUE(wake.has_value());

  const auto start = std::chrono::steady_clock::now();
  const auto result = wait_for(pipe.read_fd(), POLLIN, *wake, Deadline(Timeout(50)));
  const auto elapsed = std::chrono::steady_clock::now() - start;

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::timeout);
  EXPECT_LT(elapsed, std::chrono::milliseconds(2000));
}

TEST(WaitForTest, ReturnsChannelClosedWhenAlreadySignalled) {
  const Pipe pipe;
  auto wake = WakeFd::create();
  ASSERT_TRUE(wake.has_value());
  wake->signal();

  const auto result = wait_for(pipe.read_fd(), POLLIN, *wake, Deadline(mqtls::kInfinite));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::channel_closed);
}

TEST(WaitForTest, SignalFromAnotherThreadWakesABlockedWaiter) {
  const Pipe pipe;
  auto wake = WakeFd::create();
  ASSERT_TRUE(wake.has_value());

  std::promise<mqtls::Result<void>> result_promise;
  std::future<mqtls::Result<void>> result_future = result_promise.get_future();

  std::thread waiter([&] {
    result_promise.set_value(wait_for(pipe.read_fd(), POLLIN, *wake, Deadline(mqtls::kInfinite)));
  });

  // Give the waiter a head start into poll() before waking it; this is a
  // best-effort nudge, not a correctness requirement (the test still passes
  // if signal() happens to run first, it would just also pass the
  // "ReturnsChannelClosedWhenAlreadySignalled" case above instead).
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  wake->signal();

  ASSERT_EQ(result_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  const auto result = result_future.get();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::channel_closed);
  waiter.join();
}
