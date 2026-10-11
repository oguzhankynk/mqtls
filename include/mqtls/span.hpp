#ifndef MQTLS_SPAN_HPP
#define MQTLS_SPAN_HPP

#include <nonstd/span.hpp>

namespace mqtls {

/// @brief A non-owning view over a contiguous sequence, standing in for
/// `std::span` (C++20) on this project's C++17 baseline. Backed by
/// span-lite's `nonstd::span`, which mirrors the same API (`data()`,
/// `size()`, `operator[]`, `subspan()`, iterators, ...).
template <class T>
using span = nonstd::span<T>;

/// @brief Equivalent of `std::dynamic_extent`.
inline constexpr std::size_t dynamic_extent = nonstd::dynamic_extent;

/// @brief Equivalent of `std::as_bytes`.
using nonstd::span_lite::as_bytes;

/// @brief Equivalent of `std::as_writable_bytes`.
using nonstd::span_lite::as_writable_bytes;

}  // namespace mqtls

#endif  // MQTLS_SPAN_HPP
