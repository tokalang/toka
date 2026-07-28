# Service Kit Reference Application Plan

Status: `Complete`

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

1. Deterministic in-process dispatch uses the current HTTP request/response
   types and the official SQLite bridge.
2. Loopback TCP qualification covers health, create, read, malformed JSON,
   not-found, restart persistence, and native SQLite handle/statement cleanup.
3. The only general blocker discovered was response wire-status preservation
   for `201 Created` and `400 Bad Request`; that is now closed in `stdx/http`.
4. The service uses `official/router` for deterministic method/path/parameter
   recognition, while retaining application-specific validation and response
   policy. This is the router's second real consumer beyond its package suite.

## Stop condition

The loopback qualification passes from a clean native link, persistence
survives reopen, errors leave no live SQLite handles/statements, and no open
finding is a compiler correctness, safety, or required-workload blocker.
Further framework features require a separate product decision.
