// Demonstrates several threads calling send() concurrently on the same
// channel (serialized internally, never interleaved) while a separate
// thread receives everything.
//
//   ./multithread_peer --local-id 100 --peer-id 101 --ca ca.pem --cert unit-100.pem --key
//   unit-100.key
//   ./multithread_peer --local-id 101 --peer-id 100 --ca ca.pem --cert unit-101.pem --key
//   unit-101.key
#include "mqtls/span.hpp"
#include <mqtls/mq.hpp>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "common.hpp"

namespace {

constexpr int kThreadsPerSide = 4;
constexpr int kMessagesPerThread = 25;

}  // namespace

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

  std::fprintf(stderr,
               "multithread_peer: connecting (local=%u, peer=%u)...\n",
               args->local_id,
               args->peer_id);
  auto channel = mqtls::mq::establish(peer_config, tls_config);
  if (!channel) {
    std::fprintf(
        stderr, "multithread_peer: establish() failed: %s\n", channel.error().message().c_str());
    return 1;
  }

  std::vector<std::thread> senders;
  for (int t = 0; t < kThreadsPerSide; ++t) {
    senders.emplace_back([&channel, t] {
      for (int i = 0; i < kMessagesPerThread; ++i) {
        const std::string text = "thread " + std::to_string(t) + " message " + std::to_string(i);
        const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
        if (auto sent = channel->send(mqtls::span<const std::byte>(bytes, text.size())); !sent) {
          std::fprintf(
              stderr, "multithread_peer: send() failed: %s\n", sent.error().message().c_str());
          return;
        }
      }
    });
  }

  int received = 0;
  const int expected = kThreadsPerSide * kMessagesPerThread;
  while (received < expected) {
    auto message = channel->recv(std::chrono::seconds(5));
    if (!message) {
      std::fprintf(
          stderr, "multithread_peer: recv() failed: %s\n", message.error().message().c_str());
      break;
    }
    ++received;
    std::fprintf(stderr,
                 "multithread_peer: received %d/%d (%zu bytes)\n",
                 received,
                 expected,
                 message->size());
  }

  for (auto& sender : senders) {
    sender.join();
  }

  channel->close();
  std::fprintf(stderr, "multithread_peer: closed\n");
  return 0;
}
