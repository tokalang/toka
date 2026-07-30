# Agent Service Vertical Slice

This is an application-level integration exercise, not `official/agent` and
not a new `std` abstraction.  It deliberately composes existing packages:

```text
POST /runs (Idempotency-Key)
  -> RedisPool idempotency read/write
  -> PostgreSQL transaction: run, message, tool audit
  -> mock provider or explicit HTTPS OpenAI-compatible request
  -> in-process NDJSON events at GET /runs/:id/events
  -> JSON event logs and Prometheus counters
```

The mock provider is the default qualification path and makes no network call
or use of credentials. `OpenAiCompatibleConfig` requires an explicit HTTPS URL,
resolved address, model, API key, and timeout; the key is placed only in the
outbound Authorization header and is never added to an event or log. The first
adapter sends a non-streaming `/v1/chat/completions` request. It intentionally
does not claim SSE/tool-call decoding or durable event replay.

PostgreSQL is expected to provide `agent_runs`, `agent_messages`, and
`agent_tool_audit` tables. The example writes all three through explicit,
short transactions.
It first persists the run and user message in a short transaction, waits for
the model outside a database transaction, then writes the assistant message,
model audit, and terminal run status in a second short transaction.
Redis atomically reserves an idempotency key with `SET ... NX` before any
database or provider work. A duplicate key is rejected in this first slice
rather than guessing whether a prior in-flight run can be resumed. The
reservation intentionally remains after a later failure: an indeterminate
operation must not be retried under the same key and execute a tool twice.
The reservation expires after 24 hours, making it deliberately short-term
state rather than a durable replay ledger.

`process_tool` accepts only an exact configured program and passes one argument
directly to `std/process::Command`—there is no shell. `http_tool_async` accepts
only a configured URL prefix. Neither policy should be treated as a sandbox.
In particular, `std/process` does not yet offer cancellation or a killable
deadline; therefore long-running process tools are deliberately not wired into
the run loop. That is a concrete gap exposed by this example.

NDJSON is chosen over WebSocket for this first slice so streaming progress does
not require a new server ownership model. Events are retained only in process;
restart recovery and SSE provider streaming are the next evidence-driven work,
not silently implied capabilities.

`serve_until_canceled_async` follows the data-access example's lifecycle
contract: cancel stops accepting first, its `TaskScope` drains request workers
to the supplied deadline, then the host calls `AgentService::close()` to drain
idle Redis/PostgreSQL pool members. A per-run `Canceler` is passed through the
provider and persistence path; hosts that need a deadline create it with
`std/context::with_timeout` before dispatch.

Cancellation is observed before start and between the data and provider
stages. The current generic `HttpClient` offers a timeout but not a
context-aware in-flight cancel operation, so an HTTPS request cannot yet be
interrupted mid-read. This is intentionally visible evidence for a future
provider-neutral client improvement, not an application-specific `stdx` API.

## Qualification

```sh
python3 examples/agent-service/tests/qualify.py
```

This credential-free qualification proves the public composition surface and
the deterministic OpenAI-compatible request/NDJSON encodings. A later eligible
loopback/Docker suite must verify the PostgreSQL and Redis path against the
declared schema and a mock HTTPS provider before calling this a deployment
reference.
