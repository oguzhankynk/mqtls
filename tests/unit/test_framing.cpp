#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "framing.hpp"

TEST(FramingTest, EncodeDecodeRoundTripsZero) {
  const auto header = mqtls::encode_frame_length(0);
  EXPECT_EQ(mqtls::decode_frame_length(header), 0U);
}

TEST(FramingTest, EncodeDecodeRoundTripsTypicalValue) {
  const auto header = mqtls::encode_frame_length(1234);
  EXPECT_EQ(mqtls::decode_frame_length(header), 1234U);
}

TEST(FramingTest, EncodeDecodeRoundTripsMaxUint32) {
  const auto header = mqtls::encode_frame_length(std::numeric_limits<std::uint32_t>::max());
  EXPECT_EQ(mqtls::decode_frame_length(header), std::numeric_limits<std::uint32_t>::max());
}

TEST(FramingTest, EncodingIsBigEndian) {
  const auto header = mqtls::encode_frame_length(0x01020304);
  EXPECT_EQ(static_cast<unsigned char>(header[0]), 0x01);
  EXPECT_EQ(static_cast<unsigned char>(header[1]), 0x02);
  EXPECT_EQ(static_cast<unsigned char>(header[2]), 0x03);
  EXPECT_EQ(static_cast<unsigned char>(header[3]), 0x04);
}

TEST(FramingTest, HeaderSizeIsFourBytes) {
  EXPECT_EQ(mqtls::kFrameHeaderSize, 4U);
}
