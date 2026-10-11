// This is the one direct test of mqtls::transport::ByteTransport itself: it
// is a pure interface, so there is no behavior to verify
// beyond "a minimal implementation compiles, links, and dispatches
// virtually as expected". Real coverage comes from whatever implements it:
// tests/support/fake_transport and mqtls::mq::QueuePair.
#include "mqtls/transport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace {

/// @brief The simplest possible ByteTransport: an in-memory byte queue, no
/// real file descriptors. Exists only to prove the interface is usable; not
/// meant to be a realistic transport implementation.
class MemoryTransport final : public mqtls::transport::ByteTransport {
public:
  mqtls::Result<std::size_t> write_some(mqtls::span<const std::byte> data) override {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
    return data.size();
  }

  mqtls::Result<std::size_t> read_some(mqtls::span<std::byte> out) override {
    const std::size_t n = std::min(out.size(), buffer_.size());
    std::copy_n(buffer_.begin(), n, out.begin());
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(n));
    return n;
  }

  [[nodiscard]] int read_fd() const noexcept override {
    return -1;
  }
  [[nodiscard]] int write_fd() const noexcept override {
    return -1;
  }
  [[nodiscard]] std::size_t pending_read() const noexcept override {
    return buffer_.size();
  }

private:
  std::vector<std::byte> buffer_;
};

}  // namespace

TEST(ByteTransportTest, WriteThenReadRoundTrips) {
  MemoryTransport transport;
  const std::byte data[] = {std::byte{1}, std::byte{2}, std::byte{3}};

  auto written = transport.write_some(data);
  ASSERT_TRUE(written.has_value());
  EXPECT_EQ(*written, 3U);
  EXPECT_EQ(transport.pending_read(), 3U);

  std::byte out[3] = {};
  auto read = transport.read_some(out);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(*read, 3U);
  EXPECT_EQ(transport.pending_read(), 0U);
  EXPECT_TRUE(std::equal(std::begin(data), std::end(data), std::begin(out)));
}

TEST(ByteTransportTest, UsableThroughABasePointer) {
  MemoryTransport impl;
  mqtls::transport::ByteTransport& base = impl;
  const std::byte data[] = {std::byte{42}};

  EXPECT_TRUE(base.write_some(data).has_value());
  EXPECT_EQ(base.pending_read(), 1U);
}
