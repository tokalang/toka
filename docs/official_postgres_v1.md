# `official/postgres` v1 — PostgreSQL v3 Client RFC

Status: **Phase 1 wire codec implemented; transport, TLS, and SCRAM are not
yet a published-client claim.**

## 1. Role and placement

`official/postgres` is an optional, pure-Toka asynchronous PostgreSQL v3
client. It depends only on public `std` and `stdx` facilities. It is not part
of `std`, does not require a compiler hook, and has no native dependency or
reverse dependency from lower library layers.

The package is intentionally built in vertical slices. Its first committed
surface is a bounded wire codec so that framing, binary ownership, server
errors, and row decoding have executable evidence before a connection API is
promised.

## 2. v1 target and delivery order

The target public client will use owner-carrying `AsyncStream` I/O and expose
one serial connection. It will default to verified TLS and support
SCRAM-SHA-256. A plaintext mode, if retained for tests and trusted local
deployments, must be explicit; a TLS request may never silently downgrade.

Delivery is intentionally ordered as follows:

1. **Wire codec — complete.** Encode StartupMessage and Query; incrementally
   decode bounded backend frames, including authentication, parameter status,
   errors, row descriptions, binary data rows, command completion, and ready
   state.
2. **Secure startup.** Send SSLRequest, use `stdx/net/tls` when required, and
   fail closed if the server refuses TLS.
3. **SCRAM-SHA-256.** Use OS randomness, SHA-256, HMAC, Base64, and the
   qualified `stdx/crypto/pbkdf2` primitive. Cleartext and MD5 authentication
   are rejected in the secure client path.
4. **Simple query client.** One query owns the serial connection until it has
   consumed `ReadyForQuery`; errors and cancellation poison the connection so
   a later query cannot receive an abandoned response.
5. **Compatibility evidence.** A deterministic mock remains the correctness
   gate; an opt-in real-PostgreSQL compatibility test is release evidence, not
   a substitute for it.

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

## 4. Explicit non-goals for the codec slice

This slice does not provide a network connection, TLS, authentication,
prepared statements, parameter binding, COPY, notifications, transactions,
pooling, replication, or ORM behavior. It does not claim compatibility with a
running PostgreSQL server until the later secure-startup and client slices are
qualified.
