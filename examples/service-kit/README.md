# Service Kit

`service-kit` is a deliberately bounded reference application.  It demonstrates
how a stateful HTTP service composes Toka's existing layers without introducing
a web framework:

```text
std/net TcpListener
        -> stdx/net/http server connection lifecycle
        -> servicekit request dispatch
        -> official/sqlite parameterized persistence
```

## Surface

- `GET /health` returns `{"status":"ok"}`.
- `POST /notes` accepts `{"body":"..."}` and returns a persisted note with
  HTTP `201 Created`.
- `GET /notes/:id` returns that note or a JSON HTTP `404` error.
- `ServiceConfig` supplies the bind address, SQLite path, and log level.

The application intentionally handles one request per connection and closes
that connection.  In addition to `serve_one_async` for small examples,
`serve_until_canceled_async` composes `Canceler`, cancellation-aware accept,
and `TaskScope` into a bounded long-lived lifecycle: cancellation stops the
next accept, completed workers drain until the caller-provided deadline, and
remaining workers receive cooperative cancellation.  Each worker opens its
own SQLite connection; concurrent-write policy remains deliberately out of
scope.

The service does not install process-global signal handlers itself.  A host
that wants POSIX `SIGINT`/`SIGTERM` handling installs `std/signal` explicitly
and calls the supplied `Canceler`; tests can supply any other cancellation
source.

It does not claim routing middleware, authentication, TLS termination, an
ORM, migrations, HTTP/2, or a daemon supervisor.

## Qualification

From the repository root, after building Toka and its runtime object:

```bash
python3 examples/service-kit/tests/qualify.py
```

The qualification builds the official SQLite bridge and executes two native
programs:

1. deterministic in-process dispatch, including malformed JSON, not-found,
   statement/handle cleanup, and restart persistence;
2. loopback TCP requests for health, malformed JSON, create, read, not-found,
   and restart persistence.  Each request waits for its server task and proves
   no SQLite handle or statement remains live.
3. a cancellation-driven long-lived loopback service that accepts a health
   request, stops accepting after cancellation, drains the completed worker,
   and proves no SQLite handle, statement, or accept cancellation token remains.

This requires the opt-in native SQLite development package (`sqlite3` through
`pkg-config`), just as `official/sqlite` does.
