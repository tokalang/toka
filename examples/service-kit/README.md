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
that connection.  It does not claim routing middleware, authentication, TLS
termination, an ORM, migrations, concurrent writes, or a daemon supervisor.

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

This requires the opt-in native SQLite development package (`sqlite3` through
`pkg-config`), just as `official/sqlite` does.
