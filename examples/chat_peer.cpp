// A two-way chat: lines typed on stdin are sent to the peer, and whatever
// the peer sends is printed, from a separate thread, so both directions
// work at once (showing SecureChannel's full-duplex send()/recv()).
//
//   ./chat_peer --local-id 100 --peer-id 101 --ca ca.pem --cert unit-100.pem --key unit-100.key
//   ./chat_peer --local-id 101 --peer-id 100 --ca ca.pem --cert unit-101.pem --key unit-101.key
#include "mqtls/span.hpp"
#include <mqtls/mq.hpp>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

#include "common.hpp"

int main(int argc, char** argv) {
  const auto args = mqtls::examples::parse_args(argc, argv, argv[0]);
  if (!args) {
    return 1;
  }

  mqtls::mq::MqPeerConfig peer_config;
  peer_config.local_unit_id = args->local_id;
  peer_config.peer_unit_id = args->peer_id;

  mqtls::TlsConfig tls_config;
  tls_config.ca_file = args->ca_file;
  tls_config.cert_file = args->cert_file;
  tls_config.key_file = args->key_file;

  std::fprintf(
      stderr, "chat_peer: connecting (local=%u, peer=%u)...\n", args->local_id, args->peer_id);
  auto channel = mqtls::mq::establish(peer_config, tls_config);
  if (!channel) {
    std::fprintf(stderr, "chat_peer: establish() failed: %s\n", channel.error().message().c_str());
    return 1;
  }
  std::fprintf(stderr, "chat_peer: connected. Type a line and press enter; Ctrl-D to quit.\n");

  std::thread reader([&channel] {
    for (;;) {
      auto message = channel->recv(std::chrono::milliseconds(500));
      if (!message) {
        if (message.error() == mqtls::errc::timeout) {
          continue;
        }
        std::fprintf(
            stderr, "\nchat_peer: peer side ended (%s)\n", message.error().message().c_str());
        return;
      }
      std::string text(reinterpret_cast<const char*>(message->data()), message->size());
      std::fprintf(stderr, "\npeer> %s\nyou> ", text.c_str());
      std::fflush(stderr);
    }
  });

  std::string line;
  std::fprintf(stderr, "you> ");
  while (std::getline(std::cin, line)) {
    const auto* bytes = reinterpret_cast<const std::byte*>(line.data());
    if (auto sent = channel->send(mqtls::span<const std::byte>(bytes, line.size())); !sent) {
      std::fprintf(stderr, "chat_peer: send() failed: %s\n", sent.error().message().c_str());
      break;
    }
    std::fprintf(stderr, "you> ");
  }

  channel->close();
  reader.join();
  std::fprintf(stderr, "chat_peer: closed\n");
  return 0;
}
