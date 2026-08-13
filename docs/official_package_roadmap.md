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
| `official/sqlite` | released [`0.1.0`](https://github.com/tokalang/sqlite/releases/tag/v0.1.0) standalone Phase 1 native lifecycle bridge | tagged GitHub Release archive, immutable catalog digest, standalone Linux/macOS qualification, and fresh public-registry/offline consumer replay | maintain the package in [`tokalang/sqlite`](https://github.com/tokalang/sqlite); expand only through separately scoped statement/transaction slices |
| `official/regex` | released `0.1.0` historical monorepo source and [`0.1.1`](https://github.com/tokalang/regex/releases/tag/v0.1.1) standalone successor | profile suite, tagged release archives, immutable catalog digests, standalone Linux/macOS CI, and fresh public-registry/offline consumer replay | maintain the package in [`tokalang/regex`](https://github.com/tokalang/regex); expand compatibility only through a separately scoped release |
| `official/unicode` | released [`0.1.1`](https://github.com/tokalang/unicode/releases/tag/v0.1.1) standalone Unicode 17.0.0 UAX #29 revision 47 extended-grapheme segmenter | locked UCD source checksums, generated tables, complete `GraphemeBreakTest.txt` corpus, standalone qualification, and exact public-registry/archive-only consumer replay | maintain the package in [`tokalang/unicode`](https://github.com/tokalang/unicode); keep normalization and layout out of v1 |
| `official/gui` | released [`0.1.0`](https://github.com/tokalang/gui/releases/tag/v0.1.0) standalone macOS native GUI vertical slice | standalone package qualification, two exact-release demos, a public consumer, strict GUI-plus-Unicode archive-only replay, native Objective-C rebuild, and AppKit/Metal/QuartzCore linkage | maintain the package and demos in [`tokalang/gui`](https://github.com/tokalang/gui); keep interactive desktop, IME, and cross-platform GUI 1.0 completion as explicit future gates |
| `official/redis` | bounded RESP2 codec, serial TCP/TLS client, ordered pipeline, and dedicated bounded pool; maintainer-qualified, pending release | deterministic codec/TCP cancellation, pipeline, and pool reuse/poison suites plus locked/offline local-consumer import replay pass; current-revision Docker matrix is maintainer-run green | retain a green current-revision Linux CI `redis-real-service` artifact before publication |
| `official/router` | released [`0.1.0`](https://github.com/tokalang/router/releases/tag/v0.1.0) standalone package | tagged GitHub Release archive, immutable catalog digest, Linux/macOS qualification, and fresh public-registry/offline consumer replay | maintain the package in [`tokalang/router`](https://github.com/tokalang/router); expand compatibility only through a separately scoped release |
| `official/compress` | released [`0.1.0`](https://github.com/tokalang/compress/releases/tag/v0.1.0) standalone package | tagged GitHub Release archive, immutable catalog digest, standalone Linux/macOS qualification, fresh public-registry/offline consumer replay, streaming and decode-limit suites, and a no-zlib/libzstd `stdx/net/http` consumer pass | maintain the package in [`tokalang/compress`](https://github.com/tokalang/compress); do not add an HTTP-core dependency |
| `official/openai_compat` | released [`0.1.1`](https://github.com/tokalang/openai_compat/releases/tag/v0.1.1) standalone package | tagged `toka publish` archive, immutable catalog digest, Linux/macOS qualification, and fresh public-registry/offline consumer replay | maintain the package in [`tokalang/openai_compat`](https://github.com/tokalang/openai_compat); expand compatibility only through a separately scoped release |
| `official/postgres` | released [`0.1.0`](https://github.com/tokalang/postgres/releases/tag/v0.1.0) standalone bounded PostgreSQL v3 client | tagged GitHub Release archive, immutable catalog digest, standalone Linux/macOS deterministic qualification, PostgreSQL 16/17 TLS/SCRAM tag gate, and fresh public-registry/offline consumer replay | maintain the package in [`tokalang/postgres`](https://github.com/tokalang/postgres); expand compatibility only through a separately scoped release |
| data-access service reference | router → Redis cache → PostgreSQL transaction, correlated JSON events, scrape-neutral metrics, and shutdown | `examples/data-access-service` compile and loopback fixture using the exact `postgres@0.1.0` lock; runtime requires a loopback-capable runner | retain the Redis real-service artifact and locked PostgreSQL composition fixture; this is composition evidence, not a framework surface |

Regex is a bounded byte-oriented RE2 profile, not a claim of full RE2
compatibility. No official package should claim a stronger release status than
its executable evidence supports.

## Priority sequence

1. **Agent streaming composition** — use `stdx/net/sse` plus
   `official/openai_compat` in `examples/agent-service` against a deterministic
   streaming provider fixture. Keep credential ownership, HTTP endpoint policy,
   cancellation, and audit persistence at the application boundary.
2. **Redis real-service release evidence** — retain a current-revision Linux
   CI copy of the already green Redis matrix before publication. PostgreSQL
   owns its completed 16/17 tag gate in `tokalang/postgres`. The remaining
   contract and runner are in
   [redis_real_service_compatibility_v1.md](redis_real_service_compatibility_v1.md).
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
