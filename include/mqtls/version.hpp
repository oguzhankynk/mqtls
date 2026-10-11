#ifndef MQTLS_VERSION_HPP
#define MQTLS_VERSION_HPP

/// @brief TLS 1.3 over POSIX message queues, peer-to-peer, by unit ID.
namespace mqtls {

/// @brief Returns the mqtls library version as "MAJOR.MINOR.PATCH".
/// @return A pointer to a static, null-terminated string. Never null.
/// @par Thread safety
/// Safe to call from any thread; the returned string is immutable.
/// @par Blocking
/// Never blocks.
const char* version() noexcept;

}  // namespace mqtls

#endif  // MQTLS_VERSION_HPP
