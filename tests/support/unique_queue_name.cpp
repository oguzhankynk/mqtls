#include "unique_queue_name.hpp"

#include <atomic>

#include <unistd.h>

namespace mqtls::test {

std::string unique_queue_prefix() {
  static std::atomic<unsigned long> counter{0};
  const unsigned long n = counter.fetch_add(1, std::memory_order_relaxed);
  return "/mqtls_test_" + std::to_string(::getpid()) + "_" + std::to_string(n) + "_id_";
}

}  // namespace mqtls::test
