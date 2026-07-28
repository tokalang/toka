# `official/redis` v1

Status: **bounded RESP2 codec and serial plaintext TCP client v1**.

`official/redis` is an optional pure-Toka Redis client package. It encodes
binary-safe RESP2 commands, incrementally decodes one response from an owned
`Vec<u8>` buffer, and supports one serial plaintext TCP connection.

```toka
import official/redis::{RedisClient, RedisCommand}

auto client# = RedisClient::connect_async(cede address, 5000).await!
auto value = client#.get_async("session:42").await!
```

The codec returns `RedisDecode::NeedMore()` without consuming an incomplete
frame. A completed bulk string is returned as owned `Bytes`, never a view into
the mutable receive buffer. Any I/O failure, EOF, timeout, cancellation, or
malformed reply after a command write closes the client; it cannot be reused.
Extra reply bytes observed with the matched reply are rejected: a serial
client must not assign an unsolicited buffered reply to a later command.

`get_async`, `set_async`, and `del_async` are thin wrappers over the serial
`execute_async` primitive. `GET` returns `Ok(None)` for a missing key;
`SET` accepts an owned binary `Bytes` value; `DEL` returns Redis's deletion
count. A well-formed Redis `-ERR` reply becomes a `RedisError(kind = "server")`
for these typed operations. Use `execute_async` when the application needs the
raw `RedisValue` reply.

RESP3, TLS, Pub/Sub, pipelines, cluster, Sentinel, retries, and pooling remain
outside this package slice.

## Qualification

Run from this package root:

```text
../../build/bin/tokac -I ../../lib -I lib tests/codec_v1.tk -o /tmp/redis_codec_v1 && /tmp/redis_codec_v1
../../build/bin/tokac -I ../../lib -I lib tests/client_v1.tk -o /tmp/redis_client_v1 && /tmp/redis_client_v1
python3 tests/qualify_package.py
```
