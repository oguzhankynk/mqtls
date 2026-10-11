#include "mqtls/mq/config.hpp"

#include <gtest/gtest.h>

#include <climits>

namespace {

mqtls::mq::MqPeerConfig MakeConfig(mqtls::mq::UnitId local, mqtls::mq::UnitId peer) {
  mqtls::mq::MqPeerConfig config;
  config.local_unit_id = local;
  config.peer_unit_id = peer;
  return config;
}

}  // namespace

TEST(MqPeerConfigTest, ValidConfigPasses) {
  const auto config = MakeConfig(100, 101);
  EXPECT_TRUE(mqtls::mq::validate(config).has_value());
}

TEST(MqPeerConfigTest, SameLocalAndPeerIdIsRejected) {
  const auto config = MakeConfig(100, 100);
  const auto result = mqtls::mq::validate(config);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::invalid_argument);
}

TEST(MqPeerConfigTest, PrefixMustStartWithSlash) {
  auto config = MakeConfig(100, 101);
  config.queue_prefix = "tls_msgQ_id_";
  EXPECT_FALSE(mqtls::mq::validate(config).has_value());
}

TEST(MqPeerConfigTest, PrefixMustNotContainAnotherSlash) {
  auto config = MakeConfig(100, 101);
  config.queue_prefix = "/tls/msgQ_id_";
  EXPECT_FALSE(mqtls::mq::validate(config).has_value());
}

TEST(MqPeerConfigTest, EmptyPrefixIsRejected) {
  auto config = MakeConfig(100, 101);
  config.queue_prefix.clear();
  EXPECT_FALSE(mqtls::mq::validate(config).has_value());
}

TEST(MqPeerConfigTest, NonPositiveMaxMessagesIsRejected) {
  auto config = MakeConfig(100, 101);
  config.max_messages = 0;
  EXPECT_FALSE(mqtls::mq::validate(config).has_value());
}

TEST(MqPeerConfigTest, TooSmallMessageSizeIsRejected) {
  auto config = MakeConfig(100, 101);
  config.message_size = 511;
  EXPECT_FALSE(mqtls::mq::validate(config).has_value());
}

TEST(MqPeerConfigTest, MinimumMessageSizeIsAccepted) {
  auto config = MakeConfig(100, 101);
  config.message_size = 512;
  EXPECT_TRUE(mqtls::mq::validate(config).has_value());
}

TEST(MqPeerConfigTest, EmptyPeerNamePrefixIsRejected) {
  auto config = MakeConfig(100, 101);
  config.peer_name_prefix.clear();
  const auto result = mqtls::mq::validate(config);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::invalid_argument);
}

TEST(MqPeerConfigTest, ZeroPeerPollIntervalIsRejected) {
  auto config = MakeConfig(100, 101);
  config.peer_poll_interval = mqtls::Timeout::zero();
  EXPECT_FALSE(mqtls::mq::validate(config).has_value());
}

TEST(MqPeerConfigTest, InfinitePeerWaitTimeoutIsAccepted) {
  auto config = MakeConfig(100, 101);
  config.peer_wait_timeout = mqtls::kInfinite;
  EXPECT_TRUE(mqtls::mq::validate(config).has_value());
}

TEST(MqPeerConfigTest, QueueNameAtNameMaxIsAccepted) {
  auto config = MakeConfig(100, 101);
  const std::string suffix = "100_tx";
  config.queue_prefix = "/" + std::string(NAME_MAX - suffix.size() - 1, 'x');
  ASSERT_EQ(mqtls::mq::tx_queue_name(config).size(), static_cast<std::size_t>(NAME_MAX));
  EXPECT_TRUE(mqtls::mq::validate(config).has_value());
}

TEST(MqPeerConfigTest, QueueNameOverNameMaxIsRejected) {
  auto config = MakeConfig(100, 101);
  const std::string suffix = "100_tx";
  config.queue_prefix = "/" + std::string(NAME_MAX - suffix.size(), 'x');
  ASSERT_GT(mqtls::mq::tx_queue_name(config).size(), static_cast<std::size_t>(NAME_MAX));
  const auto result = mqtls::mq::validate(config);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), mqtls::errc::invalid_argument);
}

TEST(MqPeerConfigTest, QueueNamesAreDerivedFromPrefixAndIds) {
  const auto config = MakeConfig(100, 101);
  EXPECT_EQ(mqtls::mq::tx_queue_name(config), "/tls_msgQ_id_100_tx");
  EXPECT_EQ(mqtls::mq::rx_queue_name(config), "/tls_msgQ_id_101_tx");
}

TEST(MqPeerConfigTest, LowerUnitIdIsServer) {
  EXPECT_EQ(mqtls::mq::tls_role(MakeConfig(100, 101)), mqtls::Role::server);
  EXPECT_EQ(mqtls::mq::tls_role(MakeConfig(101, 100)), mqtls::Role::client);
}

TEST(MqPeerConfigTest, ExpectedPeerNameUsesPrefixAndId) {
  const auto config = MakeConfig(100, 101);
  EXPECT_EQ(mqtls::mq::expected_peer_name(config), "unit-101");
}

TEST(MqPeerConfigTest, ExpectedPeerNameHonorsCustomPrefix) {
  auto config = MakeConfig(100, 101);
  config.peer_name_prefix = "node-";
  EXPECT_EQ(mqtls::mq::expected_peer_name(config), "node-101");
}
