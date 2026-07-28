# `official/sqlite` bridge v1

Status: **Phase 0 native preflight**. This package is intentionally opt-in;
the repository's compiler, `std`, and `stdx` builds must not acquire a SQLite
link dependency.

`official/sqlite` will be a synchronous, safe bridge to the system SQLite C
library. SQLite handles and raw C pointers remain in the package's private
native boundary. The public Toka API will use owned values and structured
errors only.

## Frozen v1 direction

- `Database::open`, `execute`, and `prepare`;
- `Statement` bind, step, reset, and exactly-once finalization;
- owned scalar/text row values, never SQLite-owned pointer views;
- explicit `Transaction` commit or rollback, with rollback on ordinary scope
  cleanup;
- structured SQLite code/message errors.

The v1 bridge excludes an ORM, SQL parser, connection pool, async API,
migrations, extensions, and cross-database abstraction.

## Native boundary

The manifest declares `sqlite3` as required **only when this package is
selected**. The preflight compiles a small C shim, generates Toka LLVM IR, and
links that selected program with `sqlite3`. It proves both ABI availability and
an in-memory open/create/insert/close round trip without changing the global
Toka build.

Run from this directory:

```text
python3 tests/qualify_preflight.py
```
