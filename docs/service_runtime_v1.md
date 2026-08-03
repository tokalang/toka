# Service Runtime v1

Status: `Complete`

Service Runtime v1 turns the existing one-request HTTP evidence into a bounded,
long-lived service lifecycle.  It is a composition contract, not a web
framework: applications still own routing, request types, database policy, and
deployment configuration.

## Contract

For an explicitly installed shutdown source, the service lifecycle is:

```text
running
  -> shutdown observed
  -> stop accepting new connections
  -> drain tracked request tasks until deadline
  -> cancel remaining request tasks
  -> join every tracked task
  -> exit
```

The first shutdown event wins.  Repeated events are idempotent.  A graceful
drain never invents a successful request result: a request either completes
normally before the deadline or observes cancellation through the existing
task/cancellation substrate.

## Layering

| Layer | Responsibility | Explicit non-responsibility |
|---|---|---|
| `lib/sys/toka_rt.c` | async-signal-safe event capture | executing Toka code from a signal handler |
| `std/signal.tk` | install, poll, and await a shutdown event | automatic process-wide handler installation |
| `std/net.tk` | cancellation-aware `TcpListener` accept | HTTP routing or request policy |
| `std/async.tk` | inspect, drain, cancel, and join a `TaskScope` | network-server semantics |
| `stdx/log/json.tk` | deterministic JSON-line events | a tracing protocol or backend exporter |
| `stdx/metrics/prometheus.tk` | in-process counters/gauges and Prometheus text | aggregation, scraping, or dashboards |
| application | bind address, route dispatch, worker creation, metrics endpoint | a generic router or ORM |

## Signal safety

On POSIX hosts, installation handles `SIGINT` and `SIGTERM`.  The C handler
only stores the first signal in a `volatile sig_atomic_t`; it performs no
allocation, locking, I/O, cancellation, scheduler operation, or Toka call.
`std/signal` observes that stored value from ordinary task context and may then
call a `Canceler`.

Windows and WASI report unsupported in v1 rather than silently pretending to
provide POSIX semantics.  The API remains explicit so those platforms can add
their native console/control-event adapters later.

### Implemented slice

`std/signal` now exposes explicit installation, one-event polling, and an
async waiter.  Its native redline raises `SIGTERM` followed by `SIGINT` and
verifies that the first pending event is observed exactly once.  This is only
the event source; it does not yet constitute server shutdown until the
cancellation-aware accept and structured drain slices below are complete.

## Cancellation-aware accept

`TcpListener::accept_async_context` must register both the listener readiness
and the supplied `Canceler` before suspension.  Whichever event wins releases
the losing registration.  Cancellation returns a typed `Err("context canceled")`
and does not accept a new connection after the cancellation winner is visible.

This avoids relying on a second task closing a listener fd behind an in-flight
accept, which is race-prone and invalidates ownership of the listener.

## Structured drain

`TaskScope` gains three distinct operations:

- `spawn_into(scope, cede task)` attempts the typed handoff, schedules an
  accepted child without exposing a TCB or requiring the caller to spell the
  result type, and returns `Result<(), TaskHandle<T>>`; a scope that has begun
  closing returns the untouched handle to its caller;
- `is_done()` checks whether every retained task is terminal;
- `join_async()` waits without requesting cancellation;
- `shutdown_async(drain_timeout_ms)` first drains, then cancels and joins only
  if the deadline expires.  Its boolean result says whether draining completed
  before cancellation was necessary.

Existing `close()` remains source-compatible as immediate cancel-and-join.
The deadline is a cooperative-cancellation boundary, not forced preemption:
individual request operations must retain their own finite I/O timeouts.

## Structured telemetry

JSON logging takes an explicit level, message, and owned string fields.  It
always escapes JSON strings and filters before formatting.  The initial metrics
registry supports validated counter/gauge names and deterministic Prometheus
text rendering.  The data-access reference service exercises a narrow
application-level request-ID convention and excludes `/metrics` scrapes from
its business counters; reusable runtime libraries do not prescribe that
policy. W3C TraceContext and OpenTelemetry exporters remain out of scope.

### Implemented composition

The bounded `examples/service-kit` now supplies
`serve_until_canceled_async`.  It owns the listener, accepts through the
cancellation-aware HTTP adapter, transfers each worker into the scope through
`spawn_into`, reaps completed worker references between accepts, and invokes
`TaskScope.shutdown_async` after cancellation.  The application supplies the
`Canceler`; it may obtain it from `std/signal` or any other explicit source.

## Non-goals

- generic routing, middleware, authentication, TLS termination, daemon
  supervision, HTTP/2, HTTP/3, gRPC, and distributed tracing;
- forced task preemption or unsafe listener closure from a signal handler;
- a general connection pool;
- implicit handler installation or global mutable request state.

## Evidence required for closure

1. a native C redline proves first-signal capture is idempotent;
2. a Toka qualification proves canceled accept wakes promptly without a
   connection;
3. a loopback service proves shutdown stops new acceptance, drains a completed
   request, and leaves no live cancellation/SQLite handle; the `TaskScope`
   qualification independently proves deadline-triggered cancel-and-join;
4. JSON log escaping/filtering and Prometheus rendering have deterministic unit
   tests;
5. existing async, HTTP, and service-kit qualifications remain green.

All five gates are implemented by `shutdown_signal_runtime`,
`g16_shutdown_signal_test`, `g16_async_accept_context_test`,
`g11_async_task_scope_test`, `g16_service_telemetry_test`, the HTTP
qualification, and the three-program service-kit qualification.
