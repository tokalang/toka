# Redis real-service compatibility v1

Status: **the qualification contract and Docker runner are canonical in
[`tokalang/redis`](https://github.com/tokalang/redis). A row is green only
when its runner JSON report says `"passed"`; its evidence level is either a
maintainer run or a CI artifact.**

## Qualification policy

The deterministic protocol and loopback fixture suites remain correctness
gates. They are intentionally separate from this document's interoperability
evidence: a fake peer gives deterministic error, cancellation, and ownership
coverage; the real-service runner proves that the supported Redis series
accept the client on the wire.

The standalone runner in `tokalang/redis` (`tests/qualify_real_service.py`) is fail-closed.
It creates short-lived Docker services published only as `127.0.0.1:ephemeral-port`,
creates a per-run test CA, compiles the Redis fixtures, and writes an
evidence JSON report when `--report` is supplied.

| Runner condition | Exit | Qualification meaning |
|---|---:|---|
| Docker daemon, `openssl`, compiler, and loopback publication available | `0` | Every selected row passed. The report is `maintainer-run green` on a maintainer host, or `CI green` when produced by CI. |
| Docker unavailable, daemon unavailable, or loopback publication forbidden (including sandbox `EPERM`) | `2` | **Not run.** This is a runner-eligibility failure, neither a product regression nor a pass. |
| Fixture, image startup, protocol, or client contract fails | `1` | Product/service qualification failed; retain the JSON report and container logs. |

Docker is the real-service execution mechanism. A local maintainer machine
with Docker Desktop and loopback publication is an eligible runner and a
successful report is valid `maintainer-run green` evidence. The Linux job in
[`tokalang/redis/.github/workflows/ci.yml`](https://github.com/tokalang/redis/blob/main/.github/workflows/ci.yml)
is the reproducible automated host used for the release gate; it uploads
`redis-real-service.json` whether the job passes or fails. Developers
using a restricted desktop sandbox must run the command on an eligible host or
read that status as `not-run`; they must not relabel it as a green integration
fixture.

```text
python3 tools/scripts/qualify_redis_real.py \
  --tokac build/bin/tokac \
  --report build/redis-real-service.json
```

## Frozen v1 server matrix

The supported compatibility series are deliberately small. The runner records
the exact server patch reported by each container in its JSON artifact; a new
major/minor series needs a matrix change and a fresh executed artifact.

| Product | Supported server series | Authentication evidence | TLS evidence | Deliberately outside this matrix |
|---|---|---|---|---|
| Redis | 7.4.x, 8.2.x | Password `AUTH` / `requirepass` | Plain TCP and verified TLS using a private test CA and `localhost` SAN | ACL users, mutual TLS, RESP3, Cluster, Sentinel, Pub/Sub |

## Per-row contract

| Package | Real service assertions |
|---|---|
| `official/redis` | Connect; authenticate; binary `SET`/`GET`; ordered `PING`/`GET` pipeline; server `WRONGTYPE` returns a structured server error; a max-one pool lease blocks then cancels a second acquire; reborrow succeeds without a second `AUTH` (proving reuse); `close` rejects subsequent acquire. The test runs once on TCP and once on private-CA TLS for each Redis series. |

Pool-admission cancellation is exercised against a real service. Wire-level
cancellation/poison cases remain deterministic fixtures because an ordinary
Redis server provides no stable way to stop a request at the exact
post-write/pre-reply boundary; those redlines are still required before a
package release.

## Scope and release evidence

`RedisPool` / `RedisLease` is a package-owned, dedicated pool. This matrix
does not introduce or imply a generic `Pool<T>` contract. The reference
data-access service under
[`examples/data-access-service`](../examples/data-access-service) composes
router dispatch, Redis caching, PostgreSQL transactions, JSON logging,
metrics, and shutdown; it is composition evidence, not a framework API.
PostgreSQL owns its real-service qualification in
[`tokalang/postgres`](https://github.com/tokalang/postgres).

For release review, retain all three kinds of evidence together:

1. deterministic package qualification and loopback fixtures;
2. the current CI `redis-real-service` artifact for every matrix row;
3. the public-import and locked/offline replay already required of official
   packages.

No checked-in report is a substitute for an execution on the revision being
reviewed. A maintainer-run green report validates the matrix but is not called
CI green; the current Linux CI artifact is the additional release-gate record.
In particular, an unavailable local Docker daemon leaves the matrix at
`not-run`, not `passed`.
