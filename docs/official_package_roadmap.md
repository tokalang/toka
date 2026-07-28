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
| `official/redis` | qualified RESP2 codec, serial plaintext TCP client, and GET/SET/DEL wrappers; not yet released | deterministic codec/TCP cancellation and typed-operation suites plus locked/offline local-consumer import replay pass | optional real-Redis compatibility gate before publication |
| `official/router` | qualified deterministic method/path recognizer with service-kit adoption; not yet released | route definition/matching qualification, service-kit dispatcher/loopback/shutdown suite, and locked/offline local-consumer import replay pass | optional independent service compatibility evidence before publication |
| `official/compress` | qualified bounded streaming gzip/zlib bridge; not yet released | native zlib bridge, streaming boundary suite, and locked/offline `toka build` public-import replay pass | optional HTTP/content-encoding policy slice before publication |
| `official/postgres` | qualified bounded PostgreSQL v3 wire codec plus ASCII-profile SCRAM helper; not yet a connection client | direct protocol/SCRAM fixtures and locked/offline local-consumer import replay pass | secure TLS startup, then serial simple-query client and real-service compatibility evidence |

Regex is a bounded byte-oriented RE2 profile, not a claim of full RE2
compatibility. No official package should claim a stronger release status than
its executable evidence supports.

## Priority sequence

1. **`official/redis`** — bounded asynchronous RESP2 client. It has the best
   production payoff per unit of new substrate: cache/session/queue use cases,
   no native dependency, and direct exercise of Toka's owner-carrying async
   byte stream, cancellation, and protocol-error boundaries.
2. **`official/router`** — deterministic HTTP method/path router above
   `stdx/net/http`. It turns the existing service-kit into a reusable service
   composition without introducing a framework, middleware system, or ORM.
3. **`official/compress`** — opt-in streaming gzip/zlib bridge, with native
   dependencies declared in the manifest. It should follow an agreed streaming
   ownership contract instead of adding one-off HTTP compression hooks.
4. **`official/postgres`** — asynchronous PostgreSQL wire client. Its bounded
   protocol codec and algorithmic SCRAM helper are now qualified. Secure
   startup remains the next slice: verified TLS, SCRAM integration, recovery,
   and cancellation must close before a
   client API is advertised.
5. **`official/pool`** — only after Redis and PostgreSQL establish common
   checkout, health, cancellation, and poisoned-connection rules. A generic
   pool before that evidence would be speculative abstraction.

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
