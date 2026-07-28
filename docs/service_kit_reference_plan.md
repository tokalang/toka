# Service Kit Reference Application Plan

Status: `InProgress`

`examples/service-kit` is a bounded production-composition reference
application. It consumes existing Toka libraries before requesting new surface
area. Its purpose is to prove that an ordinary stateful HTTP service can be
built from the 1.0 substrate, not to establish a web framework.

## Product boundary

- `GET /health` returns a deterministic JSON health response;
- `POST /notes` accepts one JSON note and persists it in SQLite;
- `GET /notes/:id` returns one JSON note or a typed HTTP not-found response;
- a restart against the same database retains committed notes;
- configuration supplies bind address, database path, and log level;
- request and persistence failures produce bounded error responses and do not
  leak native SQLite statements or database handles.

The application does not promise authentication, TLS termination, middleware,
an ORM, migrations, concurrent writes, a general router, or a production
daemon supervisor.

## Evidence order

1. Build a deterministic in-process request dispatcher using the current HTTP
   request/response types and SQLite bridge.
2. Add a loopback TCP qualification for health, create, read, malformed JSON,
   not-found, and restart persistence.
3. Record each concrete blocker. A missing capability is added to `std` or
   `stdx` only if it is general and cannot be expressed safely in the app.
4. Extract `official/router` only if at least two independent routes require
   the same routing abstraction; otherwise routing remains application code.

## Stop condition

Stop when the loopback qualification passes from a clean build, persistence
survives reopen, errors leave no live SQLite handles/statements, and no open
finding is a compiler correctness, safety, or required-workload blocker.
Further framework features require a separate product decision.
