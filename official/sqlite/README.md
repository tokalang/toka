# `official/sqlite` bridge v1

Status: **Phase 1 database lifecycle slice**. This package is intentionally opt-in;
the repository's compiler, `std`, and `stdx` builds must not acquire a SQLite
link dependency.

`official/sqlite` is a synchronous, safe bridge to the system SQLite C
library. SQLite handles and raw C pointers remain in the package's private
native boundary. The public Toka API uses owned values and structured errors
only.

## Frozen v1 direction

- `Database::open`, `execute`, explicit idempotent `close`, and RAII close;
- `prepare` and `Statement` bind/step/reset/finalization;
- `Statement` integer/text/null bind, step, reset, owned scalar/text reads,
  and exactly-once finalization;
- owned scalar/text row values, never SQLite-owned pointer views;
- explicit `Transaction` commit or rollback, with rollback on ordinary scope
  cleanup;
- structured SQLite code/message errors.

The v1 bridge excludes an ORM, SQL parser, connection pool, async API,
migrations, extensions, and cross-database abstraction.

Transactions are synchronous, non-nested, and bound to the ordinary lexical
cleanup path. They do not promise rollback after Toka's fail-fast `panic` or
after process termination.

## Native boundary

The manifest declares `sqlite3` as required **only when this package is
selected**. `toka build` compiles the package-private C shim and links SQLite
for a locked consumer only. The preflight independently proves ABI availability
and an in-memory open/create/insert/close round trip without changing the
global Toka build.

Run from this directory:

```text
python3 tests/qualify_preflight.py
```
