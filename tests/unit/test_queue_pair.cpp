#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include <mqueue.h>

#include "mq/message_queue.hpp"
#include "mq/queue_pair.hpp"
#include "unique_queue_name.hpp"
#include "util/deadline.hpp"
#include "util/wait.hpp"

namespace {

using mqtls::Timeout;
using mqtls::mq::Direction;
using mqtls::mq::MessageQueue;
using mqtls::mq::MqPeerConfig;
using mqtls::mq::QueuePair;
using mqtls::mq::rx_queue_name;
using mqtls::mq::tx_queue_name;
using mqtls::util::Deadline;
using mqtls::util::WakeFd;

class QueuePairTest : public ::testing::Test {
protected:
  void SetUp() override {
    config_.queue_prefix = mqtls::test::unique_queue_prefix();
    config_.local_unit_id = 100;
    config_.peer_unit_id = 101;
    config_.max_messages = 2;
    config_.message_size = 512;  // validate() requires >= 512
    config_.peer_poll_interval = Timeout(10);
  }

  void TearDown() override {
    [[maybe_unused]] auto a = MessageQueue::unlink(tx_queue_name(config_));
    [[maybe_unused]] auto b = MessageQueue::unlink(rx_queue_name(config_));
  }

  MqPeerConfig config_;
};

}  // namespace

TEST_F(QueuePairTest, ConnectsWhenPeerAlreadyExists) {
  MqPeerConfig peer_config = config_;
  std::swap(peer_config.local_unit_id, peer_config.peer_unit_id);
  auto peer_tx = MessageQueue::create(tx_queue_name(peer_config),
                                      peer_config.max_messages,
                                      peer_config.message_size,
                                      peer_config.mode,
                                      Direction::write_only,
                                      /*exclusive=*/true);
  ASSERT_TRUE(peer_tx.has_value());

  auto pair = QueuePair::establish(config_, Deadline(Timeout(1000)));
  ASSERT_TRUE(pair.has_value());

  [[maybe_unused]] auto unused = MessageQueue::unlink(tx_queue_name(peer_config));
}

TEST_F(QueuePairTest, ConnectsWhenPeerStartsLater) {
  MqPeerConfig peer_config = config_;
  std::swap(peer_config.local_unit_id, peer_config.peer_unit_id);

  std::promise<void> peer_started;
  std::thread peer_thread([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto peer_tx = MessageQueue::create(tx_queue_name(peer_config),
                                        peer_config.max_messages,
                                        peer_config.message_size,
                                        peer_config.mode,
                                        Direction::write_only,
                                        /*exclusive=*/true);
    peer_started.set_value();
  });

  auto pair = QueuePair::establish(config_, Deadline(Timeout(2000)));
  EXPECT_TRUE(pair.has_value());

  peer_thread.join();
  [[maybe_unused]] auto unused = MessageQueue::unlink(tx_queue_name(peer_config));
}

TEST_F(QueuePairTest, TimesOutWhenPeerNeverAppearsAndRemovesOwnTxQueue) {
  const auto result = QueuePair::establish(config_, Deadline(Timeout(100)));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::timeout);

  // The TX queue this call created must not have leaked.
  const auto reopened = MessageQueue::open(tx_queue_name(config_), Direction::write_only);
  EXPECT_FALSE(reopened.has_value());
  EXPECT_EQ(reopened.error(), std::errc::no_such_file_or_directory);
}

TEST_F(QueuePairTest, CancelStopsTheWaitEarly) {
  auto wake = WakeFd::create();
  ASSERT_TRUE(wake.has_value());

  std::thread canceller([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    wake->signal();
  });

  const auto start = std::chrono::steady_clock::now();
  const auto result = QueuePair::establish(config_, Deadline(mqtls::kInfinite), &*wake);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::channel_closed);
  EXPECT_LT(elapsed, std::chrono::seconds(2));
  canceller.join();
}

TEST_F(QueuePairTest, StaleOwnTxQueueIsClearedAndRecreated) {
  // Simulate a leftover queue from a previous, uncleanly-terminated run.
  auto stale = MessageQueue::create(tx_queue_name(config_),
                                    config_.max_messages,
                                    config_.message_size,
                                    config_.mode,
                                    Direction::write_only,
                                    /*exclusive=*/true);
  ASSERT_TRUE(stale.has_value());
  // Simulate it still being "open" by a dead process by just leaking this
  // handle's fd via leaving `stale` alive isn't representative of a real
  // stale queue (no process has it open after this test's process exits);
  // the queue's *name* existing is what establish() must handle, so drop
  // the handle but keep the name registered.

  MqPeerConfig peer_config = config_;
  std::swap(peer_config.local_unit_id, peer_config.peer_unit_id);
  auto peer_tx = MessageQueue::create(tx_queue_name(peer_config),
                                      peer_config.max_messages,
                                      peer_config.message_size,
                                      peer_config.mode,
                                      Direction::write_only,
                                      /*exclusive=*/true);
  ASSERT_TRUE(peer_tx.has_value());

  const auto result = QueuePair::establish(config_, Deadline(Timeout(1000)));
  EXPECT_TRUE(result.has_value());

  [[maybe_unused]] auto unused = MessageQueue::unlink(tx_queue_name(peer_config));
}

TEST_F(QueuePairTest, DestructorRemovesOnlyOwnTxQueue) {
  MqPeerConfig peer_config = config_;
  std::swap(peer_config.local_unit_id, peer_config.peer_unit_id);
  auto peer_tx = MessageQueue::create(tx_queue_name(peer_config),
                                      peer_config.max_messages,
                                      peer_config.message_size,
                                      peer_config.mode,
                                      Direction::write_only,
                                      /*exclusive=*/true);
  ASSERT_TRUE(peer_tx.has_value());

  {
    auto pair = QueuePair::establish(config_, Deadline(Timeout(1000)));
    ASSERT_TRUE(pair.has_value());
  }  // ~QueuePair runs here.

  const auto own_tx = MessageQueue::open(tx_queue_name(config_), Direction::write_only);
  EXPECT_FALSE(own_tx.has_value());
  EXPECT_EQ(own_tx.error(), std::errc::no_such_file_or_directory);

  // The peer's queue (this QueuePair's rx) must still exist: we only ever
  // read from it, never unlinked it.
  const auto peer_still_there =
      MessageQueue::open(tx_queue_name(peer_config), Direction::read_only);
  EXPECT_TRUE(peer_still_there.has_value());

  [[maybe_unused]] auto unused = MessageQueue::unlink(tx_queue_name(peer_config));
}

TEST_F(QueuePairTest, WriteSomeChunksLargerDataAndPreservesOrder) {
  MqPeerConfig peer_config = config_;
  std::swap(peer_config.local_unit_id, peer_config.peer_unit_id);
  config_.max_messages = 4;
  peer_config.max_messages = 4;
  // Only needs to exist so establish() finds a queue to read from; this
  // test never sends anything through it.
  auto peer_tx = MessageQueue::create(tx_queue_name(peer_config),
                                      peer_config.max_messages,
                                      peer_config.message_size,
                                      peer_config.mode,
                                      Direction::write_only,
                                      /*exclusive=*/true);
  ASSERT_TRUE(peer_tx.has_value());

  auto pair = QueuePair::establish(config_, Deadline(Timeout(1000)));
  ASSERT_TRUE(pair.has_value());

  // A second reader attached to `pair`'s own TX queue (already created by
  // establish() above), to observe exactly what write_some() sends.
  auto own_tx_reader = MessageQueue::open(tx_queue_name(config_), Direction::read_only);
  ASSERT_TRUE(own_tx_reader.has_value());

  // message_size is 512; send more than 2x that, so it must take multiple
  // chunked write_some() calls, each at most message_size bytes, with byte
  // order preserved.
  std::vector<std::byte> payload(1100);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::byte>(i % 256);
  }

  std::size_t offset = 0;
  int calls = 0;
  while (offset < payload.size()) {
    const auto sent = pair->write_some(mqtls::span<std::byte>(payload).subspan(offset));
    ASSERT_TRUE(sent.has_value());
    ASSERT_GT(*sent, 0U);
    EXPECT_LE(*sent, static_cast<std::size_t>(config_.message_size));
    offset += *sent;
    ++calls;
    ASSERT_LT(calls, 100) << "write_some() made no progress";
  }
  EXPECT_GT(calls, 1);

  std::vector<std::byte> received;
  while (received.size() < payload.size()) {
    std::vector<char> buf(static_cast<std::size_t>(config_.message_size));
    unsigned int prio = 0;
    const auto n = ::mq_receive(own_tx_reader->native_fd(), buf.data(), buf.size(), &prio);
    ASSERT_GE(n, 0);
    for (int i = 0; i < n; ++i) {
      received.push_back(static_cast<std::byte>(buf[static_cast<std::size_t>(i)]));
    }
  }
  EXPECT_EQ(received, payload);

  [[maybe_unused]] auto unused = MessageQueue::unlink(tx_queue_name(peer_config));
}

TEST_F(QueuePairTest, ReadSomePreservesLeftoverAcrossCalls) {
  MqPeerConfig peer_config = config_;
  std::swap(peer_config.local_unit_id, peer_config.peer_unit_id);
  auto peer_tx = MessageQueue::create(tx_queue_name(peer_config),
                                      peer_config.max_messages,
                                      peer_config.message_size,
                                      peer_config.mode,
                                      Direction::write_only,
                                      /*exclusive=*/true);
  ASSERT_TRUE(peer_tx.has_value());

  auto pair = QueuePair::establish(config_, Deadline(Timeout(1000)));
  ASSERT_TRUE(pair.has_value());

  const char message[] = "ABCDEF";  // 6 bytes, under message_size (8)
  ASSERT_GE(peer_tx->try_send(mqtls::as_bytes(mqtls::span<const char>(message, 6))).value_or(0),
            0U);

  std::byte first[2];
  const auto first_read = pair->read_some(first);
  ASSERT_TRUE(first_read.has_value());
  EXPECT_EQ(*first_read, 2U);
  EXPECT_EQ(pair->pending_read(), 4U);

  std::byte rest[16];
  const auto second_read = pair->read_some(rest);
  ASSERT_TRUE(second_read.has_value());
  EXPECT_EQ(*second_read, 4U);
  EXPECT_EQ(pair->pending_read(), 0U);

  EXPECT_EQ(static_cast<char>(first[0]), 'A');
  EXPECT_EQ(static_cast<char>(first[1]), 'B');
  EXPECT_EQ(static_cast<char>(rest[0]), 'C');
  EXPECT_EQ(static_cast<char>(rest[3]), 'F');

  [[maybe_unused]] auto unused = MessageQueue::unlink(tx_queue_name(peer_config));
}

TEST_F(QueuePairTest, WriteSomeReturnsZeroWhenQueueIsFull) {
  MqPeerConfig peer_config = config_;
  std::swap(peer_config.local_unit_id, peer_config.peer_unit_id);
  config_.max_messages = 1;
  peer_config.max_messages = 1;
  // Only needs to exist so establish() finds a queue to read from; this
  // test never sends anything through it, and nobody drains `pair`'s own
  // TX queue either, which is the point (it fills up after one message).
  auto peer_tx = MessageQueue::create(tx_queue_name(peer_config),
                                      peer_config.max_messages,
                                      peer_config.message_size,
                                      peer_config.mode,
                                      Direction::write_only,
                                      /*exclusive=*/true);
  ASSERT_TRUE(peer_tx.has_value());

  auto pair = QueuePair::establish(config_, Deadline(Timeout(1000)));
  ASSERT_TRUE(pair.has_value());

  const std::vector<std::byte> chunk(static_cast<std::size_t>(config_.message_size));
  const auto first = pair->write_some(chunk);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(*first, chunk.size());

  const auto second = pair->write_some(chunk);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*second, 0U);

  [[maybe_unused]] auto unused = MessageQueue::unlink(tx_queue_name(peer_config));
}
