# `official/postgres` v1

Status: **bounded PostgreSQL v3 wire codec, ASCII-profile SCRAM-SHA-256,
secure startup client, serial queries, prepared statements, transactions, and
bounded connection pooling**.

`official/postgres` currently encodes StartupMessage and simple Query frames,
then incrementally decodes PostgreSQL backend messages without exposing a view
into its input buffer. Binary row values become owned `Bytes`; incomplete
frames return `PostgresDecode::NeedMore()`.

```toka
import official/postgres::{PostgresDecode, decode_backend_one, startup_message}

auto startup = startup_message("app", "notes").unwrap()
auto decoded = decode_backend_one(backend_bytes)
```

`PostgresClient::connect_async` establishes one serial session. Its normal
constructor, `PostgresConfig::secure`, requires verified TLS and SCRAM-SHA-256;
`secure_with_ca_file` provides the same verified path for a deployment private
CA. An SSLRequest refusal fails the connection rather than silently falling
back to plaintext. `trusted_local` and `insecure_tls_for_test` are deliberately
named escape hatches for a separately secured local deployment and deterministic
test fixtures. They are never fallback paths.

`scram_client_first`, `scram_client_final`, and
`ScramClientFinal::verify_server_final` supply the algorithmic SCRAM-SHA-256
steps used by later startup code. This initial helper accepts the portable
printable-ASCII credential profile only; it explicitly rejects non-ASCII input
until a qualified SASLprep/Unicode profile exists.

The serial simple-query client executes one SQL string through `ReadyForQuery`:

```toka
import official/postgres::{PostgresQueryLimits}

auto limits = PostgresQueryLimits::new(8, 10000, 8 * 1024 * 1024).unwrap()
auto results = client#.simple_query_async("SELECT id, body FROM notes", limits).await.unwrap()
```

Each `CommandComplete` becomes one owned result, so a SQL string containing
multiple statements cannot silently discard later result sets. `DataRow`
values are owned `Bytes`; SQL NULL remains `Option::None`. The caller must set
maximum result-set count, total rows, and retained data bytes before a query is
sent. Server `ErrorResponse` preserves its SQLSTATE in `PostgresError` and is
drained through its following `ReadyForQuery`, so the session remains usable.
I/O, timeout, cancellation, limit, or protocol sequence failure closes the
serial client before it can be reused.

COPY, cursor pagination, and pool-side prepared-statement caching remain
separate, explicitly qualified slices.

Prepared statements use PostgreSQL's extended protocol (`Parse`, statement
`Describe`, `Bind`, `Execute`, `Sync`) and retain parameter bytes until their
single outgoing frame is complete. `PostgresParam::Bytes` is sent in binary
format; text, integer, and boolean values use PostgreSQL text format. A
statement is session-local and must be closed against the same client.

```toka
auto types# = Vec<u32>::new()
types#.push(25) // text
auto statement# = client#.prepare_async("SELECT $1", cede types).await!
auto params# = Vec<PostgresParam>::new()
params#.push(cede PostgresParam::Text(string::from("note")))
auto rows = statement#.execute_async(client, cede params, limits).await!
statement#.close_async(client).await!
```

`PostgresTransaction::begin_async` consumes a client. `commit_async` or
`rollback_async` returns it only after the matching command reaches
`ReadyForQuery`; dropping an active transaction closes the client. This makes
it impossible to issue an unrelated serial command through the same client
while the transaction is live.

`PostgresPool` uses a shared pool handle and exclusive `PostgresPoolLease`.
It never shares a socket across requests; a lease is automatically returned on
drop if healthy. `try_acquire_async` fails immediately on exhaustion, while
`acquire_async(timeout_ms)` waits with a bounded, cancellation-aware timeout.
`query_params_async` performs one parameterized Parse/Bind/Execute/Close
exchange while the lease owns the socket, so no session-local statement can
outlive its lease. `begin_transaction_async` consumes the lease and returns a
`PostgresPoolTransaction`; commit or rollback returns its healthy client to
the pool, while dropping an active transaction closes and discards it.

## Qualification

Run from this package root:

```text
../../build/bin/tokac -I ../../lib -I lib tests/protocol_v1.tk -o /tmp/postgres_protocol_v1 && /tmp/postgres_protocol_v1
../../build/bin/tokac -I ../../lib -I lib tests/client_v1.tk -o /tmp/postgres_client_v1 && /tmp/postgres_client_v1
../../build/bin/tokac -I ../../lib -I lib tests/query_v1.tk -o /tmp/postgres_query_v1 && /tmp/postgres_query_v1
../../build/bin/tokac -I ../../lib -I lib tests/extended_v1.tk -o /tmp/postgres_extended_v1 && /tmp/postgres_extended_v1
../../build/bin/tokac -I ../../lib -I lib tests/pool_v1.tk -o /tmp/postgres_pool_v1 && /tmp/postgres_pool_v1
../../build/bin/tokac -I ../../lib -I lib tests/pool_extended_v1.tk -o /tmp/postgres_pool_extended_v1 && /tmp/postgres_pool_extended_v1
python3 tests/qualify_package.py
```

Real-service compatibility is a separate fail-closed Linux/Docker
qualification. It verifies PostgreSQL 16.x and 17.x with private-CA TLS and
SCRAM-SHA-256; a runner that cannot publish loopback ports is reported as
`not-run`, never as a passing package test.

```text
python3 tools/scripts/qualify_data_access_real.py --tokac build/bin/tokac --report build/data-access-real-service.json
```

Run that command from the repository root. The exact scope and artifact policy
are documented in [`docs/data_access_real_service_compatibility_v1.md`](../../docs/data_access_real_service_compatibility_v1.md).
