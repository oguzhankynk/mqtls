# Threading Model {#threading_page}

> The model described here **belongs entirely to the `mqtls` core**
> (`SecureChannel::Impl`, `src/`) and uses no transport-specific knowledge —
> it behaves identically no matter which `transport::ByteTransport`
> implementation runs underneath, `mqtls::mq` included.

## 1. Why this model is needed

OpenSSL's `SSL*` object is not safe under concurrent calls: two threads
calling `SSL_read`/`SSL_write` on the same `SSL*` at the same time is
undefined behavior. At the same time, `mqtls::SecureChannel` promises that
one thread can call `send()` while another is blocked in `recv()`
(full-duplex), and that multiple threads can safely call `send()`/`recv()`
concurrently (without frames getting interleaved). These two requirements
are met by three locks working together.

## 2. The three locks

| Lock | Protects | Hold duration |
|---|---|---|
| `ssl_mtx_` | **Every** call on the `SSL*` (`SSL_read`, `SSL_write`, `SSL_do_handshake`, `SSL_shutdown`, `SSL_get_verify_result`, ...) | Very short — about as long as a single OpenSSL call. **Never held while blocked waiting.** |
| `send_mtx_` | Writing **one entire** frame (4-byte header + payload) | For the whole lifetime of one `send()` call (including its loop, but `ssl_mtx_` is released during `wait_for()`) |
| `recv_mtx_` | Reading and assembling **one entire** frame | For the whole lifetime of one `recv()` call (likewise) |

`send_mtx_` and `recv_mtx_` are independent of each other: one can be
sending a frame while the other receives one — this is the foundation of
the full-duplex guarantee (§4).

## 3. Lock ordering

Fixed and one-directional:

```
send_mtx_  or  recv_mtx_   →   ssl_mtx_
```

- **No other lock is ever taken while `ssl_mtx_` is held** (this rule is what
  makes deadlock impossible).
- `send_mtx_` and `recv_mtx_` are not ordered relative to each other, because
  the two are never held at the same time: a `send()` call never requests
  `recv_mtx_`, and vice versa.
- Multiple `send()` calls (from different threads) queue up on `send_mtx_`;
  likewise, multiple `recv()` calls queue up on `recv_mtx_`.

## 4. The wait loop (`wait_for` + `WakeFd`) — the full flow

Since `SSL_write`/`SSL_read`/`SSL_do_handshake` run over a non-blocking
`BIO`, they can return `SSL_ERROR_WANT_READ` or `SSL_ERROR_WANT_WRITE`. The
flow (the same pattern for `send()`, `recv()`, and the handshake alike):

```
1. lock(ssl_mtx_)
2. SSL_write(...) / SSL_read(...) / SSL_do_handshake(...)
3. unlock(ssl_mtx_)
4. Is the result WANT_READ or WANT_WRITE?
     yes → go to step 5
     no  → exit the loop (success or a permanent error)
5. wait_for(transport.read_fd() | transport.write_fd(),  // whichever was requested
            wake_fd,
            deadline.remaining_ms())
     - ssl_mtx_ IS NOT HELD DURING THIS CALL.
     - wait_for() waits for one of three things: the fd becomes ready,
       wake_fd is signalled, or the deadline elapses.
     - if both the fd AND wake_fd become ready in the same poll(2) round,
       wake_fd takes priority (util::wait_for's contract) — this is what
       lets close() win reliably (§5).
6. based on wait_for()'s result:
     ok                   → go back to step 1, retry the SSL call
     errc::timeout        → the calling operation returns errc::timeout
     errc::channel_closed → the calling operation returns errc::channel_closed
     errc::io_error       → the calling operation returns errc::io_error
```

This loop calls **only `transport::ByteTransport::read_fd()`/`write_fd()`**;
it has no idea which transport is running underneath. When `SSL_write` is
retried, it is called again with the same buffer and the same length
(OpenSSL's non-blocking `BIO` contract requires this); that's why
`SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER` is set during `Context` construction —
so the calling code doesn't have to hand back the exact same memory on
every retry.

Releasing `ssl_mtx_` in step 5 is critical: if it were held, a thread
waiting for `SSL_write` to complete would keep `ssl_mtx_` locked, and
another thread that needs to call `SSL_read` on the same `SSL*`
(a full-duplex scenario) would block forever.

## 5. How `close()` wakes other threads

```
the thread calling close()                 threads blocked in recv()/send()
───────────────────────                    ──────────────────────────────────
closing_.store(true)   (atomic)
wake_fd_.signal()       ───────────────►  the poll(2) inside wait_for() returns
                                           (wake_fd is ready; wake_fd wins even
                                            if the fd is also ready, §4)
                                           → the SSL_write/SSL_read loop that
                                             called it exits with
                                             errc::channel_closed
                                           → send()/recv() returns
                                             errc::channel_closed, releasing
                                             its lock
lock(send_mtx_)   ◄── now free, since the threads above released their locks
lock(recv_mtx_)
lock(ssl_mtx_)
best-effort SSL_shutdown()  (close_notify, bounded by the given timeout)
release the locks
transport_.reset()
```

The `closing_` flag makes `close()` idempotent: a second call finds
`wake_fd_` already signalled and reaches the same result (`{}`).
`wake_fd_.signal()` requires no lock at all (`WakeFd::signal()` is
lock-free, `noexcept`, and never blocks) — which is what lets `close()`
force other threads to release their locks without having to wait for them.

## 6. The full-duplex guarantee

While one thread holds `recv_mtx_` and is waiting inside `recv()` on
`wait_for(read_fd(), ...)` (with `ssl_mtx_` free at that moment), another
thread can take `send_mtx_` and call `send()`: it briefly locks `ssl_mtx_`
for its own `SSL_write` call, releases it, and waits on `write_fd()` if
needed. Neither operation blocks the other, because:

- `send_mtx_` and `recv_mtx_` are independent (§3).
- `ssl_mtx_` is never held while waiting (§4).
- `wait_for()` can wait multiple threads independently on the same
  `wake_fd_`/fd pair (`poll(2)` semantics — each call waits with its own
  `pollfd` set).

## 7. Guarantees

- Multiple threads can call `SecureChannel::send()` **concurrently**: frames
  (header+payload) never interleave, each one goes out as a whole.
- Multiple threads can call `SecureChannel::recv()` **concurrently**: each
  call receives one complete, separate application message; no two calls
  share the same bytes.
- One thread may call `send()` while another is blocked in `recv()` (and
  vice versa) — full-duplex.
- `close()` can be called from any thread, and wakes **every** thread
  currently blocked in `send()`/`recv()` at that moment with
  `errc::channel_closed`.
- `close()` is idempotent; multiple threads may call `close()` concurrently.
- `~SecureChannel()` attempts a best-effort `close()` if one hasn't already
  been called.

## 8. What is NOT guaranteed

- **No ordering guarantee**: if two threads call `send()` at the same time,
  frames don't interleave, but which frame goes out on the wire first (which
  thread acquires `send_mtx_` first) is not guaranteed — it depends on the
  OS scheduler.
- **No move safety**: `SecureChannel` is move-only; moving a `SecureChannel`
  while another thread is concurrently using it (e.g. is inside `recv()`)
  is **undefined behavior** — just like every other move-only handle type.
- **No concurrent `send()`/`recv()` during the handshake**: the object isn't
  visible to the caller until `create()` completes and returns a
  `SecureChannel`, so this scenario can't actually arise — but `create()`
  itself must also be called from a single thread.
- **No success guarantee for `send()`/`recv()` after `close()`**: any
  `send()`/`recv()` call that starts (or is in progress) after `close()` has
  been called (or is being called) ends with `errc::channel_closed`; the
  channel cannot be reopened.
- **Timeout precision is not guaranteed**: `Timeout` values are bound by
  `poll(2)`'s millisecond resolution and scheduler delays; expect "at least
  N ms, in practice a bit more", not "exactly N ms".
- **`ssl_mtx_` is not fair**: since `std::mutex` is used, there is no
  fairness guarantee about which thread acquires the lock next (starvation
  is theoretically possible, though in practice the short hold time of
  frames makes it negligible).
- **One `SecureChannel` represents one connection**: managing multiple peers
  through the same object is not supported (a natural consequence of
  mqtls's P2P model).
