#ifndef MQTLS_MQTLS_HPP
#define MQTLS_MQTLS_HPP

/// @brief Umbrella header for mqtls's transport-agnostic core.
///
/// Deliberately does not include the mqueue transport binding: code that
/// only needs the core, or that plugs in a different transport module
/// entirely, never needs `mqtls::mq` at all.
/// See `mqtls/mq.hpp` for that module's own umbrella.
#include "mqtls/config.hpp"
#include "mqtls/error.hpp"
#include "mqtls/secure_channel.hpp"
#include "mqtls/transport.hpp"
#include "mqtls/version.hpp"

#endif  // MQTLS_MQTLS_HPP
