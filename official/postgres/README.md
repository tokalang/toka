# `official/postgres` v1

Status: **bounded PostgreSQL v3 wire codec and SCRAM-SHA-256 helper; not yet
a connection client**.

`official/postgres` currently encodes StartupMessage and simple Query frames,
then incrementally decodes PostgreSQL backend messages without exposing a view
into its input buffer. Binary row values become owned `Bytes`; incomplete
frames return `PostgresDecode::NeedMore()`.

```toka
import official/postgres::{PostgresDecode, decode_backend_one, startup_message}

auto startup = startup_message("app", "notes").unwrap()
auto decoded = decode_backend_one(backend_bytes)
```

`scram_client_first`, `scram_client_final`, and
`ScramClientFinal::verify_server_final` supply the algorithmic SCRAM-SHA-256
steps used by later startup code. This initial helper accepts the portable
printable-ASCII credential profile only; it explicitly rejects non-ASCII input
until a qualified SASLprep/Unicode profile exists.

This is deliberately not a promise of a usable database connection yet. TLS
startup, serial async I/O, parameters, transactions, COPY, and pooling remain
separate, explicitly qualified slices. The planned secure client will default
to TLS and never silently downgrade after a server refuses it.

## Qualification

Run from this package root:

```text
../../build/bin/tokac -I ../../lib -I lib tests/protocol_v1.tk -o /tmp/postgres_protocol_v1 && /tmp/postgres_protocol_v1
python3 tests/qualify_package.py
```
