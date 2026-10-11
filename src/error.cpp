#include "mqtls/error.hpp"

namespace mqtls {

namespace {

class ErrcCategory final : public std::error_category {
public:
  [[nodiscard]] const char* name() const noexcept override {
    return "mqtls";
  }

  [[nodiscard]] std::string message(int ev) const override {
    switch (static_cast<errc>(ev)) {
      case errc::ok:
        return "no error";
      case errc::timeout:
        return "the deadline passed before the operation completed";
      case errc::channel_closed:
        return "the channel was closed while the operation was waiting";
      case errc::peer_closed:
        return "the peer closed the TLS session (close_notify)";
      case errc::truncated:
        return "the transport ended without a TLS close_notify";
      case errc::queue_open_failed:
        return "opening, creating, or unlinking a POSIX message queue failed";
      case errc::queue_attr_mismatch:
        return "an existing message queue's attributes do not match the request";
      case errc::invalid_argument:
        return "a configuration value violates its documented contract";
      case errc::handshake_failed:
        return "the TLS handshake failed";
      case errc::certificate_verify_failed:
        return "the peer's certificate failed verification";
      case errc::tls_config_failed:
        return "configuring the TLS context failed";
      case errc::message_too_large:
        return "the message exceeds the configured maximum size";
      case errc::protocol_error:
        return "the TLS layer reported a protocol violation";
      case errc::io_error:
        return "the underlying transport failed unexpectedly";
    }
    return "unknown mqtls error";
  }
};

}  // namespace

const std::error_category& mqtls_category() noexcept {
  static const ErrcCategory category;
  return category;
}

std::error_code make_error_code(errc e) noexcept {
  return {static_cast<int>(e), mqtls_category()};
}

}  // namespace mqtls
