#include "framing.hpp"

namespace mqtls {

namespace {

constexpr int kByteBits = 8;
constexpr std::uint32_t kByteMask = 0xFFU;

}  // namespace

std::array<std::byte, kFrameHeaderSize> encode_frame_length(std::uint32_t length) noexcept {
  return {
      static_cast<std::byte>((length >> (3 * kByteBits)) & kByteMask),
      static_cast<std::byte>((length >> (2 * kByteBits)) & kByteMask),
      static_cast<std::byte>((length >> kByteBits) & kByteMask),
      static_cast<std::byte>(length & kByteMask),
  };
}

std::uint32_t decode_frame_length(const std::array<std::byte, kFrameHeaderSize>& header) noexcept {
  return (static_cast<std::uint32_t>(header[0]) << (3 * kByteBits)) |
         (static_cast<std::uint32_t>(header[1]) << (2 * kByteBits)) |
         (static_cast<std::uint32_t>(header[2]) << kByteBits) |
         static_cast<std::uint32_t>(header[3]);
}

}  // namespace mqtls
