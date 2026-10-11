#include "tls/bio.hpp"

#include "mqtls/span.hpp"

#include <cstddef>
#include <mutex>

#include "tls/openssl_raii.hpp"

namespace mqtls::tls {

namespace {

BIO_METHOD* g_method = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::once_flag g_method_once;

int bio_create(BIO* b) {
  BIO_set_init(b, 1);
  BIO_set_data(b, nullptr);
  // This BIO never owns the transport it wraps (make_bio()'s contract), so
  // BIO_free() must never try to close/release it.
  BIO_set_shutdown(b, 0);
  return 1;
}

int bio_destroy(BIO* b) {
  if (b == nullptr) {
    return 0;
  }
  BIO_set_data(b, nullptr);
  BIO_set_init(b, 0);
  return 1;
}

int bio_read(BIO* b, char* out, int outl) {
  BIO_clear_retry_flags(b);
  if (out == nullptr || outl <= 0) {
    return 0;
  }
  auto* transport = static_cast<transport::ByteTransport*>(BIO_get_data(b));
  auto result = transport->read_some(
      mqtls::span<std::byte>(reinterpret_cast<std::byte*>(out), static_cast<std::size_t>(outl)));
  if (!result) {
    // errc::truncated (confirmed end of stream) or errc::io_error: a hard
    // failure either way, so no retry flag; OpenSSL reports this as
    // SSL_ERROR_SYSCALL.
    return -1;
  }
  if (*result == 0) {
    BIO_set_retry_read(b);
    return -1;
  }
  return static_cast<int>(*result);
}

int bio_write(BIO* b, const char* in, int inl) {
  BIO_clear_retry_flags(b);
  if (in == nullptr || inl <= 0) {
    return 0;
  }
  auto* transport = static_cast<transport::ByteTransport*>(BIO_get_data(b));
  auto result = transport->write_some(mqtls::span<const std::byte>(
      reinterpret_cast<const std::byte*>(in), static_cast<std::size_t>(inl)));
  if (!result) {
    return -1;
  }
  if (*result == 0) {
    BIO_set_retry_write(b);
    return -1;
  }
  return static_cast<int>(*result);
}

// A fixed OpenSSL BIO_meth_set_ctrl() callback signature; the parameter
// order isn't ours to change.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
long bio_ctrl(BIO* b, int cmd, long num, void* ptr) {
  (void)ptr;
  switch (cmd) {
    case BIO_CTRL_FLUSH:
      return 1;
    case BIO_CTRL_PENDING: {
      auto* transport = static_cast<transport::ByteTransport*>(BIO_get_data(b));
      return transport != nullptr ? static_cast<long>(transport->pending_read()) : 0;
    }
    case BIO_CTRL_WPENDING:
    // Confirmed end-of-stream (BIO_CTRL_EOF) is reported through
    // bio_read()'s return value, not through this ctrl command, so it
    // shares WPENDING's trivial answer here.
    case BIO_CTRL_EOF:
      return 0;
    case BIO_CTRL_GET_CLOSE:
      return BIO_get_shutdown(b);
    case BIO_CTRL_SET_CLOSE:
      BIO_set_shutdown(b, static_cast<int>(num));
      return 1;
    case BIO_CTRL_DUP:
      return 1;
    default:
      return 0;
  }
}

void init_method() {
  const int index = BIO_get_new_index() | BIO_TYPE_SOURCE_SINK;
  BioMethodPtr candidate(BIO_meth_new(index, "mqtls transport BIO"));
  if (!candidate) {
    return;
  }
  if (BIO_meth_set_create(candidate.get(), bio_create) == 0 ||
      BIO_meth_set_destroy(candidate.get(), bio_destroy) == 0 ||
      BIO_meth_set_read(candidate.get(), bio_read) == 0 ||
      BIO_meth_set_write(candidate.get(), bio_write) == 0 ||
      BIO_meth_set_ctrl(candidate.get(), bio_ctrl) == 0) {
    return;  // candidate's destructor frees the half-configured method.
  }
  // Deliberately never freed: this method is a process-wide singleton for
  // the lifetime of the program, same as the rest of OpenSSL's built-in
  // BIO_METHODs.
  g_method = candidate.release();
}

}  // namespace

BIO* make_bio(transport::ByteTransport& transport) {
  std::call_once(g_method_once, init_method);
  if (g_method == nullptr) {
    return nullptr;
  }
  BIO* bio = BIO_new(g_method);
  if (bio == nullptr) {
    return nullptr;
  }
  BIO_set_data(bio, &transport);
  return bio;
}

}  // namespace mqtls::tls
