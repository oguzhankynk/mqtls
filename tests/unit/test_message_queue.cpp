#include <gtest/gtest.h>

#include <cerrno>
#include <cstddef>
#include <system_error>

#include <poll.h>

#include "mq/message_queue.hpp"
#include "unique_queue_name.hpp"

namespace {

using mqtls::mq::Direction;
using mqtls::mq::MessageQueue;

class MessageQueueTest : public ::testing::Test {
protected:
  void SetUp() override {
    name_ = mqtls::test::unique_queue_prefix() + "mq";
  }

  void TearDown() override {
    [[maybe_unused]] auto unused = MessageQueue::unlink(name_);
  }

  std::string name_;
};

}  // namespace

TEST_F(MessageQueueTest, CreateThenOpenThenUnlink) {
  auto writer = MessageQueue::create(
      name_, /*max_messages=*/2, /*message_size=*/64, 0600, Direction::write_only);
  ASSERT_TRUE(writer.has_value());
  EXPECT_EQ(writer->message_size(), 64);
  EXPECT_GE(writer->native_fd(), 0);

  auto reader = MessageQueue::open(name_, Direction::read_only);
  ASSERT_TRUE(reader.has_value());
  EXPECT_EQ(reader->message_size(), 64);

  EXPECT_TRUE(MessageQueue::unlink(name_).has_value());
}

TEST_F(MessageQueueTest, OpenNonexistentQueueFails) {
  const auto result = MessageQueue::open(name_, Direction::read_only);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), std::errc::no_such_file_or_directory);
}

TEST_F(MessageQueueTest, CreateAttachesToExistingCompatibleQueue) {
  auto first = MessageQueue::create(name_, 2, 64, 0600, Direction::write_only);
  ASSERT_TRUE(first.has_value());

  auto second = MessageQueue::create(name_, 2, 64, 0600, Direction::read_only);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->message_size(), 64);
}

TEST_F(MessageQueueTest, CreateDetectsIncompatibleAttributes) {
  auto first = MessageQueue::create(name_, 2, 64, 0600, Direction::write_only);
  ASSERT_TRUE(first.has_value());

  const auto second = MessageQueue::create(name_, 2, 128, 0600, Direction::write_only);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), mqtls::errc::queue_attr_mismatch);
}

TEST_F(MessageQueueTest, ExclusiveCreateFailsIfQueueAlreadyExists) {
  auto first = MessageQueue::create(name_, 2, 64, 0600, Direction::write_only);
  ASSERT_TRUE(first.has_value());

  const auto second =
      MessageQueue::create(name_, 2, 64, 0600, Direction::write_only, /*exclusive=*/true);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), std::errc::file_exists);
}

TEST_F(MessageQueueTest, SendLargerThanMessageSizeIsRejected) {
  auto writer = MessageQueue::create(name_, 2, 8, 0600, Direction::write_only);
  ASSERT_TRUE(writer.has_value());

  const std::byte payload[9] = {};
  const auto result = writer->try_send(payload);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::invalid_argument);
}

TEST_F(MessageQueueTest, ReceiveIntoTooSmallBufferIsRejected) {
  auto reader = MessageQueue::create(name_, 2, 64, 0600, Direction::read_only);
  ASSERT_TRUE(reader.has_value());

  std::byte buffer[32];
  const auto result = reader->try_receive(buffer);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::invalid_argument);
}

TEST_F(MessageQueueTest, ReceiveFromEmptyQueueWouldBlock) {
  auto reader = MessageQueue::create(name_, 2, 64, 0600, Direction::read_only);
  ASSERT_TRUE(reader.has_value());

  std::byte buffer[64];
  const auto result = reader->try_receive(buffer);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), std::errc::resource_unavailable_try_again);
}

TEST_F(MessageQueueTest, SendToFullQueueWouldBlock) {
  auto writer = MessageQueue::create(name_, /*max_messages=*/1, 8, 0600, Direction::write_only);
  ASSERT_TRUE(writer.has_value());

  const std::byte payload[4] = {};
  ASSERT_TRUE(writer->try_send(payload).has_value());

  const auto result = writer->try_send(payload);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), std::errc::resource_unavailable_try_again);
}

TEST_F(MessageQueueTest, SendThenReceiveRoundTrips) {
  auto writer = MessageQueue::create(name_, 2, 64, 0600, Direction::write_only);
  ASSERT_TRUE(writer.has_value());
  auto reader = MessageQueue::open(name_, Direction::read_only);
  ASSERT_TRUE(reader.has_value());

  const std::byte payload[3] = {std::byte{1}, std::byte{2}, std::byte{3}};
  const auto sent = writer->try_send(payload);
  ASSERT_TRUE(sent.has_value());
  EXPECT_EQ(*sent, 3U);

  std::byte buffer[64];
  const auto received = reader->try_receive(buffer);
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(*received, 3U);
  EXPECT_EQ(buffer[0], payload[0]);
  EXPECT_EQ(buffer[1], payload[1]);
  EXPECT_EQ(buffer[2], payload[2]);
}

TEST_F(MessageQueueTest, NativeFdIsPollable) {
  auto writer = MessageQueue::create(name_, 1, 8, 0600, Direction::write_only);
  ASSERT_TRUE(writer.has_value());
  auto reader = MessageQueue::open(name_, Direction::read_only);
  ASSERT_TRUE(reader.has_value());

  // Nothing sent yet: the read side must not be readable.
  pollfd pfd{reader->native_fd(), POLLIN, 0};
  ASSERT_EQ(::poll(&pfd, 1, 0), 0);

  const std::byte payload[1] = {};
  ASSERT_TRUE(writer->try_send(payload).has_value());

  pfd = {reader->native_fd(), POLLIN, 0};
  ASSERT_EQ(::poll(&pfd, 1, 0), 1);
  EXPECT_NE(pfd.revents & POLLIN, 0);
}

TEST_F(MessageQueueTest, MoveTransfersOwnership) {
  auto writer = MessageQueue::create(name_, 1, 8, 0600, Direction::write_only);
  ASSERT_TRUE(writer.has_value());
  const int original_fd = writer->native_fd();

  MessageQueue moved(std::move(*writer));
  EXPECT_EQ(moved.native_fd(), original_fd);

  const std::byte payload[1] = {};
  EXPECT_TRUE(moved.try_send(payload).has_value());
}
