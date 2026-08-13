# `official/postgres` v1 — PostgreSQL v3 Client RFC

Status: **released as standalone
[`tokalang/postgres` v0.1.0](https://github.com/tokalang/postgres/releases/tag/v0.1.0),
with deterministic Linux/macOS qualification, PostgreSQL 16/17 TLS/SCRAM tag
evidence, and public-registry/offline consumer replay.**

This compiler repository retains the API and scope contract below. Canonical
source, package CI, real-service qualification, and releases live in
[`tokalang/postgres`](https://github.com/tokalang/postgres). The retained
exact-version cross-repository fixture is
[`toka-examples/registry_postgres_consumer`](https://github.com/tokalang/toka-examples/tree/main/registry_postgres_consumer).

## 1. Role and placement

`official/postgres` is an optional, pure-Toka asynchronous PostgreSQL v3
client. It depends only on public `std` and `stdx` facilities. It is not part
of `std`, does not require a compiler hook, has no package-owned native
dependency, and has no reverse dependency from lower library layers. Its TLS
mode uses the configured `stdx/net/tls` backend.

The package was built in vertical slices, beginning with a bounded wire codec
so framing, binary ownership, server errors, and row decoding had executable
evidence before a connection API was promised.

## 2. v1 target and delivery order

The public client uses owner-carrying `AsyncStream` I/O and exposes one serial
connection. It defaults to verified TLS and supports SCRAM-SHA-256. A
plaintext mode for explicitly trusted local deployments and an insecure TLS
mode for deterministic tests are named opt-ins; a TLS request never silently
downgrades.

Delivery is intentionally ordered as follows:

1. **Wire codec — complete.** Encode StartupMessage and Query; incrementally
   decode bounded backend frames, including authentication, parameter status,
   errors, row descriptions, binary data rows, command completion, and ready
   state.
2. **Secure startup — complete.** `PostgresClient::connect_async` sends
   SSLRequest, upgrades through `stdx/net/tls`, and fails closed if the server
   refuses TLS. `PostgresConfig::secure` requires verified TLS; plaintext and
   unverified TLS are explicit local/test configurations, never fallbacks.
3. **SCRAM-SHA-256 — complete.** It uses SHA-256, HMAC,
   Base64, and the qualified `stdx/crypto/pbkdf2` primitive. The present helper
   accepts printable ASCII credentials only, rather than claiming unqualified
   SASLprep behavior. It requires 4,096 to 1,000,000 iterations and bounds
   decoded salts at 64 KiB. Startup rejects cleartext and MD5 authentication
   in its secure path and verifies the server-final signature before accepting
   AuthOK.
4. **Queries and transactions — complete.** Simple queries retain every
   result set; extended parameter queries use Parse/Bind/Execute; transactions
   retain the serial client until commit or rollback reaches `ReadyForQuery`.
   I/O, limit, cancellation, or protocol failure poisons the connection.
5. **Dedicated pool — complete.** `PostgresPool` and exclusive leases bound
   concurrency without introducing a generic `Pool<T>` surface. A failed,
   canceled, or active transaction client is discarded before return.
6. **Compatibility evidence — complete.** Deterministic mocks remain
   correctness gates. The standalone tag workflow's PostgreSQL 16.x/17.x
   private-CA TLS/SCRAM matrix is release evidence, not a replacement for
   them; see the canonical repository's
   [`qualify_real_service.py`](https://github.com/tokalang/postgres/blob/main/tests/qualify_real_service.py).

## 3. Phase 1 contract

`startup_message(user, database)` and `simple_query_message(sql)` return
owned wire buffers. `decode_backend_one` returns `NeedMore` without consuming
an incomplete frame, or one `PostgresBackendFrame` with an explicit consumed
count. `DataRow` values are owned `Bytes` or explicit nulls, never views into
the source receive buffer.

All frames are limited to 16 MiB; row column counts and field lengths are
validated before allocation or slicing. Backend `ErrorResponse` is parsed into
an owned SQLSTATE/message record. Unknown message tags retain an owned payload
so a later client state machine can decide whether the message is ignorable.

## 4. Explicit non-goals

COPY, notifications/listen, cursors, replication, pool-side prepared-statement
caching, client-certificate/GSS/SSPI authentication, full SASLprep Unicode
normalization, and ORM behavior are outside this v1 surface. The running
service evidence is intentionally bounded to private-CA verified TLS and
ASCII-profile SCRAM against PostgreSQL 16.x and 17.x; the canonical package
workflow owns that evidence for each release.
