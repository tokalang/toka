# `official/redis` v1

Status: **RESP2 codec and serial plaintext TCP client slices**.

`official/redis` is an optional pure-Toka Redis client package. It encodes
binary-safe RESP2 commands, incrementally decodes one response from an owned
`Vec<u8>` buffer, and supports one serial plaintext TCP connection.

```toka
import official/redis::{RedisClient, RedisCommand}

auto client# = RedisClient::connect_async(cede address, 5000).await!
auto command# = RedisCommand::new("GET")
command#.arg_text("session:42")
auto reply = client#.execute_async(cede command).await!
```

The codec returns `RedisDecode::NeedMore()` without consuming an incomplete
frame. A completed bulk string is returned as owned `Bytes`, never a view into
the mutable receive buffer. Any I/O failure, EOF, timeout, cancellation, or
malformed reply after a command write closes the client; it cannot be reused.
Extra reply bytes are also rejected: a serial client must not assign an
unsolicited reply to a later command.

RESP3, TLS, Pub/Sub, pipelines, cluster, Sentinel, retries, pooling, and
convenience command wrappers remain outside this package slice.

## Qualification

Run from this package root:

```text
../../build/bin/tokac -I ../../lib -I lib tests/codec_v1.tk -o /tmp/redis_codec_v1 && /tmp/redis_codec_v1
../../build/bin/tokac -I ../../lib -I lib tests/client_v1.tk -o /tmp/redis_client_v1 && /tmp/redis_client_v1
```
