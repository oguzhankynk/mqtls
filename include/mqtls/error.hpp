#ifndef MQTLS_ERROR_HPP
#define MQTLS_ERROR_HPP

#include <chrono>
#include <system_error>
#include <tl/expected.hpp>
#include <type_traits>

/// @brief A mutually authenticated TLS 1.3 layer over any byte-stream
/// transport that implements mqtls::transport::ByteTransport.
namespace mqtls {

/// @brief Error codes mqtls operations report through mqtls::Result.
///
/// Every non-`ok` value maps to a `std::error_condition` with a
/// human-readable message via mqtls_category(); see each operation's own
/// documentation for which of these it can return.
enum class errc {
  ok = 0,                     ///< No error.
  timeout,                    ///< A deadline passed before the operation completed.
  channel_closed,             ///< The channel was closed (locally) while waiting.
  peer_closed,                ///< The peer sent a TLS close_notify.
  truncated,                  ///< The transport ended without a close_notify.
  queue_open_failed,          ///< mq_open()/mq_unlink() failed; see errno context in logs.
  queue_attr_mismatch,        ///< An existing queue's attributes don't match the request.
  invalid_argument,           ///< A configuration value violates its documented contract.
  handshake_failed,           ///< The TLS handshake failed for a reason other than
                              ///< certificate verification.
  certificate_verify_failed,  ///< The peer's certificate chain or identity failed
                              ///< verification.
  tls_config_failed,          ///< SSL_CTX configuration (ciphers, groups, files) failed.
  message_too_large,          ///< A message exceeded ChannelOptions::max_message_size.
  protocol_error,             ///< The TLS layer reported a protocol violation.
  io_error,                   ///< The underlying transport failed unexpectedly.
};

/// @brief Returns the single, process-wide mqtls::errc error category.
/// @return A reference valid for the lifetime of the program.
/// @par Thread safety
/// Safe to call from any thread.
const std::error_category& mqtls_category() noexcept;

/// @brief Builds a std::error_code from an mqtls::errc value.
/// @param e The error value to wrap.
/// @return The equivalent std::error_code, in mqtls_category().
/// @par Thread safety
/// Safe to call from any thread.
std::error_code make_error_code(errc e) noexcept;

/// @brief The result of a fallible mqtls operation: either a `T`, or a
/// `std::error_code` (whose category is always mqtls_category() for errors
/// this library raises itself).
///
/// mqtls never throws for its own error conditions;
/// callers check `if (result)` or call `result.error()`. This alias exists so
/// the project can switch to `std::expected` (C++23) without touching call
/// sites.
template <class T>
using Result = tl::expected<T, std::error_code>;

/// @brief The duration type every blocking mqtls operation accepts.
using Timeout = std::chrono::milliseconds;

/// @brief A Timeout value meaning "wait forever".
inline constexpr Timeout kInfinite{-1};

}  // namespace mqtls

namespace std {

/// @brief Registers mqtls::errc as a std::error_code-compatible enum, so
/// `make_error_code(mqtls::errc::...)` is found by argument-dependent lookup
/// and `std::error_code{mqtls::errc::...}` works implicitly.
template <>
struct is_error_code_enum<mqtls::errc> : true_type {};

}  // namespace std

#endif  // MQTLS_ERROR_HPP
