// A minimal peer that echoes every message it receives back to the sender.
// Run two instances with swapped --local-id/--peer-id to see them talk:
//
//   ./echo_peer --local-id 100 --peer-id 101 --ca ca.pem --cert unit-100.pem --key unit-100.key
//   ./echo_peer --local-id 101 --peer-id 100 --ca ca.pem --cert unit-101.pem --key unit-101.key
#include <mqtls/mq.hpp>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <optional>

#include "common.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) {
  g_stop = 1;
}

}  // namespace

int main(int argc, char** argv) {
  const auto args = mqtls::examples::parse_args(argc, argv, argv[0]);
  if (!args) {
    return 1;
  }

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  mqtls::mq::MqPeerConfig peer_config;
  peer_config.local_unit_id = args->local_id;
  peer_config.peer_unit_id = args->peer_id;

  mqtls::TlsConfig tls_config;
  tls_config.ca_file = args->ca_file;
  tls_config.cert_file = args->cert_file;
  tls_config.key_file = args->key_file;

  std::fprintf(
      stderr, "echo_peer: connecting (local=%u, peer=%u)...\n", args->local_id, args->peer_id);
  auto channel = mqtls::mq::establish(peer_config, tls_config);
  if (!channel) {
    std::fprintf(stderr, "echo_peer: establish() failed: %s\n", channel.error().message().c_str());
    return 1;
  }
  std::fprintf(stderr,
               "echo_peer: connected (%s, peer=%s)\n",
               channel->cipher().c_str(),
               channel->peer_subject().c_str());

  while (g_stop == 0) {
    auto message = channel->recv(std::chrono::milliseconds(500));
    if (!message) {
      if (message.error() == mqtls::errc::timeout) {
        continue;
      }
      std::fprintf(stderr, "echo_peer: recv() failed: %s\n", message.error().message().c_str());
      break;
    }
    std::fprintf(stderr, "echo_peer: received %zu bytes, echoing back\n", message->size());
    if (auto sent = channel->send(*message); !sent) {
      std::fprintf(stderr, "echo_peer: send() failed: %s\n", sent.error().message().c_str());
      break;
    }
  }

  channel->close();
  std::fprintf(stderr, "echo_peer: closed\n");
  return 0;
}
