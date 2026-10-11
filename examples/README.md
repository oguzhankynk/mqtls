# Examples

Build the examples from the repository root:

```sh
cmake --preset debug
cmake --build --preset debug
```

Each example takes the same five arguments:

```sh
--local-id <id> --peer-id <id> --ca <ca.pem> --cert <cert.pem> --key <key.pem>
```

Generate test certificates from the repository root:

```sh
./scripts/gen_certs.sh
```

This writes `ca.pem`, `unit-100.{pem,key}` and `unit-101.{pem,key}` into a
`certs/` subdirectory. The examples use Linux POSIX message queues and need
both processes to run on the same host.

Run the two peers in separate terminals, from the repository root. Their
local and peer IDs must be swapped, and each must use its own certificate
and private key.

## echo_peer

`echo_peer` does not read from the keyboard. It receives messages sent by
another peer and sends each message back to its sender. Starting two
`echo_peer` processes by themselves only establishes the secure channel;
they have no message to echo until something sends one.

Terminal 1:
```sh
./build/debug/examples/echo_peer --local-id 100 --peer-id 101 --ca certs/ca.pem --cert certs/unit-100.pem --key certs/unit-100.key
```

Terminal 2:
```sh
./build/debug/examples/echo_peer --local-id 101 --peer-id 100 --ca certs/ca.pem --cert certs/unit-101.pem --key certs/unit-101.key
```

After the handshake, both terminals print a `connected` line and wait. When
a message arrives, the receiving process prints output like:

```text
echo_peer: received 5 bytes, echoing back
```

To type messages interactively and see them on the other side, use
`chat_peer` below instead.

## chat_peer

`chat_peer` is the interactive example. Start one copy in each terminal:

Terminal 1:
```sh
./build/debug/examples/chat_peer --local-id 100 --peer-id 101 --ca certs/ca.pem --cert certs/unit-100.pem --key certs/unit-100.key
```

Terminal 2:
```sh
./build/debug/examples/chat_peer --local-id 101 --peer-id 100 --ca certs/ca.pem --cert certs/unit-101.pem --key certs/unit-101.key
```

Once connected, type a line and press Enter. It is sent to the peer and
printed there. For example, after typing `hello` in Terminal 1:

```text
# Terminal 1
chat_peer: connected. Type a line and press enter; Ctrl-D to quit.
you> hello
you>

# Terminal 2
chat_peer: connected. Type a line and press enter; Ctrl-D to quit.
you>
peer> hello
you>
```

The receive thread prints incoming messages while the main thread accepts
keyboard input. Press Ctrl-D to exit; use Ctrl-C if you need to stop either
process.

## multithread_peer

Several threads call `send()` on the same channel concurrently (never
interleaved — each message arrives whole) while a separate thread receives
everything the peer sends.

Terminal 1:
```sh
./build/debug/examples/multithread_peer --local-id 100 --peer-id 101 --ca certs/ca.pem --cert certs/unit-100.pem --key certs/unit-100.key
```

Terminal 2:
```sh
./build/debug/examples/multithread_peer --local-id 101 --peer-id 100 --ca certs/ca.pem --cert certs/unit-101.pem --key certs/unit-101.key
```

Each process sends messages from several threads and reports the messages
received from its peer, for example:

```text
multithread_peer: received 1/100 (18 bytes)
multithread_peer: received 2/100 (18 bytes)
...
multithread_peer: closed
```

## Cleaning up

If an example is killed without reaching its own `close()`/destructor (e.g.
`kill -9`), its own queue can be left behind under `/dev/mqueue`. From the
repository root, remove any leftover `tls_msgQ_id_*` queues with:

```sh
./scripts/cleanup_queues.sh
```
