mqtls {#mainpage}
=====

A mutually authenticated TLS 1.3 layer over any byte-stream transport,
shipping with a peer-to-peer POSIX message-queue binding identified by unit
ID.

The core (`mqtls`, namespace `mqtls`) is deliberately transport-agnostic: its
only connection point to the outside world is the public
`mqtls::transport::ByteTransport` interface. `mqtls::mq` (CMake target
`mqtls_mq`) is the first and currently only concrete transport connector,
filling that interface with POSIX message queues — it is a bolt-on module,
not part of the core. See \subpage architecture_page for the full layer
breakdown and sequence diagrams, \subpage threading_page for the
thread-safety model, and \subpage security_page for the threat model and TLS
profile.

@code
mqtls::mq::MqPeerConfig peer;
peer.local_unit_id = 100;
peer.peer_unit_id = 101;

mqtls::TlsConfig tls;
tls.ca_file = "ca.pem";
tls.cert_file = "unit-100.pem";
tls.key_file = "unit-100.key";

auto channel = mqtls::mq::establish(peer, tls);
if (!channel) { /* handle channel.error() */ }

const char hello[] = "hello";
channel->send(mqtls::as_bytes(mqtls::span<const char>(hello, sizeof(hello) - 1)));
auto reply = channel->recv(std::chrono::seconds(5));
@endcode

See the top-level `README.md` for build instructions, the full example
programs under `examples/`, and `scripts/gen_certs.sh` for generating test
certificates.
