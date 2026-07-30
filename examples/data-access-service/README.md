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

The deterministic loopback suite drives two HTTP requests through the cache
miss/fill/hit path, verifies a PostgreSQL parameterized transaction, renders
the metrics endpoint, cancels the accept loop, and proves both mock data
servers observe their pooled connection close.
