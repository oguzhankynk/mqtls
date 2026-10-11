#ifndef MQTLS_UTIL_DEADLINE_HPP
#define MQTLS_UTIL_DEADLINE_HPP

#include "mqtls/error.hpp"

#include <chrono>
#include <climits>

/// @brief Internal helpers shared across mqtls's core and transport modules;
/// not part of the public API (only `mqtls` itself and
/// `mqtls::transport` have a public surface, through `include/mqtls`).
namespace mqtls::util {

/// @brief A monotonic point in time derived from a Timeout, for poll()-style waits.
///
/// Deadlines are computed from `std::chrono::steady_clock`, never from a
/// wall-clock, so a concurrent clock adjustment cannot shorten or extend a
/// wait in progress.
class Deadline {
public:
  /// @brief Builds a deadline `timeout` from now. `kInfinite` never expires.
  /// @param timeout How far in the future the deadline falls.
  /// @par Thread safety
  /// Safe to call from any thread.
  explicit Deadline(Timeout timeout) noexcept
      : infinite_(timeout == kInfinite),
        deadline_(infinite_ ? std::chrono::steady_clock::time_point{}
                            : std::chrono::steady_clock::now() + timeout) {}

  /// @brief Milliseconds left, suitable as poll(2)'s `timeout` argument.
  /// @return -1 if this deadline is infinite; otherwise the remaining time
  /// clamped to `[0, INT_MAX]` (0 once the deadline has passed).
  /// @par Thread safety
  /// Safe to call from any thread; reads only this object's own state.
  [[nodiscard]] int remaining_ms() const noexcept {
    if (infinite_) {
      return -1;
    }
    const auto remaining = deadline_ - std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
    if (ms <= 0) {
      return 0;
    }
    if (ms > INT_MAX) {
      return INT_MAX;
    }
    return static_cast<int>(ms);
  }

  /// @brief True once `remaining_ms()` would return 0. Always false for an
  /// infinite deadline.
  /// @return Whether the deadline has passed.
  /// @par Thread safety
  /// Safe to call from any thread.
  [[nodiscard]] bool expired() const noexcept {
    return !infinite_ && remaining_ms() == 0;
  }

private:
  bool infinite_;
  std::chrono::steady_clock::time_point deadline_;
};

}  // namespace mqtls::util

#endif  // MQTLS_UTIL_DEADLINE_HPP
