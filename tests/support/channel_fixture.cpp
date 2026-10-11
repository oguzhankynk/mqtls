#include "channel_fixture.hpp"

#include <chrono>
#include <thread>
#include <utility>

namespace mqtls::test {

std::pair<mq::MqPeerConfig, mq::MqPeerConfig> MakeConfigPair(const std::string& queue_prefix,
                                                             mq::UnitId local_id,
                                                             mq::UnitId peer_id) {
  mq::MqPeerConfig local;
  local.queue_prefix = queue_prefix;
  local.local_unit_id = local_id;
  local.peer_unit_id = peer_id;
  // Tests must never hang forever: if the peer side fails for any reason
  // before creating its own queue, this bounds how long the other side
  // waits for it, turning a silent hang into a clear test failure. 30s
  // (rather than something tighter) gives headroom on a busy/oversubscribed
  // CI machine or under a sanitizer's overhead, where the peer thread may
  // simply not have been scheduled yet.
  local.peer_wait_timeout = std::chrono::seconds(30);

  mq::MqPeerConfig peer = local;
  peer.local_unit_id = peer_id;
  peer.peer_unit_id = local_id;

  return {local, peer};
}

ChannelPair EstablishBothSides(const mq::MqPeerConfig& local_config,
                               const TlsConfig& local_tls,
                               const mq::MqPeerConfig& peer_config,
                               const TlsConfig& peer_tls,
                               const ChannelOptions& options) {
  ChannelPair result{tl::make_unexpected(make_error_code(errc::io_error)),
                     tl::make_unexpected(make_error_code(errc::io_error))};

  std::thread local_thread([&] { result.local = mq::establish(local_config, local_tls, options); });
  std::thread peer_thread([&] { result.peer = mq::establish(peer_config, peer_tls, options); });
  local_thread.join();
  peer_thread.join();
  return result;
}

}  // namespace mqtls::test
