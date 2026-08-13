# Data Access Service

`data-access-service` is a bounded production-composition reference, not a
web framework. It makes one common request path explicit:

```text
HTTP/1.1 request
  -> official/router
  -> RedisPool GET
  -> PostgresPoolLease -> PostgresPoolTransaction -> parameterized query
  -> RedisPool SET
  -> JSON response, JSON event, Prometheus text metrics
```

`GET /notes/:id` first checks the cache. A cache miss acquires a PostgreSQL
lease, consumes it into a transaction, runs a parameterized query, commits,
then fills the cache. The server never exposes a raw socket or a session-local
prepared statement to application code. `GET /metrics` returns the current
in-process Prometheus registry; its intentionally unlabelled counters are a
small proof of integration, not a metrics framework.

## Request correlation and metrics

The reference service applies a deliberately small application convention. It
accepts a gateway-supplied `X-Request-Id` only when it is 1--128 ASCII
letters, digits, `.`, `_`, or `-`; otherwise it generates a process-local
`data-access-N` ID. Every normal application response echoes that ID and every
completed application request emits one JSON event with `request_id`, `route`,
`source`, and `status` fields. This is not a tracing protocol.

The registry exposes total application requests, cache hits, database
transactions, client errors, and server errors. `/metrics` is a control-plane
endpoint: it echoes the request ID but does not log or increment the business
counters it renders, so scraping cannot feed back into the series.

`serve_until_canceled_async` is the lifecycle boundary: cancellation stops
accepting, `TaskScope` drains workers to its deadline, and the caller then
calls `DataAccessService::close()` to drain idle Redis/PostgreSQL connections.
Checked-out leases observe the closed pool on return and close instead of being
requeued. The application deliberately leaves process signal installation,
authentication, migrations, cache invalidation, and HTTP/2 to its host.

## Qualification

From the repository root:

```bash
python3 examples/data-access-service/tests/qualify.py
```

The qualifier materializes the committed exact `postgres@0.1.0` registry lock;
the canonical package and its PostgreSQL 16/17 real-service gate are owned by
[`tokalang/postgres`](https://github.com/tokalang/postgres).

On a runner that permits loopback binds, the deterministic loopback suite
drives health, a 404, and the cache miss/fill/hit path; verifies a PostgreSQL
parameterized transaction; checks request-correlated JSON events and metrics;
cancels the accept loop; and proves both mock data servers observe their
pooled connection close. A sandbox that forbids loopback is not runtime
qualification evidence: the fixture must remain red until it runs on an
eligible runner.
