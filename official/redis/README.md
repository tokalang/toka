# `official/redis` v1

Status: **bounded RESP2 codec, serial TCP/TLS client, ordered pipelines, and bounded connection pooling**.

`official/redis` is an optional pure-Toka Redis client package. It encodes
binary-safe RESP2 commands, incrementally decodes one response from an owned
`Vec<u8>` buffer, and supports serial plaintext or TLS connections directly
or through an exclusive pool lease.

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

`connect_tls_async` verifies the peer and SNI hostname against the system
trust store. `connect_tls_with_ca_file_async` is available for private CA
deployments. `connect_insecure_tls_for_test_async` is deliberately named for
deterministic test fixtures only; it is never a fallback from verified TLS.

`RedisPipeline` batches commands into one write and returns replies in command
order. It reads exactly one RESP value per command; transport, decoder, or
reply-count failure poisons the connection rather than leaving an ambiguous
reply for the next operation.

```toka
auto pipeline# = RedisPipeline::new()
pipeline#.push(cede RedisCommand::new("PING"))
pipeline#.push(cede RedisCommand::new("INFO"))
auto replies = client#.execute_pipeline_async(cede pipeline).await!
```

`get_async`, `set_async`, and `del_async` are thin wrappers over the serial
`execute_async` primitive. `GET` returns `Ok(None)` for a missing key;
`SET` accepts an owned binary `Bytes` value; `DEL` returns Redis's deletion
count. A well-formed Redis `-ERR` reply becomes a `RedisError(kind = "server")`
for these typed operations. Use `execute_async` when the application needs the
raw `RedisValue` reply.

RESP3, Pub/Sub, cluster, Sentinel, retry policy, and Redis Cluster routing
remain outside this package slice.

## Pooling

`RedisPool` owns a bounded set of plaintext or verified-TLS clients. An
acquired `RedisLease` is the only mutable owner of one client, so it preserves
the serial request/reply contract across `.await`; dropping the lease returns a
healthy client automatically.

```toka
auto pool# = RedisPool::new(cede address, 5000, 16).unwrap()
{
    auto lease# = pool#.acquire_async(1000).await!
    lease#.get_async("session:42").await!
}
pool#.close()
```

`new_tls` and `new_tls_with_ca_file` retain the same verified-TLS choices as
the direct client. `close()` stops new leases, drains idle sockets, and causes
checked-out leases to close on return. A capacity timeout or cancellation
leaves the pool unchanged. A client is discarded rather than returned after
I/O, cancellation, decode/protocol failure, or an unread/extra pipeline reply.

## Qualification

Run from this package root:

```text
../../build/bin/tokac -I ../../lib -I lib tests/codec_v1.tk -o /tmp/redis_codec_v1 && /tmp/redis_codec_v1
../../build/bin/tokac -I ../../lib -I lib tests/client_v1.tk -o /tmp/redis_client_v1 && /tmp/redis_client_v1
../../build/bin/tokac -I ../../lib -I lib tests/transport_v2.tk -o /tmp/redis_transport_v2 && /tmp/redis_transport_v2
../../build/bin/tokac -I ../../lib -I lib tests/pool_v1.tk -o /tmp/redis_pool_v1 && /tmp/redis_pool_v1
python3 tests/qualify_package.py
```

Real-service compatibility is a separate fail-closed Docker qualification. It
verifies Redis 7.4.x and 8.2.x with password TCP and private-CA TLS; a
successful local Docker run is maintainer evidence, while Linux CI records the
release-gate artifact. A runner that cannot publish loopback ports is reported
as `not-run`, never as a passing package test.

```text
python3 tools/scripts/qualify_data_access_real.py --tokac build/bin/tokac --report build/data-access-real-service.json
```

Run that command from the repository root. The exact scope and artifact policy
are documented in [`docs/data_access_real_service_compatibility_v1.md`](../../docs/data_access_real_service_compatibility_v1.md).
