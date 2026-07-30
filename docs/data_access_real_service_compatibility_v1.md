# Data-access real-service compatibility v1

Status: **the qualification contract and Docker runner are committed. A row is
green only when its runner JSON report says `"passed"`; its evidence level is
either a maintainer run or a CI artifact.** This document does not convert a
machine that cannot bind loopback ports into a passing test.

## Qualification policy

The deterministic protocol and loopback fixture suites remain correctness
gates. They are intentionally separate from this document's interoperability
evidence: a fake peer gives deterministic error, cancellation, and ownership
coverage; the real-service runner proves that the supported server products
accept the client on the wire.

`tools/scripts/qualify_data_access_real.py` is fail-closed. It creates
short-lived Docker services published only as `127.0.0.1:ephemeral-port`,
creates a per-run test CA, compiles the two Toka fixtures, and writes an
evidence JSON report when `--report` is supplied.

| Runner condition | Exit | Qualification meaning |
|---|---:|---|
| Docker daemon, `openssl`, compiler, and loopback publication available | `0` | Every selected row passed. The report is `maintainer-run green` on a maintainer host, or `CI green` when produced by CI. |
| Docker unavailable, daemon unavailable, or loopback publication forbidden (including sandbox `EPERM`) | `2` | **Not run.** This is a runner-eligibility failure, neither a product regression nor a pass. |
| Fixture, image startup, protocol, or client contract fails | `1` | Product/service qualification failed; retain the JSON report and container logs. |

Docker is the real-service execution mechanism. A local maintainer machine
with Docker Desktop and loopback publication is an eligible runner and a
successful report is valid `maintainer-run green` evidence. The Linux job in
[`.github/workflows/ci.yml`](../.github/workflows/ci.yml) is only the
reproducible automated host used for the release gate; it uploads
`data-access-real-service.json` whether the job passes or fails. Developers
using a restricted desktop sandbox must run the command on an eligible host or
read that status as `not-run`; they must not relabel it as a green integration
fixture.

```text
python3 tools/scripts/qualify_data_access_real.py \
  --tokac build/bin/tokac \
  --report build/data-access-real-service.json
```

## Frozen v1 server matrix

The supported compatibility series are deliberately small. The runner records
the exact server patch reported by each container in its JSON artifact; a new
major/minor series needs a matrix change and a fresh executed artifact.

| Product | Supported server series | Authentication evidence | TLS evidence | Deliberately outside this matrix |
|---|---|---|---|---|
| Redis | 7.4.x, 8.2.x | Password `AUTH` / `requirepass` | Plain TCP and verified TLS using a private test CA and `localhost` SAN | ACL users, mutual TLS, RESP3, Cluster, Sentinel, Pub/Sub |
| PostgreSQL | 16.x, 17.x | `SCRAM-SHA-256`, printable-ASCII test credentials | Verified TLS using `PostgresConfig::secure_with_ca_file` and a private test CA | Trust/MD5/cleartext auth, client certificates, GSS/SSPI, channel binding, non-ASCII SASLprep profile |

`PostgresConfig::secure` continues to use the system trust store. The
`secure_with_ca_file` constructor is the same verified path with an explicit
deployment CA file; it was added so the matrix proves verification rather than
depending on an insecure test-only TLS mode.

## Per-row contract

| Package | Real service assertions |
|---|---|
| `official/redis` | Connect; authenticate; binary `SET`/`GET`; ordered `PING`/`GET` pipeline; server `WRONGTYPE` returns a structured server error; a max-one pool lease blocks then cancels a second acquire; reborrow succeeds without a second `AUTH` (proving reuse); `close` rejects subsequent acquire. The test runs once on TCP and once on private-CA TLS for each Redis series. |
| `official/postgres` | Verified TLS + SCRAM connection; extended parameter query; `SET application_name` followed by a reborrowed `SHOW` proves same-session pool reuse; pooled transaction commits; division-by-zero returns SQLSTATE `22012` and the following query succeeds after `ReadyForQuery`; a max-one pool acquire is canceled; `close` rejects future acquire. |

Pool-admission cancellation is exercised against a real service. Wire-level
cancellation/poison cases remain deterministic fixtures because an ordinary
Redis/PostgreSQL server provides no stable way to stop a request at the exact
post-write/pre-reply boundary; those redlines are still required before a
package release.

## Scope and release evidence

`RedisPool` / `RedisLease` and `PostgresPool` / `PostgresPoolLease` are
package-owned, dedicated pools. This matrix does not introduce or imply a
generic `Pool<T>` contract. The reference data-access service under
[`examples/data-access-service`](../examples/data-access-service) composes
router dispatch, Redis caching, PostgreSQL transactions, JSON logging,
metrics, and shutdown; it is composition evidence, not a framework API.

For release review, retain all three kinds of evidence together:

1. deterministic package qualification and loopback fixtures;
2. the current CI `data-access-real-service` artifact for every matrix row;
3. the public-import and locked/offline replay already required of official
   packages.

No checked-in report is a substitute for an execution on the revision being
reviewed. A maintainer-run green report validates the matrix but is not called
CI green; the current Linux CI artifact is the additional release-gate record.
In particular, an unavailable local Docker daemon leaves the matrix at
`not-run`, not `passed`.
