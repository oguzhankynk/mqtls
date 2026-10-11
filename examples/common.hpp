#ifndef MQTLS_EXAMPLES_COMMON_HPP
#define MQTLS_EXAMPLES_COMMON_HPP

#include <cstdint>
#include <optional>
#include <string>

namespace mqtls::examples {

/// @brief Command-line arguments shared by every example program:
/// `--local-id <id> --peer-id <id> --ca <file> --cert <file> --key <file>`.
struct Args {
  std::uint32_t local_id = 0;
  std::uint32_t peer_id = 0;
  std::string ca_file;
  std::string cert_file;
  std::string key_file;
};

/// @brief Parses `argv` into Args, or prints a usage message to stderr
/// (naming `program_name`) and returns std::nullopt if any argument is
/// missing or malformed.
std::optional<Args> parse_args(int argc, char** argv, const char* program_name);

}  // namespace mqtls::examples

#endif  // MQTLS_EXAMPLES_COMMON_HPP
