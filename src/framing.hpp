#ifndef MQTLS_FRAMING_HPP
#define MQTLS_FRAMING_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace mqtls {

/// @brief The size, in bytes, of a frame's length header.
inline constexpr std::size_t kFrameHeaderSize = 4;

/// @brief Encodes `length` as a 4-byte big-endian header, the application-
/// level framing SecureChannel puts on top of the TLS byte stream: every
/// message is `encode_frame_length(payload.size()) + payload`.
/// @param length The payload length to encode; must fit in 32 bits
/// (validate(ChannelOptions) keeps max_message_size within that range).
/// @return The 4-byte big-endian encoding of `length`.
std::array<std::byte, kFrameHeaderSize> encode_frame_length(std::uint32_t length) noexcept;

/// @brief Decodes a header produced by encode_frame_length().
/// @param header The 4 bytes to decode.
/// @return The encoded length.
std::uint32_t decode_frame_length(const std::array<std::byte, kFrameHeaderSize>& header) noexcept;

}  // namespace mqtls

#endif  // MQTLS_FRAMING_HPP
