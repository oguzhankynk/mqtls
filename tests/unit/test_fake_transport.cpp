#include <gtest/gtest.h>

#include "fake_transport.hpp"

namespace {

using mqtls::test::FakeTransport;

}  // namespace

TEST(FakeTransportTest, WritesOnOneEndAreReadableOnTheOther) {
  auto [a, b] = FakeTransport::make_pair();

  const std::byte payload[] = {std::byte{1}, std::byte{2}, std::byte{3}};
  const auto written = a->write_some(payload);
  ASSERT_TRUE(written.has_value());
  EXPECT_EQ(*written, 3U);

  std::byte buffer[16];
  const auto read = b->read_some(buffer);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(*read, 3U);
  EXPECT_EQ(buffer[0], payload[0]);
  EXPECT_EQ(buffer[2], payload[2]);
}

TEST(FakeTransportTest, ReadWithNothingAvailableWouldBlock) {
  auto [a, b] = FakeTransport::make_pair();
  (void)a;

  std::byte buffer[16];
  const auto read = b->read_some(buffer);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(*read, 0U);
}

TEST(FakeTransportTest, ReadFdAndWriteFdAreTheSamePollableDescriptor) {
  auto [a, b] = FakeTransport::make_pair();
  (void)b;
  EXPECT_EQ(a->read_fd(), a->write_fd());
  EXPECT_GE(a->read_fd(), 0);
}

TEST(FakeTransportTest, LimitWriteSizeCapsEachCall) {
  auto [a, b] = FakeTransport::make_pair();
  a->limit_write_size(2);

  const std::byte payload[5] = {};
  const auto first = a->write_some(payload);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(*first, 2U);

  std::byte buffer[16];
  const auto read = b->read_some(buffer);
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(*read, 2U);
}

TEST(FakeTransportTest, ForceWriteWouldBlockOnSpecificCall) {
  auto [a, b] = FakeTransport::make_pair();
  (void)b;
  a->force_write_would_block_on_call(2);

  const std::byte payload[1] = {};
  EXPECT_EQ(*a->write_some(payload), 1U);  // call 1: normal
  EXPECT_EQ(*a->write_some(payload), 0U);  // call 2: forced would-block
  EXPECT_EQ(*a->write_some(payload), 1U);  // call 3: back to normal
}

TEST(FakeTransportTest, ForceReadWouldBlockOnSpecificCall) {
  auto [a, b] = FakeTransport::make_pair();
  const std::byte payload[3] = {};
  ASSERT_TRUE(a->write_some(payload).has_value());

  b->force_read_would_block_on_call(1);
  std::byte buffer[16];
  const auto forced = b->read_some(buffer);
  ASSERT_TRUE(forced.has_value());
  EXPECT_EQ(*forced, 0U);

  const auto real = b->read_some(buffer);
  ASSERT_TRUE(real.has_value());
  EXPECT_EQ(*real, 3U);
}

TEST(FakeTransportTest, FailWriteFromCallStaysFailed) {
  auto [a, b] = FakeTransport::make_pair();
  (void)b;
  a->fail_write_from_call(2);

  const std::byte payload[1] = {};
  EXPECT_TRUE(a->write_some(payload).has_value());  // call 1: normal

  const auto second = a->write_some(payload);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), mqtls::errc::io_error);

  const auto third = a->write_some(payload);
  ASSERT_FALSE(third.has_value());
  EXPECT_EQ(third.error(), mqtls::errc::io_error);
}

TEST(FakeTransportTest, FailReadFromCallStaysFailed) {
  auto [a, b] = FakeTransport::make_pair();
  (void)a;
  b->fail_read_from_call(1);

  std::byte buffer[16];
  const auto result = b->read_some(buffer);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::io_error);
}

TEST(FakeTransportTest, PeerClosingTheSocketIsReportedAsTruncated) {
  auto [a, b] = FakeTransport::make_pair();
  a.reset();  // closes a's end of the socket pair

  std::byte buffer[16];
  const auto result = b->read_some(buffer);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::truncated);
}

TEST(FakeTransportTest, PendingReadIsAlwaysZero) {
  auto [a, b] = FakeTransport::make_pair();
  (void)a;
  EXPECT_EQ(b->pending_read(), 0U);
}
