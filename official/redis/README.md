# `official/redis` v1

Status: **RESP2 codec slice**.

`official/redis` is an optional pure-Toka Redis client package. The first
vertical slice implements the binary-safe RESP2 codec only: it encodes commands
and incrementally decodes one response from an owned `Vec<u8>` buffer. It does
not open sockets yet.

```toka
import official/redis::{RedisCommand, decode_one}

auto command# = RedisCommand::new("GET")
command#.arg_text("session:42")
auto wire = command#.into_wire()!
```

The codec returns `RedisDecode::NeedMore()` without consuming or copying an
incomplete frame. A completed bulk string is returned as owned `Bytes`, never a
view into the mutable receive buffer.

The next slice adds one serial async connection above this codec. RESP3,
Pub/Sub, pipelines, cluster, Sentinel, retries, and pooling remain outside
this package slice.

## Qualification

Run from this package root:

```text
../../build/bin/tokac -I ../../lib -I lib tests/codec_v1.tk -o /tmp/redis_codec_v1 && /tmp/redis_codec_v1
```
