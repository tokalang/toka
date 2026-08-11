# `official/openai_compat` v1 package contract

Status: incubating standalone-release candidate

## Purpose

`official/openai_compat` owns the transport-neutral protocol surface of the
OpenAI-compatible `/v1/chat/completions` profile. It encodes owned requests,
decodes normal completion payloads, turns already-framed SSE data into ordered
semantic events, and assembles those events into one owned assistant turn.

The application retains authority over HTTP transport, credential storage,
endpoint selection, timeouts, cancellation, and tool execution.

## Compatibility boundary

v1 supports:

- `system`, `developer`, `user`, `assistant`, and tool-result messages;
- validated function-tool schemas and tool choices;
- non-streaming assistant turns and provider API-error payloads;
- streaming role, text, refusal, usage, completion, indexed tool-call deltas,
  and provider API-error events;
- the provider `[DONE]` sentinel, after generic SSE framing.

The package deliberately excludes HTTP/TLS, SSE framing, the Responses API,
credential handling, tool execution, and provider-specific extensions outside
this profile.

## Resource and failure contract

All protocol values and decoder outputs are owned. Request construction and
stream assembly consume their inputs explicitly. Malformed request schemas,
JSON, completion shapes, conflicting stream state, and configured bounds are
typed errors. A well-formed provider error payload is represented as an
`ApiError` value so callers can keep provider output visible in their own
audit trail.

`OpenAiCompatLimits` bounds JSON bytes per completion or SSE event and tool
calls per decoded chunk. `OpenAiAssemblyLimits` additionally bounds accumulated
output, tool-call count, and tool-argument bytes. These are supplemental to
the limits enforced by `stdx/net/sse`.

## Qualification

Run `tests/qualify_package.py`. In the monorepo it discovers the adjacent Toka
toolchain. In a standalone checkout set `TOKA_ROOT` to a built Toka source
checkout. The gate exercises direct compilation, locked local dependency
resolution, offline lock replay, and public-import build/run behavior without
network calls or a model account.
