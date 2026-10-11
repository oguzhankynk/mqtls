#include "common.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mqtls::examples {

std::optional<Args> parse_args(int argc, char** argv, const char* program_name) {
  Args args;
  bool have_local = false;
  bool have_peer = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_next = i + 1 < argc;

    if (arg == "--local-id" && has_next) {
      args.local_id = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
      have_local = true;
    } else if (arg == "--peer-id" && has_next) {
      args.peer_id = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
      have_peer = true;
    } else if (arg == "--ca" && has_next) {
      args.ca_file = argv[++i];
    } else if (arg == "--cert" && has_next) {
      args.cert_file = argv[++i];
    } else if (arg == "--key" && has_next) {
      args.key_file = argv[++i];
    } else {
      std::fprintf(stderr, "Unrecognized or incomplete argument: %s\n", arg.c_str());
      have_local = false;
      break;
    }
  }

  if (!have_local || !have_peer || args.ca_file.empty() || args.cert_file.empty() ||
      args.key_file.empty()) {
    std::fprintf(stderr,
                 "Usage: %s --local-id <id> --peer-id <id> --ca <file> --cert <file> --key "
                 "<file>\n",
                 program_name);
    return std::nullopt;
  }
  return args;
}

}  // namespace mqtls::examples
