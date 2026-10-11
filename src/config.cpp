#include "mqtls/config.hpp"

#include <cstdint>
#include <limits>

namespace mqtls {

Result<void> validate(const TlsConfig& config) {
  if (config.ca_file.empty() || config.cert_file.empty() || config.key_file.empty()) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  if (config.ciphersuites.empty() || config.groups.empty() || config.sigalgs.empty()) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  return {};
}

Result<void> validate(const ChannelOptions& options) {
  if (options.max_message_size == 0 ||
      options.max_message_size > std::numeric_limits<std::uint32_t>::max()) {
    // The upper bound matches SecureChannel's 4-byte frame length header;
    // it is far above any sane default (1 MiB) and only guards against a
    // value framing literally cannot encode.
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  if (options.handshake_timeout != kInfinite && options.handshake_timeout <= Timeout::zero()) {
    return tl::make_unexpected(make_error_code(errc::invalid_argument));
  }
  return {};
}

}  // namespace mqtls
