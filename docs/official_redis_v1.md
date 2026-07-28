# `official/redis` v1 — Bounded RESP2 Client RFC

Status: **RESP2 codec and one serial plaintext TCP client implemented and
deterministically qualified; convenience APIs and package release remain open**.

## 1. Role and placement

`official/redis` will be an optional pure-Toka asynchronous Redis client with
the package identity and import path `official/redis`. It depends on exported
`std`/`stdx` APIs only. It introduces no C dependency, no compiler hook, and no
reverse dependency from `std` or `stdx`.

The package fills a real production gap: cache, session, rate-limit, and
lightweight queue workloads. It is deliberately a Redis protocol client, not a
cache framework or distributed-systems abstraction.

## 2. v1 target public surface

```toka
import official/redis::{RedisClient, RedisCommand, RedisValue}

auto client# = RedisClient::connect_async(cede addr, 5000).await!
auto command# = RedisCommand::new("GET")
command#.arg_text("session:42")
auto reply = client#.execute_async(cede command).await!
```

The complete v1 package is intended to provide:

- `RedisClient::connect_async`, `close`, and one serial `execute_async` path;
- `RedisCommand::new`, `arg_text`, and `arg_bytes`; binary arguments are owned
  `Bytes`, never nul-terminated C strings;
- `RedisValue::{SimpleString, Error, Integer, Bulk, Array, Null}`;
- small convenience operations `get`, `set`, and `del`, implemented only as
  wrappers over `execute_async`;
- `RedisError` carrying an error class, byte position where relevant, and an
  owned message.

The implemented slices export `RedisClient`, `RedisCommand`, `RedisArgument`,
`RedisValue`, `RedisDecode`, `RedisError`, and `decode_one`. `RedisClient`
opens one plaintext TCP connection and accepts one command at a time. It keeps
an owned receive buffer, retries `decode_one` after appending more bytes when
it returns `NeedMore`, and closes itself after a write-side or reply-side
failure. The small `get`, `set`, and `del` wrappers remain deferred; no
unimplemented convenience API is part of the current contract.

`RedisValue::Error` represents a valid Redis `-ERR` reply. Transport failures,
limits, malformed frames, timeout, and cancellation are `RedisError` results.

## 3. Protocol and safety boundary

v1 implements RESP2 only. It accepts fragmented input and retains an owned
receive `Vec<u8>` across `read_into_async` calls. Parsed bulk data becomes
owned `Bytes`; the decoder never returns a view into its mutable receive
buffer. Command encoding creates one owned wire buffer and submits it through
`write_from_async`.

Resource limits are part of the API contract:

- maximum line length: 64 KiB;
- maximum bulk string: 64 MiB;
- maximum aggregate depth: 64;
- maximum aggregate item count: 1,000,000.

One client permits one in-flight command. Timeout or cancellation after a
command may have been written poisons and closes the connection: the client
must not reuse a stream whose next reply could belong to an abandoned command.
There are no automatic retries.

## 4. Explicit non-goals

RESP3, Pub/Sub, MONITOR, blocking commands, transactions, Lua scripts,
pipelining, cluster, Sentinel, automatic reconnect, connection pooling, and
TLS connection setup are outside this first release. TLS may be added later as
an explicit `stdx/net/tls` integration; plaintext must never silently downgrade
from a requested TLS connection.

## 5. Implementation slices

1. **Codec** — encode `RedisCommand`, decode complete RESP2 values from an
   owned byte buffer, and return `NeedMore` without copying or losing bytes.
2. **Stream integration** — complete: connect, write one command,
   incrementally decode one reply, and enforce close-on-poison semantics.
3. **Qualification** — codec and deterministic TCP mock-server tests cover
   fragmented frames, binary bulk payloads, nested arrays, malformed lengths,
   EOF, and timeout. A task cancellation probe and optional real Redis
   integration remain future evidence, not release claims.
4. **Package release** — extend the existing `package.tk`, `AI_CONTRACT`,
   import smoke, lock/offline evidence, and scope-aligned README once the
   stream slice is green.

## 6. Canonical fixture corpus

The implementation qualification must use these wire examples before adding
convenience commands:

| Case | Wire bytes | Expected result |
|---|---|---|
| simple string | `+OK\r\n` | `SimpleString("OK")` |
| integer | `:42\r\n` | `Integer(42)` |
| binary bulk | `$3\r\na\x00b\r\n` | `Bulk(Bytes[61 00 62])` |
| null bulk | `$-1\r\n` | `Null` |
| nested array | `*2\r\n:1\r\n*1\r\n+OK\r\n` | nested `Array` |
| server error | `-ERR denied\r\n` | `RedisValue::Error("ERR denied")` |
| fragmented bulk | `$5\r\nhel` then `lo\r\n` | one `Bulk("hello")` |
| malformed length | `$x\r\n` | `RedisError::Protocol` |

The mock server must also split CRLF across reads and close mid-frame. These
are correctness cases, not benchmark-only cases.
