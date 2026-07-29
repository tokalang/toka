# `official/postgres` v1

Status: **bounded PostgreSQL v3 wire codec, ASCII-profile SCRAM-SHA-256, and
secure startup client; not yet a query client**.

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
an SSLRequest refusal fails the connection rather than silently falling back to
plaintext. `trusted_local` and `insecure_tls_for_test` are deliberately named
escape hatches for a separately secured local deployment and deterministic test
fixtures. They are never fallback paths.

`scram_client_first`, `scram_client_final`, and
`ScramClientFinal::verify_server_final` supply the algorithmic SCRAM-SHA-256
steps used by later startup code. This initial helper accepts the portable
printable-ASCII credential profile only; it explicitly rejects non-ASCII input
until a qualified SASLprep/Unicode profile exists.

The startup slice intentionally stops before query execution. A later serial
simple-query slice must consume through `ReadyForQuery`, poison a connection
after cancellation or protocol failure, and then add deterministic mock and
real-service compatibility evidence. Parameters, transactions, COPY, and
pooling remain separate, explicitly qualified slices.

## Qualification

Run from this package root:

```text
../../build/bin/tokac -I ../../lib -I lib tests/protocol_v1.tk -o /tmp/postgres_protocol_v1 && /tmp/postgres_protocol_v1
../../build/bin/tokac -I ../../lib -I lib tests/client_v1.tk -o /tmp/postgres_client_v1 && /tmp/postgres_client_v1
python3 tests/qualify_package.py
```
