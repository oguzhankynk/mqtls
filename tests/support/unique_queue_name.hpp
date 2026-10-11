#ifndef MQTLS_TEST_UNIQUE_QUEUE_NAME_HPP
#define MQTLS_TEST_UNIQUE_QUEUE_NAME_HPP

#include <string>

namespace mqtls::test {

/// @brief Returns a POSIX message-queue name prefix that no other call in
/// this process, and almost certainly no concurrently-running test binary,
/// will return: `"/mqtls_test_<pid>_<counter>_id_"`.
///
/// Intended as `mq::MqPeerConfig::queue_prefix` in tests, so that unit IDs
/// (which tests often pick as small, repeated constants like 100/101) never
/// collide across tests or parallel `ctest -j` runs.
/// @par Thread safety
/// Safe to call concurrently; each call returns a distinct value.
std::string unique_queue_prefix();

}  // namespace mqtls::test

#endif  // MQTLS_TEST_UNIQUE_QUEUE_NAME_HPP
