#ifndef MQTLS_MQ_HPP
#define MQTLS_MQ_HPP

/// @brief Umbrella header for the POSIX message-queue transport binding
/// (`mqtls::mq`, CMake target `mqtls::mq`). Kept separate from
/// `mqtls/mqtls.hpp` (the transport-agnostic core): code that only needs
/// the core — or that plugs in a different transport module entirely —
/// never includes this.
#include "mqtls/mq/channel.hpp"
#include "mqtls/mq/config.hpp"

#endif  // MQTLS_MQ_HPP
