# mqtls — a TLS layer over POSIX message queue 

**mqtls** is a C++17 library for Linux that carries a mutually authenticated
TLS 1.3 session over a byte-stream transport you choose. The core library
(`mqtls`) knows nothing about *how* the bytes actually move — it talks to a
single abstract interface, `mqtls::transport::ByteTransport`. The library
ships one concrete transport, `mqtls::mq`, which carries that byte stream
over a pair of POSIX message queues (`mq_open(3)`) instead of a TCP socket.
Adding support for another medium (UART, shared memory, CAN, ...) means
implementing that one interface — nothing in the core changes.

Two units, each identified by a numeric **unit ID**, form a peer-to-peer
secure channel; there is no client/server configuration to get wrong, no
listener, and no third-party broker.

## Why?

Plain IPC transports (POSIX message queues among them) are simple,
kernel-backed mechanisms with no confidentiality, no integrity, and no real
peer authentication — file-mode permissions on a queue's name are not
identity. Any process that can open the queue can read, inject, or replay
messages.

mqtls adds a TLS 1.3 layer on top of whatever transport you give it:

- **Confidentiality and integrity** for every byte exchanged between two
  processes.
- **Mutual authentication** using X.509 certificates (mTLS). Each side
  proves its identity, and each side checks that the peer is who it claims
  to be.
- **A defense-in-depth boundary** between processes on the same host, for
  example between components with different privilege levels or trust
  domains.

## Design

```
┌──────────────────────────────────────────────────────────────┐
│ CORE (namespace mqtls, target mqtls) — transport-agnostic     │
│                                                                │
│  SecureChannel          framing, locking, send()/recv()/close()│
│  tls::Context, tls::Bio  SSL_CTX setup, OpenSSL BIO bridge      │
│  transport::ByteTransport  ← the one seam a new transport fills │
└──────────────────────────────────────────────────────────────┘
                              ▲ implements
          ┌───────────────────┴───────────────────┐
┌───────────────────────┐              ┌───────────────────────────┐
│ mqtls::mq (target      │              │ a transport you write      │
│ mqtls_mq) — ships here  │              │ (UART, shared memory, CAN,  │
│ QueuePair over POSIX    │              │ ...) — not part of this repo │
│ message queues           │              │                             │
└───────────────────────┘              └───────────────────────────┘
```

`mqtls` (the core) never links against `librt` or touches `<mqueue.h>` —
only `mqtls::mq` does. Building with `-DMQTLS_BUILD_MQ_TRANSPORT=OFF` (the
`core-only` preset) compiles and tests the core completely on its own,
which is both a usability property and a standing proof that the core/mq
separation holds.

See [`docs/architecture.md`](docs/architecture.md) for the full layer
breakdown, sequence diagrams, and a walkthrough of adding a new transport;
[`docs/threading.md`](docs/threading.md) for the concurrency model; and
[`docs/security.md`](docs/security.md) for the threat model and TLS profile.

## Quick example

Using the bundled message-queue transport:

```cpp
#include <mqtls/mq.hpp>

mqtls::mq::MqPeerConfig peer;
peer.local_unit_id = 100;
peer.peer_unit_id = 101;

mqtls::TlsConfig tls;
tls.ca_file = "ca.pem";
tls.cert_file = "unit-100.pem";
tls.key_file = "unit-100.key";

// Creates our own queue, waits for the peer's, then runs the TLS handshake.
auto channel = mqtls::mq::establish(peer, tls);
if (!channel) { /* handle channel.error() */ }

const char hello[] = "hello";
channel->send(mqtls::as_bytes(mqtls::span<const char>(hello, sizeof(hello) - 1)));
auto reply = channel->recv(std::chrono::seconds(5));
```

Connection setup, end to end:

```
Unit 100                                      Unit 101
create /tls_msgQ_id_100_tx  (write)           create /tls_msgQ_id_101_tx  (write)
poll until /tls_msgQ_id_101_tx opens (read)   poll until /tls_msgQ_id_100_tx opens (read)
TLS server (lower unit ID)  <── TLS 1.3 mTLS ──>   TLS client
verify peer cert == unit-101                  verify peer cert == unit-100
             └──── send() / recv() start after a successful handshake ────┘
```

Plugging in your own transport means implementing `ByteTransport` and
calling the core directly, bypassing `mqtls::mq` entirely:

```cpp
auto transport = std::make_unique<MyTransport>(/* ... */);
auto channel = mqtls::SecureChannel::create(std::move(transport), mqtls::Role::client,
                                             "peer-identity", tls_config);
```

## Features

- **Peer-to-peer, no roles to configure.** For the bundled `mqtls::mq`
  transport, the lower unit ID is always the TLS server; callers never set
  a role explicitly.
- **Whole-message API.** `send()` transmits one message; `recv()` returns
  one whole message. A 4-byte length-prefixed frame sits on top of the TLS
  byte stream so message boundaries survive any transport's own chunking.
- **Thread-safe and full-duplex.** Several threads may call `send()`,
  `recv()`, and `close()` on the same channel concurrently; messages from
  different senders are serialized, never interleaved, and one thread
  blocked in `recv()` doesn't stop another from calling `send()`.
- **Deadlines everywhere.** Every blocking call takes a timeout; `close()`
  wakes every thread currently blocked in `send()`/`recv()` immediately.
- **Secure defaults.** TLS 1.3 only, `TLS_AES_256_GCM_SHA384`, P-384,
  ECDSA-SHA384, mandatory mutual authentication, no renegotiation, session
  tickets, or compression.
- **No exceptions.** Fallible operations return `mqtls::Result<T>`
  (`std::error_code`-based); the library never throws its own errors.
- **Modern, well-tested codebase.** CMake presets, GoogleTest unit and
  integration tests, coverage reports, cppcheck, clang-tidy, clang-format,
  and ASan/UBSan/TSan builds — see [Building](#building) below.
- **Fully documented.** Every public and internal header carries Doxygen
  comments; `cmake --build --preset debug --target docs` builds the HTML
  API reference, and `docs-check` fails the build on any undocumented
  symbol.

## Non-goals (v1)

- Multiple simultaneous peers on one channel. The model is strictly
  peer-to-peer: one `SecureChannel` talks to exactly one peer.
- Platforms other than Linux (the `mqtls::mq` transport specifically relies
  on `mqd_t` being a pollable file descriptor, a Linux extension).
- TLS 1.2, PSK, session resumption, or 0-RTT.

## Requirements

- Linux with POSIX message queue support (`/dev/mqueue`) — only needed for
  the `mqtls::mq` transport; the core itself has no such requirement.
- A C++17 compiler (GCC ≥ 11 or Clang ≥ 14)
- CMake ≥ 3.25
- OpenSSL ≥ 3.0

## Building

The project uses [CMake presets](CMakePresets.json); each configures into
its own `build/<preset>` directory.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

| Preset       | Purpose                                                                 |
|--------------|--------------------------------------------------------------------------|
| `debug`      | Default development build.                                               |
| `release`    | Optimized build.                                                          |
| `asan`       | AddressSanitizer + UndefinedBehaviorSanitizer.                           |
| `tsan`       | ThreadSanitizer, for SecureChannel's concurrency guarantees.             |
| `coverage`   | Instrumented build; `--target coverage` runs the tests and writes an HTML/XML gcovr report under `build/coverage/coverage/`. |
| `tidy`       | Builds with clang-tidy enabled on `mqtls` and `mqtls_mq`.                 |
| `core-only`  | Builds `mqtls` alone, with `mqtls_mq` disabled — proves the core has no mqueue dependency. |

Run every check in one go (format, cppcheck, tidy, docs, every preset's tests,
coverage, and an install/`find_package` smoke test):

```sh
./scripts/run_checks.sh
```

Useful CMake options (`-D<option>=<value>` with any preset):

| Option                      | Default                 | Effect                                     |
|------------------------------|--------------------------|---------------------------------------------|
| `BUILD_SHARED_LIBS`          | `OFF`                    | Build `mqtls`/`mqtls_mq` as shared libraries. |
| `MQTLS_BUILD_TESTS`          | `ON` (top-level build)   | Build `tests/`.                              |
| `MQTLS_BUILD_EXAMPLES`       | `ON` (top-level build)   | Build `examples/`.                           |
| `MQTLS_BUILD_DOCS`           | `ON` if Doxygen is found | Enable the `docs` / `docs-check` targets.    |
| `MQTLS_BUILD_MQ_TRANSPORT`   | `ON`                     | Build `mqtls::mq`, the message-queue transport. |
| `MQTLS_ENABLE_CLANG_TIDY`    | `OFF`                    | Run clang-tidy as part of the build.         |
| `MQTLS_ENABLE_COVERAGE`      | `OFF`                    | Build with `--coverage` instrumentation.     |
| `MQTLS_SANITIZER`            | `""`                     | e.g. `address;undefined` or `thread`.        |
| `MQTLS_WARNINGS_AS_ERRORS`   | `OFF`                    | Treat compiler warnings as errors.           |

`cppcheck` is run as part of `scripts/run_checks.sh` and CI. Install it with
your system package manager to run the full local check script.

```sh
# HTML API reference (requires Doxygen)
cmake --build --preset debug --target docs
# same inputs, but fails the build on any undocumented symbol or broken reference
cmake --build --preset debug --target docs-check
```

### Installing and consuming the library

```sh
cmake --install build/release --prefix /path/to/prefix
```

```cmake
find_package(mqtls CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE mqtls::mqtls)   # core only
target_link_libraries(your_target PRIVATE mqtls::mq)      # + the mqueue transport
```

## Testing

- **Unit tests** (`tests/unit/`) cover the core (error handling, framing,
  the TLS context and BIO bridge, `SecureChannel` driven over an in-memory
  fake transport) independently of any real transport, plus `mqtls::mq`'s
  own pieces (`MessageQueue`, `QueuePair`, config/naming).
- **Integration tests** (`tests/integration/`) exercise the whole stack
  over real POSIX message queues: handshake success and failure scenarios
  (wrong CA, expired/wrong-identity certificates, EKU/sigalg mismatches),
  send/recv edge cases, concurrency, and resource cleanup.
- Test certificates are generated **programmatically** at test time
  (`tests/support/test_pki.hpp`, OpenSSL's EVP/X509 builder API) — no
  checked-in key material, no shelling out to the `openssl` CLI.

```sh
ctest --preset debug             # everything
ctest --preset tsan              # the same tests, under ThreadSanitizer
cmake --build --preset coverage --target coverage
```

## Examples

See [`examples/`](examples/) for `echo_peer` (request/response), `chat_peer`
(a two-way terminal chat), and `multithread_peer` (several threads sending
concurrently on one channel). `scripts/gen_certs.sh` generates the test
certificates the examples need; `scripts/cleanup_queues.sh` removes any
message queues a killed example process left behind.

## Repository layout

```
include/mqtls/   public headers — mqtls.hpp (core) and mq.hpp (the mqueue transport)
src/             library implementation (src/mq/ is the mqueue transport; the rest is core)
tests/           unit tests (core + mqtls::mq) and integration tests (real queues, end to end)
examples/        echo_peer, chat_peer, multithread_peer
cmake/           CMake helper modules (warnings, sanitizers, coverage, clang-tidy, Doxygen)
scripts/         certificate generation, queue cleanup, run_checks.sh
docs/            architecture, threading, and security documentation; generated API reference
```

## License

MIT — see [LICENSE](LICENSE).
