# Security {#security_page}

> This document describes the library's security design decisions from a
> threat-model perspective. For concrete parameter values,
> `include/mqtls/config.hpp` and `include/mqtls/mq/config.hpp` are the single
> source of truth.

## 1. Threat model

### 1.1 POSIX mqueue permissions are NOT an authentication mechanism

The `mqtls::mq` module specifies a file mode (`mode_t`, default `0600`) when
creating queues. This mode controls **only which local user/group on the
same machine can access the queue at the filesystem level** (POSIX mqueues
appear as virtual files under `/dev/mqueue`). This mechanism:

- **Does not verify who wrote to the queue** — any local process with write
  access to the queue can produce every byte the other side expects.
- Provides no protection over the network, since POSIX mqueue already only
  works **within the same host**.
- Carries no notion of a "peer": the queue name (`/tls_msgQ_id_<id>_tx`)
  encodes a unit ID, but doesn't prove who created that unit ID.

**Real authentication happens entirely at the TLS layer, via certificates.**
`mqtls::SecureChannel::create()`, in both roles, verifies the peer's
certificate against both the CA chain and an expected name
(`expected_peer_name`, via `X509_VERIFY_PARAM_set1_host`, matching SAN DNS
with a CN fallback). mqueue permissions are only a local noise/accident
prevention layer (e.g. another user accidentally opening a queue with the
same name); the security boundary is **the certificates**, not the file
mode. This means running without `ca_file`/`cert_file`/`key_file`, or with a
weakened `TlsConfig`, disables authentication regardless of mqueue
permissions.

### 1.2 Assumption: same host, untrusted peer processes

The threat model is this: two processes running on the same host, connected
via mqueue, each want to **cryptographically** verify the other's identity
(e.g. one process may be compromised or misconfigured). The security mqtls
provides is a layer that is **independent of, and goes beyond**, whatever
access control the transport itself (mqueue, UART, ...) provides.

## 2. TLS profile

| Parameter | Value | Why |
|---|---|---|
| Protocol | TLS 1.3 only (`min_proto_version = max_proto_version = TLS1_3_VERSION`) | TLS 1.2 and below's known design weaknesses (RSA key exchange, CBC padding oracles, weak renegotiation) are architecturally removed in TLS 1.3; v1's scope has no fallback to older protocols |
| Ciphersuite | `TLS_AES_256_GCM_SHA384` | A single, fixed, AEAD cipher suite — cipher suite negotiation is itself an attack surface; pinning it removes that |
| Key exchange group | `P-384` | The NIST P-384 curve corresponds to a 256-bit symmetric security level; a consistent security budget with AES-256 |
| Signature algorithm | `ECDSA+SHA384` | A signature scheme matching P-384; used for both the certificate and `CertificateVerify` signatures |
| mTLS | **bidirectional, mandatory** (`SSL_VERIFY_PEER` plus, in the server role, `SSL_VERIFY_FAIL_IF_NO_PEER_CERT`) | One-directional TLS only verifies the server; here both ends are "units", so authentication is required in both directions |

### Why ticket/renegotiation/compression are disabled

- **Session tickets (`SSL_OP_NO_TICKET`)**: Session resumption opens a fast
  path that skips the full mTLS verification done on the initial handshake,
  and managing/rotating the ticket key is itself extra attack surface.
  v1's scope already excludes session resumption; leaving tickets enabled
  has no benefit, only risk.
- **Renegotiation (`SSL_OP_NO_RENEGOTIATION`)**: TLS 1.3 has already removed
  classic renegotiation at the protocol level; this flag is an extra
  defense-in-depth layer against possible implementation leaks in OpenSSL.
- **Compression (`SSL_OP_NO_COMPRESSION`)**: TLS compression leaves the door
  open to CRIME-like side-channel attacks; the length of compressed text can
  leak information about encrypted content. It is already removed in TLS
  1.3; the flag is set anyway as a defense-in-depth layer.

## 3. Certificate lifecycle and identity scheme

### The mechanism is universal in the core; the identity scheme is specific to the transport

`SecureChannel::create()` takes a `std::string expected_peer_name` and uses
it for verification — **it has no idea where it came from.** The question of
how this name is produced is entirely the transport module's decision.
Since this is mTLS, and a unit's role (server/client) can vary depending on
the peer, certificates must carry **both `serverAuth` and `clientAuth`** as
EKU so they can be used in either direction — a convention followed by the
mq module, but recommended for any transport in general.

### The mq module's identity scheme (an example, mq-specific)

`mqtls::mq` combines `MqPeerConfig::peer_name_prefix` (default `"unit-"`)
with `peer_unit_id` via `mq::expected_peer_name()` to produce a name of the
form `"unit-<id>"`; this name must be embedded in the certificate as a SAN
DNS entry (with a CN fallback). For example, unit 101's certificate is
expected to carry `SAN = DNS:unit-101`, `CN = unit-101`.

**This is an mq-specific choice, not a requirement of the core.** Another
transport module could use an entirely different identity scheme: for
example, a UART connector could use a fixed device serial number
(`"device-SN12345"`), a CAN connector could use a node address
(`"can-node-7"`), or simply a single, statically pre-shared name. The core
already supports this variety by taking `expected_peer_name` as a plain
`std::string`.

### Certificate issuance and distribution

Certificate issuance/distribution is out of scope for v1:
`scripts/gen_certs.sh` produces a development/test CA plus unit certificates
(see `tests/support/test_pki.hpp` for programmatic generation). In
production, the certificate lifecycle (issuance, rotation, revocation) is
the responsibility of the system using the library; mqtls is not a CA or a
PKI management tool, it only uses whatever `ca_file`/`cert_file`/`key_file`
it is given.

## 4. Clock trust

Certificate validity checks (`notBefore`/`notAfter`) rely, inside OpenSSL,
on the system clock based on `CLOCK_REALTIME`. This is outside mqtls's
control, and has these consequences:

- If the system clock is set backward, a certificate that is not yet valid
  (`not_yet_valid`) or has already expired may appear valid.
- If the system clock is set forward, a certificate that is still valid may
  be rejected as expired (`expired`).
- On systems running without a trustworthy time source (NTP synchronization,
  a hardware RTC, a secure-boot timestamp, etc.), certificate validity
  checking **can stop providing a meaningful security guarantee.**

**For this reason: certificate validity checking must not be disabled or
weakened in the absence of a trustworthy time source.** If the clock is
untrustworthy, the correct fix is not to turn off certificate verification
but to fix the underlying time source (NTP/chrony, hardware RTC
maintenance, etc.); skipping the `notBefore`/`notAfter` check completely
invalidates mTLS's authentication promise (an attacker could impersonate an
identity using an expired/revoked certificate).

## 5. DoS limits

### `max_message_size` (core, `ChannelOptions`)

After reading each message's 4-byte length field in the application-level
framing, `SecureChannel` compares the declared length against
`ChannelOptions::max_message_size` (default 1 MiB). A value that exceeds it
is rejected with `errc::message_too_large` without allocating memory for
it, and the channel is closed. This prevents a peer (or a corrupted/
malicious sender) from forcing the receiver into unbounded memory
allocation by declaring an arbitrarily large length field (the classic
length-prefix DoS).

### mqueue queue limits (`msg_max` / `msgsize_max`, mq-specific)

`MqPeerConfig::max_messages` and `MqPeerConfig::message_size` are each
subject to the system limits `/proc/sys/fs/mqueue/msg_max` and
`msgsize_max` (typical defaults: `msg_max=10`, `msgsize_max=8192`, for a
non-root user). These limits create an effect that cuts both ways:

- **Protection**: a queue cannot grow without bound; if one side keeps
  writing without the other reading, the queue fills up and the writer gets
  `would_block` (backpressure, not memory exhaustion).
- **Risk**: with very small limits (e.g. `max_messages=2`), the TLS
  handshake itself (especially with a large certificate chain) can fill the
  queue. mqtls handles this because the handshake loop is non-blocking and
  `poll()`-based: when the writer gets `would_block`, it waits on
  `write_fd()`; as the peer reads, room opens up in the queue and the loop
  progresses — no deadlock occurs, only slowdown (see `docs/threading.md`
  §4).

### The stale-queue scenario (mq-specific)

A leftover, uncleaned TX queue ("stale queue") from a previous run can show
up in two ways:

1. **Your own TX is stale**: `QueuePair::establish()` tries to create its
   own queue with `O_CREAT|O_EXCL`; if it gets `EEXIST`, it treats this as a
   queue left behind by a previous run, `mq_unlink`s it, and recreates it.
   This automatic cleanup covers cases where an earlier process could not
   call `mq_unlink` while exiting (e.g. it was killed with SIGKILL).
2. **The peer's TX is stale**: the other side's queue may be openable, but
   if it still holds leftover data from an old/broken handshake, the TLS
   handshake fails on that data (`errc::handshake_failed`) — this is not a
   security hole (a valid TLS session cannot be established from corrupted
   data), but it does require the caller to retry `establish()`.

In both cases the outcome is either automatic recovery or a safe failure
(`handshake_failed`/`timeout`), never a connection silently established with
the wrong peer — because final authentication always goes through TLS
certificate verification (§1.1).
