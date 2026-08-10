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
| `official/regex` | v1 pilot release `0.1.0`; successor development is in [`tokalang/regex`](https://github.com/tokalang/regex) at `0.1.1-dev.0` | direct profile suite, locked/offline local-consumer import replay, tagged release archive, static-catalog digest, and standalone package CI | qualify and publish a real `0.1.1` successor before removing the historical monorepo root |
| `official/unicode` | qualified Unicode 17.0.0 UAX #29 revision 47 extended-grapheme segmenter; not yet released | locked UCD source checksums, generated tables, complete `GraphemeBreakTest.txt` corpus, and locked/offline local-consumer import replay pass | retain qualification evidence through publication; keep normalization and layout out of v1 |
| `official/redis` | bounded RESP2 codec, serial TCP/TLS client, ordered pipeline, and dedicated bounded pool; maintainer-qualified, pending release | deterministic codec/TCP cancellation, pipeline, and pool reuse/poison suites plus locked/offline local-consumer import replay pass; current-revision Docker matrix is maintainer-run green | retain a green current-revision Linux CI `data-access-real-service` artifact before publication |
| `official/router` | qualified deterministic method/path recognizer with service-kit adoption; not yet released | route definition/matching qualification, service-kit dispatcher/loopback/shutdown suite, and locked/offline local-consumer import replay pass | optional independent service compatibility evidence before publication |
| `official/compress` | qualified bounded streaming gzip/zlib/zstd bridge with optional HTTP `Content-Encoding` policy; not yet released | native zlib/libzstd bridges; streaming, decode-limit, fixed-Zstd-window, and HTTP negotiation/encoding suites; locked/offline `toka build` public-import replay; and a no-zlib/libzstd `stdx/net/http` consumer pass | retain qualification evidence through publication; do not add an HTTP-core dependency |
| `official/openai_compat` | qualified OpenAI-compatible chat-completions SSE semantic adapter; not yet released | bounded text/tool-call/completion/API-error fixture plus locked/offline public-import replay | exercise it through an application-owned streaming provider mock; do not make HTTP, SSE, or credential management package responsibilities |
| `official/postgres` | bounded PostgreSQL v3 client with secure TLS/SCRAM startup, simple and extended queries, transactions, and a dedicated bounded pool; maintainer-qualified, pending release | deterministic protocol/SCRAM/TLS/query/extended-query/pool suites plus locked/offline local-consumer import replay pass; current-revision Docker matrix is maintainer-run green | retain a green current-revision Linux CI `data-access-real-service` artifact before publication |
| data-access service reference | router → Redis cache → PostgreSQL transaction, correlated JSON events, scrape-neutral metrics, and shutdown | `examples/data-access-service` compile and loopback fixture; runtime requires a loopback-capable runner | retain the same real-service artifact; this is composition evidence, not a framework surface |

Regex is a bounded byte-oriented RE2 profile, not a claim of full RE2
compatibility. No official package should claim a stronger release status than
its executable evidence supports.

## Priority sequence

1. **Agent streaming composition** — use `stdx/net/sse` plus
   `official/openai_compat` in `examples/agent-service` against a deterministic
   streaming provider fixture. Keep credential ownership, HTTP endpoint policy,
   cancellation, and audit persistence at the application boundary.
2. **Real-service release evidence** — retain a current-revision Linux CI copy
   of the already green Redis/PostgreSQL matrix before publication. The
   maintainer qualification remains valid while this non-code release artifact
   is pending; the contract and runner are in
   [data_access_real_service_compatibility_v1.md](data_access_real_service_compatibility_v1.md).
3. **Observability evolution** — use repeated application code, rather than
   anticipation, to decide whether router middleware, labels, histograms, or
   a tracing protocol is justified. The current service intentionally keeps a
   narrow request-ID and scrape-neutral metrics convention at the application
   boundary.
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

The next release-evidence artifact is the Linux CI copy of the frozen
real-service compatibility matrix; compression policy is documented in
[`official_compress_v1.md`](official_compress_v1.md).
