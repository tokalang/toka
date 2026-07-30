# Official Package Roadmap

Status: **active planning baseline**.

`official/*` is a publisher namespace for optional packages, not a fourth
standard-library layer. The authoritative placement rules are in
[`official_package_v1.md`](official_package_v1.md): `std` owns indispensable
system abstractions, `stdx` owns bundled general extensions, and official
packages must be independently useful without making either lower layer depend
on them.

## Current evidence

| Package | Status | Evidence | Required next action |
|---|---|---|---|
| `official/sqlite` | qualified Phase 1 native lifecycle bridge | `python3 official/sqlite/tests/qualify_preflight.py` passes | expand only through separately scoped statement/transaction slices |
| `official/regex` | qualified bounded RE2-profile matcher; not yet released | direct profile suite plus locked/offline local-consumer import replay pass | optional independent compatibility corpus before publication |
| `official/redis` | bounded RESP2 codec, serial TCP/TLS client, ordered pipeline, and dedicated bounded pool; not yet released | deterministic codec/TCP cancellation, pipeline, and pool reuse/poison suites plus locked/offline local-consumer import replay pass; real-service matrix runner is a Linux CI gate | retain a green current-revision `data-access-real-service` artifact before publication |
| `official/router` | qualified deterministic method/path recognizer with service-kit adoption; not yet released | route definition/matching qualification, service-kit dispatcher/loopback/shutdown suite, and locked/offline local-consumer import replay pass | optional independent service compatibility evidence before publication |
| `official/compress` | qualified bounded streaming gzip/zlib bridge; not yet released | native zlib bridge, streaming boundary suite, and locked/offline `toka build` public-import replay pass | opt-in HTTP `Content-Encoding` adapter with explicit flush/close and decompression/ratio limits |
| `official/postgres` | bounded PostgreSQL v3 client with secure TLS/SCRAM startup, simple and extended queries, transactions, and a dedicated bounded pool; not yet released | deterministic protocol/SCRAM/TLS/query/extended-query/pool suites plus locked/offline local-consumer import replay pass; real-service matrix runner is a Linux CI gate | retain a green current-revision `data-access-real-service` artifact before publication |
| data-access service reference | router → Redis cache → PostgreSQL transaction, with JSON logs, metrics, and shutdown | `examples/data-access-service` compile and loopback fixture | retain the same real-service artifact; this is composition evidence, not a framework surface |

Regex is a bounded byte-oriented RE2 profile, not a claim of full RE2
compatibility. No official package should claim a stronger release status than
its executable evidence supports.

## Priority sequence

1. **Real-service compatibility** — execute the frozen Redis/PostgreSQL
   matrix on a loopback-capable runner and retain its report. Deterministic
   loopback suites remain the correctness gate; real services add
   interoperability evidence rather than replacing them. The contract and
   runner are in [data_access_real_service_compatibility_v1.md](data_access_real_service_compatibility_v1.md).
2. **`official/compress` HTTP adapter** — build the next bounded slice on the
   existing streaming gzip/zlib bridge: opt-in `Content-Encoding`, explicit
   flush/close, a decompressed-byte limit, and a compression-ratio limit. Do
   not bake compression into the HTTP core.
3. **Observability composition** — connect existing JSON logging and
   Prometheus registry to the reference service entry point and `/metrics`.
   Use the resulting repeated handler code, rather than anticipation, to
   decide whether router middleware is justified.
4. **`official/pool`** — remain deliberately deferred. `RedisPool` and
   `PostgresPool` are concrete package APIs; a common abstraction needs
   evidence that their checkout, cancellation, health, and shutdown semantics
   genuinely coincide.

## Explicit exclusions

Do not duplicate existing bundled capabilities as official packages: JSON,
TOML, logging, Prometheus metrics, UUID, MIME, SemVer, crypto primitives, HTTP,
TLS, and WebSocket already belong to `stdx`. Complete calendar/time-zone work
needs a separate placement decision because `std/time` already owns clocks and
basic calendar conversion. HTTP/2, gRPC, ORM, and code-generation-derived
Serde are later infrastructure projects, not the next package slice.

## Package acceptance gate

Every new official package must have, before release:

1. a static `package.tk` and `AI_CONTRACT` matching the v1 scope;
2. a stable `import official/name::{...}` entry module;
3. structured errors and explicit resource/timeout/cancellation semantics;
4. deterministic local qualification using fixtures or a mock server; optional
   real-service integration must not be the sole correctness evidence;
5. package-lock, offline, and native-dependency evidence where applicable;
6. no compiler-private API or reverse dependency from `std`/`stdx`.

The next design artifact is [`official_compress_v1.md`](official_compress_v1.md).
