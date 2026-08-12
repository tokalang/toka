# Toka 1.0 Six-Tier API Stability Matrix

**Status**: Qualification Layer Classification  
**Purpose**: Categorize interfaces across `core`, `std`, `stdx`, and qualified
`official` packages into distinct stability tiers for Toka 1.0.

---

## 1. Stability Tier Definitions

1. **Tier 1: Language 1.0**: Core syntax, ownership mechanics (`borrow`, `cede`, `move`), value/handle sigils (`#`, `$`), postfix `!`, and async function morphology (`async`, `.await`, `.start`).
2. **Tier 2: Core / Std 1.0**: Fundamental runtime data structures (`Vec`, `Bytes`, `string`, `str`), `Option`/`Result`, `@Iterable`/`@Iterator` traits, `File`/`Reader`/`Writer`, `Task`/`Context`.
3. **Tier 3: Stdx 1.0**: Standard extensions module (`HttpRequest`/`HttpResponseStream`, `HttpHeaderView`, TLS stream adapter, WebSocket framing).
4. **Tier 4: Qualified Official Packages**: Independently versioned packages
   with executable package qualification. `official/redis` and
   `official/postgres` expose bounded, dedicated connection pools here; this
   does not stabilize a generic pooling abstraction. Their real-service
   interoperability evidence additionally requires the loopback-capable
   runner defined in
   [`data_access_real_service_compatibility_v1.md`](data_access_real_service_compatibility_v1.md);
   a restricted-sandbox `EPERM` is `not-run`, never green.
5. **Tier 5: Legacy / Deprecated Shims**: Pre-1.0 C-style raw pointer APIs (`read_async(*buf, len)`, `to_u8_ptr`). Retained temporarily for low-level compatibility; NO new implementation in `stdx` may rely on Legacy APIs.
6. **Tier 6: Experimental / Post-1.0**: Parameterized task spawning, generic
   connection pools, HTTP/2, structured task scopes, advanced async
   combinators.

---

## 2. API Classification Matrix

| Module / Subject | API / Feature Surface | Tier | Safety Contract & Scope Notes |
|---|---|---|---|
| **Syntax & Ownership** | `borrow`, `cede`, `move`, `~T`, `#`, `$` | **Tier 1: Language 1.0** | Core permission and transfer model. |
| **Error Handling** | Postfix `!`, `Option<T>`, `Result<T, E>` | **Tier 1: Language 1.0** | Early return cleanup and fallible computation. |
| **Async Mechanics** | `fn -> async T`, `.await`, `.wait`, `.start` | **Tier 1: Language 1.0** | Async function invocation & task start. |
| **`std/task.tk`** | `@HostEventSource`, `pump_with_host`, `host_mailbox` | **Tier 2: Core/Std 1.0** | Bounded, current-thread coordination of a host event source with ready tasks, timers, and non-blocking socket readiness; `@Send` worker data may enter a non-`Send` host inbox, with no global callback registration or GUI dependency. |
| **`core/string.tk`** | `string`, `str`, `bytes`, `from`, `as_str` | **Tier 2: Core/Std 1.0** | Safe owned and slice UTF-8 strings. |
| **`std/bytes.tk`** | `Vec<u8> → Bytes → bytes` via `from_vec`, `into_vec`, `as_slice` | **Tier 2: Core/Std 1.0** | Mutable I/O owner, zero-copy frozen owner, and borrowed binary view. |
| **`std/slab.tk`** | `Slab<T>`, generational `SlabID`, `insert`, `get`, `get_mut`, `remove`, `clear` | **Tier 2: Core/Std 1.0** | A `SlabID` is an index plus generation, never a raw address. `remove` and `clear` invalidate every affected prior ID; later slot reuse cannot revive a stale ID. A slot is retired rather than allowing its packed 32-bit generation to wrap. |
| **`std/net.tk`** | `TcpStream`, `TcpListener`, `TlsSession`, owner-based read/write | **Tier 2: Core/Std 1.0** | Async TCP/TLS engine; TLS FFI handles and suspended buffer pointers stay below `stdx`. |
| **`std/random.tk`** | `secure_random_bytes`, `fill_secure_random_bytes` | **Tier 2: Core/Std 1.0** | OS-backed cryptographic entropy; separate from deterministic PRNGs. |
| **`std/io.tk`** | `File::open`, `read_to_string()`, `write()`, `close()` | **Tier 2: Core/Std 1.0** | Synchronous file I/O operations. |
| **`std/fs.tk`** | `write_string_atomic` | **Tier 2: Core/Std 1.0** | Atomic reader visibility on supported platforms; not a durability transaction. |
| **`std/process.tk`** | `Command`, `Child`, per-child cwd/env/stdio, explicit cancellation | **Tier 2: Core/Std 1.0** | Structured argv and explicit wait ownership. Per-child configuration and TERM/KILL cancellation are POSIX-only; Windows/WASI reject non-default configuration rather than silently changing process-global state. |
| **`stdx/net/http.tk`** | `HttpRequest`, `HttpResponse`, `HttpResponseStream` | **Tier 3: Stdx 1.0** | Owner-carrying HTTP/1.1 framing. |
| **`stdx/net/http.tk`** | `HttpServer::accept_async`, `HttpServerConnection` | **Tier 3: Stdx 1.0** | Safe server-side request framing and complete response writes; routing remains above this layer. |
| **`stdx/net/http.tk`** | `HttpHeaderView`, `name()`, `value()` | **Tier 3: Stdx 1.0** | Non-owning borrowed offset views. |
| **`stdx/net/websocket.tk`** | `WebSocketFrame`, `WsStream` | **Tier 3: Stdx 1.0** | WebSocket async frame processing. |
| **`stdx/net/url.tk`** | hierarchical URL and relative-reference parser | **Tier 3: Stdx 1.0** | Parses authority, bracketed IPv6, path, query, and fragment; no IDNA, userinfo, or RFC 3986 reference resolution. |
| **`stdx/net/cookie.tk`** | single `Set-Cookie` value parser and formatter | **Tier 3: Stdx 1.0** | Value type only; no cookie jar, `Expires` date parser, domain matching, or public-suffix policy. |
| **`stdx/encoding/*`** | safe hex, Base64, percent codecs | **Tier 3: Stdx 1.0** | Binary-safe `bytes` input, owned decode output, structured errors. |
| **`stdx/data/form.tk`** | ordered `application/x-www-form-urlencoded` fields | **Tier 3: Stdx 1.0** | Preserves duplicates and ordering; form `+` semantics are confined to this module. |
| **`stdx/crypto/{sha1,sha256,sha512,hmac,constant_time}.tk`** | SHA-1/SHA-256/SHA-512, HMAC, equal-length timing-safe comparison | **Tier 3: Stdx 1.0** | Safe `bytes` views for SHA-1/SHA-256/SHA-512 MAC generation and verification; length is not treated as secret. |
| **`stdx/crypto/token.tk`** | OS-random hexadecimal and Base64URL tokens | **Tier 3: Stdx 1.0** | Delegates entropy to `std/random`; does not impose session or expiry policy. |
| **`stdx/crypto/hkdf.tk`** | HKDF-SHA-256 extract, expand, derive | **Tier 3: Stdx 1.0** | RFC 5869 key derivation; no protocol-specific key schedule policy. |
| **`stdx/crypto/pbkdf2.tk`** | PBKDF2-HMAC-SHA-256 derivation | **Tier 3: Stdx 1.0** | RFC 8018 derivation with explicit work factor and bounded output; protocol adapters choose their own password policy. |
| **`official/compress`** | streaming Gzip/Zlib plus optional `official/compress/http` policy | **Tier 4: Qualified Official** | zlib remains package-private and optional. The HTTP module composes `HttpRequest`/`HttpResponse`, explicitly negotiates gzip/identity, completes gzip before emitting headers, and requires decoded-byte plus ratio limits; `stdx/net/http` has no zlib dependency. |
| **`official/redis`** | RESP2 client, ordered pipelines, `RedisPool` / exclusive `RedisLease` | **Tier 4: Qualified Official** | Dedicated package pool, not `Pool<T>`; cancellation, protocol, and reply-alignment failures poison a client before lease return. Real service rows: Redis 7.4.x/8.2.x TCP `AUTH` and private-CA TLS. |
| **`official/postgres`** | PostgreSQL queries, transactions, `PostgresPool` / exclusive `PostgresPoolLease` | **Tier 4: Qualified Official** | Dedicated package pool, not `Pool<T>`; query results are bounded and owned, and a failed, canceled, or active-transaction client is not reused. Real service rows: PostgreSQL 16.x/17.x private-CA TLS + SCRAM. |
| **Data-access service reference** | Router → Redis cache → PostgreSQL transaction → correlated JSON events/metrics/shutdown | **Tier 4 composition evidence** | [`examples/data-access-service`](../examples/data-access-service) demonstrates an application-level `X-Request-Id` convention and scrape-neutral `/metrics`; it adds no web framework, tracing protocol, or generic pool API. |
| **Legacy Shims** | `read_async(*buf, len)`, `to_u8_ptr` | **Tier 5: Legacy** | Retained as compatibility shims; new stdx code prohibited from depending on them. |
| **Async Extensions** | Async blocks, TaskScope, generic connection pools | **Tier 6: Experimental** | Dedicated official package pools are excluded; generic pooling remains deferred. |
