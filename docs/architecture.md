# Architecture {#architecture_page}

> This document describes the library's layer architecture, the connection
> setup flow, and the path to follow when adding a new transport. The exact
> contract of behavior is the Doxygen comments in the relevant header files.

## 1. The essence of the design: the core knows nothing about the transport

The sole purpose of the `mqtls` library is not "TLS over POSIX mqueue". The
goal is for a TLS 1.3 layer to sit on top of **any physical/logical protocol
that carries bytes** (POSIX mqueue, UART, shared memory, CAN, ...) without
changing a single line of the core. To achieve this, the library is split
into two separate CMake targets:

- **`mqtls` (the core, `namespace mqtls`)** — completely unaware of the
  transport. Its one connection point is the **public** abstract interface
  `mqtls::transport::ByteTransport`. No file in the core references
  `<mqueue.h>` or any `mq_*` POSIX call — this isn't just a convention, it's a
  fact proven at build level: with `MQTLS_BUILD_MQ_TRANSPORT=OFF`, the
  `mqtls` target builds on its own, without depending on the `rt` library.
- **`mqtls_mq` (the mq module, `namespace mqtls::mq`)** — the **first and
  currently only** concrete transport binding, filling `ByteTransport` with
  POSIX mqueue. It is not part of the core; it links `mqtls::mqtls` PUBLIC
  and `rt` PRIVATE.

This separation is deliberate: a future `mqtls::uart`, `mqtls::shm`, or
`mqtls::can` module could be added by doing for its own transport exactly
what `mqtls_mq` does today — **without a single line of the core (`mqtls`)
changing.** Reading "mqtls = just a msgQ library" is wrong: the `mqtls`
namespace and target know nothing about msgQ at all; all msgQ knowledge
lives entirely in the `mqtls::mq` namespace and the `mqtls_mq` target.

## 2. Layer diagram

```
┌──────────────────────────────────────────────────────────────────────┐
│ CORE — namespace mqtls, CMake target `mqtls`  (transport-independent) │
│                                                                        │
│  PUBLIC API                                                           │
│  ┌──────────────────────────────────────────────────────────────┐    │
│  │ mqtls::SecureChannel                                          │    │
│  │  - framing (4B BE length + payload), three locks,             │    │
│  │    send/recv/close                                            │    │
│  │  - single construction path:                                  │    │
│  │      SecureChannel::create(unique_ptr<ByteTransport>, Role,   │    │
│  │                            expected_peer_name, TlsConfig,     │    │
│  │                            ChannelOptions)                    │    │
│  │    NEVER carries information about "how the connection was   │    │
│  │    set up"                                                    │    │
│  └──────────────────────────────────────────────────────────────┘    │
│                              │ uses                                   │
│  ┌──────────────────────────────────────────────────────────────┐    │
│  │ INTERNAL — mqtls::tls (src/tls/, not public)                  │    │
│  │  tls::Context  : SSL_CTX RAII, the security profile           │    │
│  │  tls::Bio      : a custom BIO_METHOD for SSL*, binds          │    │
│  │                  ByteTransport to OpenSSL (the BIO does NOT   │    │
│  │                  own the transport)                           │    │
│  └──────────────────────────────────────────────────────────────┘    │
│                              │ talks to                               │
│  ┌──────────────────────────────────────────────────────────────┐    │
│  │ PUBLIC SEAM — mqtls::transport (include/mqtls/transport.hpp)  │    │
│  │  class ByteTransport (abstract):                              │    │
│  │    write_some / read_some   (non-blocking)                    │    │
│  │    read_fd() / write_fd()   (for poll(2))                     │    │
│  │    pending_read()                                              │    │
│  │  ── ANYTHING IMPLEMENTING THIS INTERFACE CAN PLUG INTO THE CORE│    │
│  └──────────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────┬───────────────────────────────┘
                                        │ implements
          ┌─────────────────────────────┼─────────────────────┬─────────────┐
┌─────────────────────┐       ┌──────────────────────┐ ┌───────────────┐ ┌─────────┐
│ mqtls::mq (v1, ships)│       │ tests/support:         │ │ mqtls::uart     │ │ ...CAN, │
│ CMake target mqtls_mq│       │ FakeTransport          │ │ (doesn't exist  │ │ shm etc.│
│ (POSIX mqueue)       │       │ (socketpair, test only)│ │  — example only)│ │ (none)  │
└─────────────────────┘       └──────────────────────┘ └───────────────┘ └─────────┘
```

`mqtls::mq` is itself layered too — nothing mq-specific leaks into the core,
but the mq module has its own internal/public split:

```
mqtls::mq  (CMake target mqtls_mq → mqtls::mqtls PUBLIC + rt PRIVATE)
│
│ PUBLIC (include/mqtls/mq.hpp, include/mqtls/mq/*.hpp)
│  mq::establish(MqPeerConfig, TlsConfig, ChannelOptions) -> Result<SecureChannel>
│     the "bridge to the core": sets up the connection, then calls
│     SecureChannel::create()
│  mq::MqPeerConfig, tx_queue_name()/rx_queue_name()/tls_role()/expected_peer_name()
│
│ INTERNAL (src/mq/, not public)
│  mq::QueuePair    : implements transport::ByteTransport
│                     (the tx/rx POSIX mqueue pair, byte-stream semantics,
│                      chunking)
│  mq::MessageQueue : an mq_open/mq_close/mq_unlink/mq_send/mq_receive RAII
│                     wrapper
```

## 3. Each layer, one by one

### `mqtls::transport::ByteTransport` — public seam (core)

The core's only way of talking to the transport. A pure virtual interface,
carrying no transport-specific assumption whatsoever:

- `write_some(mqtls::span<const std::byte>) -> Result<std::size_t>` —
  non-blocking; a partial write is a valid result.
- `read_some(mqtls::span<std::byte>) -> Result<std::size_t>` — non-blocking;
  if the transport side is message/frame-based (as with mq), it buffers any
  RX excess internally.
- `read_fd() const -> int`, `write_fd() const -> int` — descriptors that can
  be waited on with `poll(2)`. In mq, both are the same `mqd_t`'s fd; on
  another transport they may differ (e.g. separate read/write sockets).
- `pending_read() const -> std::size_t` — the number of bytes waiting in the
  internal buffer.

Nothing outside this interface is public, and nothing enters the core's
construction logic: `ByteTransport` is handed to `SecureChannel::create()`
already as an connected/ready object.

### `mqtls::tls::Context` / `mqtls::tls::Bio` — internal (core)

- `tls::Context::make(const TlsConfig&, Role) -> Result<Context>`: an
  `SSL_CTX*` RAII wrapper. Applies the security profile (see
  `docs/security.md`): TLS 1.3 only, `TLS_AES_256_GCM_SHA384`, `P-384`,
  `ECDSA+SHA384`, mutual certificate verification (`SSL_VERIFY_PEER` +
  `SSL_VERIFY_FAIL_IF_NO_PEER_CERT` in the server role), tickets/
  renegotiation/compression disabled. It knows nothing about peer identity
  (unit ID, serial number, ...) — that information is never given to
  `Context`.
- `tls::Bio`: a custom `BIO_METHOD` binding an `SSL*` to a `ByteTransport&`
  (registered once via `std::call_once`). A single `BIO` is used for both
  reading and writing (`SSL_set_bio(ssl, bio, bio)` + `BIO_up_ref`). The
  `BIO` does **not own** the transport — the transport's lifetime is managed
  by `SecureChannel::Impl`.

### `mqtls::SecureChannel` — public, core, framing + thread-safety

The library's only user-visible public class. Move-only, pimpl
(`std::unique_ptr<Impl>`). Its responsibilities:

1. **Construction**: `create(transport, role, expected_peer_name, tls_config,
   options)` — `tls::Context::make()` → `SSL*` + `tls::Bio(*transport)` →
   `SSL_set_accept_state()`/`SSL_set_connect_state()` depending on role →
   `X509_VERIFY_PARAM_set1_host(expected_peer_name)` in **both roles** → a
   non-blocking handshake loop (with the `handshake_timeout` deadline).
2. **Framing**: application-level framing on top of the TLS record — a
   4-byte big-endian length plus payload. `recv()` always returns one whole
   application message; exceeding `max_message_size` yields
   `errc::message_too_large` and closes the channel.
3. **Thread-safety**: three locks (`ssl_mtx_`, `send_mtx_`, `recv_mtx_`) and
   a wait/wake mechanism (`WakeFd` + `wait_for`). Details in
   `docs/threading.md`.
4. **Shutdown**: `close()` is idempotent, performs a best-effort
   `SSL_shutdown` (close_notify), then the transport's `unique_ptr` is
   destroyed — any transport-specific close/unlink logic runs in that
   transport's own destructor; `SecureChannel` never knows about it.

`SecureChannel` **never knows in any way** which transport it came from, or
how the connection was established (which queue name, which serial port,
...). All it has is: a connected `ByteTransport`, a `Role`, an expected peer
name (a plain `std::string`), and a `TlsConfig`.

### `mqtls::mq::QueuePair` / `mqtls::mq::MessageQueue` — internal (mq module)

- `MessageQueue`: move-only RAII around a single POSIX mqueue — `mq_open`,
  `mq_close`, `mq_unlink`, `try_send`/`try_receive` (EAGAIN →
  `std::errc::resource_unavailable_try_again`), `native_fd()`,
  `message_size()`.
- `QueuePair : transport::ByteTransport` — a pair formed from two
  `MessageQueue`s (its own TX plus the peer's TX, used as RX); implements
  `ByteTransport::write_some` by splitting into `message_size` chunks, and
  `read_some` via an internal RX buffer. This class covers every mq-specific
  detail (queue naming, chunking, stale-queue cleanup) and appears to the
  core as **a perfectly ordinary `ByteTransport`**.

### `mqtls::mq::establish()` — public, mq module, bridge to the core

```text
mq::establish(MqPeerConfig cfg, TlsConfig tls_cfg, ChannelOptions opts = {})
    -> Result<SecureChannel>
```

This function is the mq module's **only** point of contact with the core:

1. Computes the `Role` via `mq::tls_role(cfg)` (the lower unit ID is the
   server).
2. Establishes the connection via `mq::QueuePair::establish(cfg, ...)` (the
   flow in §4).
3. Produces the expected peer name via `mq::expected_peer_name(cfg)`
   (`"unit-<id>"`).
4. Calls `SecureChannel::create(std::move(queue_pair), role,
   expected_peer_name, tls_cfg, opts)` — past this line there is no
   mq-specific information left at all.

Outside `mq::establish()`, the mq module contains no TLS/SSL knowledge;
`channel.cpp` only knows `SecureChannel::create()`'s signature.

## 4. Sequence diagrams

### 4.1 Handshake setup (using mq as the example — which steps are mq-specific vs. core)

```
Unit 100 (mq::establish, server)            Unit 101 (mq::establish, client)
──────────────────────────────              ──────────────────────────────
┌─ MQ-SPECIFIC (QueuePair::establish) ───────────────────────────────────┐
│ validate(MqPeerConfig)                     validate(MqPeerConfig)      │
│ create /tls_msgQ_id_100_tx (own TX)         create /tls_msgQ_id_101_tx │
│ poll: mq_open(101_tx, RDONLY)               poll: mq_open(100_tx,RDONLY)│
│   ENOENT → back off, retry                    ENOENT → back off        │
│   (cancel → channel_closed; deadline → timeout, own TX unlinked)       │
│ ... QueuePair ready: a ByteTransport ...                               │
└──────────────────────────────────────────────────────────────────────┘
                 │ Role=server, expected_peer_name="unit-101"   │ Role=client, "unit-100"
                 ▼                                              ▼
┌─ CORE (SecureChannel::create → tls::Context/Bio) ──────────────────────┐
│ SSL_set_accept_state()                     SSL_set_connect_state()    │
│ X509_VERIFY_PARAM_set1_host("unit-101")    X509_VERIFY_PARAM_set1_host│
│                                             ("unit-100")               │
│          ◄──────────────── ClientHello (over the QueuePair's TX) ─────┤
│ ServerHello, EncryptedExtensions,                                     │
│ CertificateRequest, Certificate,                                      │
│ CertificateVerify, Finished         ──────────────────────────────►   │
│          ◄──────────── Certificate, CertificateVerify, Finished ──────┤
│ Both sides verify the CA chain + SAN/CN == "unit-<peer>".              │
│   Failure → errc::certificate_verify_failed                           │
│   Other handshake errors → errc::handshake_failed                     │
└──────────────────────────────────────────────────────────────────────┘
                 │ Result<SecureChannel>                        │ Result<SecureChannel>
                 ▼                                              ▼
       the caller may call send()/recv()              the caller may call send()/recv()
```

Another transport (e.g. UART) shares none of steps 1–3, which are
mq-specific — perhaps opening a serial port is all it needs. The one thing
in common: a `ByteTransport` + `Role` + `expected_peer_name` is eventually
handed to the core, and from that point on everything (the CORE box above)
works **identically**, independent of the transport.

### 4.2 `send(msg, timeout)` (core)

```
Caller's thread                   SecureChannel::Impl                 ByteTransport
───────────────                   ────────────────────                ─────────────
send(msg)
  │ lock(send_mtx_)   ── queues up other senders
  │ header = BE32(len(msg))
  │ ┌─ loop: until header+payload are fully written ─┐
  │ │ lock(ssl_mtx_)                                  │
  │ │ SSL_write(...) ──────────────────────────────────────────► write_some()
  │ │ unlock(ssl_mtx_)                                │
  │ │ on WANT_READ/WANT_WRITE:                         │
  │ │   wait_for(read_fd()|write_fd(), wake_fd, remaining) │ ← another thread may
  │ │   (waited on WITHOUT holding ssl_mtx_)                │   call recv() meanwhile
  │ └───────────────────────────────────────────────────┘
  │ unlock(send_mtx_)
  ▼
Result<void> (ok / timeout / channel_closed / message_too_large / io_error / protocol_error)
```

### 4.3 `recv(timeout)` (core)

```
Caller's thread                   SecureChannel::Impl                 ByteTransport
───────────────                   ────────────────────                ─────────────
recv()
  │ lock(recv_mtx_)   ── guarantees a single receiver assembles one frame
  │ ┌─ until the full 4-byte header is read ─┐
  │ │ lock(ssl_mtx_) → SSL_read() → unlock(ssl_mtx_) ───────────► read_some()
  │ │ WANT_READ/WRITE → wait_for(...)    (WITHOUT holding ssl_mtx_) │
  │ └────────────────────────────────────┘
  │ len decoded from BE32; len > max_message_size → message_too_large, channel closes
  │ ┌─ until len bytes of payload are fully read (as above) ─┐
  │ └──────────────────────────────────────────────────────────┘
  │ unlock(recv_mtx_)
  ▼
Result<std::vector<std::byte>> (ok / timeout / channel_closed / peer_closed / truncated / ...)
```

`SSL_ERROR_ZERO_RETURN` (the peer sent close_notify) → `errc::peer_closed`;
EOF without close_notify → `errc::truncated`.

### 4.4 `close(timeout)` (core)

```
Any thread                         SecureChannel::Impl
────────────────                   ────────────────────
close()
  │ closing_.store(true)             (an atomic flag, for idempotency)
  │ wake_fd_.signal()        ── wakes EVERY thread currently blocked in
  │                              wait_for() (they return channel_closed,
  │                              whether blocked in recv() or send())
  │ lock(send_mtx_), lock(recv_mtx_), lock(ssl_mtx_)   ── in order, in turn
  │ best-effort SSL_shutdown()  (send close_notify, bounded by the 'timeout'
  │                               deadline)
  │ unlocks
  │ transport_.reset()        ── the unique_ptr is destroyed; any
  │                               transport-specific close/unlink logic
  │                               (e.g. mq::QueuePair's destructor) runs
  │                               here, which SecureChannel knows nothing
  │                               about.
  ▼
Result<void>  (idempotent: a second call also returns {})
```

See `docs/threading.md` for the exact lock ordering and wait-loop guarantees.

## 5. How to add a new transport

Someone wanting to add a transport other than mq (e.g. UART) follows these
three steps:

1. **Implement `transport::ByteTransport`.** Expose your physical protocol's
   non-blocking read/write and pollable fds as `write_some`, `read_some`,
   `read_fd`, `write_fd`, `pending_read`.
2. **Decide `Role` and the expected peer name with your own connection
   logic.** mq's "lower unit ID = server" rule, or its `"unit-<id>"` identity
   scheme, is specific to mq — a new transport can use entirely its own
   convention (a fixed role, a serial number, a pre-shared name, ...).
3. **Call `SecureChannel::create(transport, role, peer_name, tls_config,
   options)`.** The connection is then considered already established (the
   `ByteTransport` is ready).

```cpp
// Example: a hypothetical UART transport (not real code, a sketch)
auto serial = std::make_unique<uart::SerialLink>(port_path);
mqtls::Role role = uart::decide_role(local_id, peer_id);     // transport-specific
std::string peer_name = uart::expected_peer_name(peer_id);   // transport-specific

mqtls::Result<mqtls::SecureChannel> channel =
    mqtls::SecureChannel::create(std::move(serial), role, peer_name,
                                  tls_config, channel_options);
```

**Not a single line of the core (the `mqtls` target, the non-mq files under
`src/`) changes.** This claim isn't just a design intention — it is verified
at build level on every build via the `core-only` build preset
(`MQTLS_BUILD_MQ_TRANSPORT=OFF`): with this preset, the `mqtls` target builds
and passes its tests entirely on its own, without ever linking `mqtls_mq`,
without even needing `librt`.
