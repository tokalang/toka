# `official/openai_compat` v1 — Chat Completions Protocol Package

Status: **approved implementation plan; not releasable until the acceptance
matrix below passes**.

`official/openai_compat` is an optional, transport-neutral package for the
OpenAI-compatible `/v1/chat/completions` protocol. It owns typed request and
response values, JSON encoding/decoding, streaming delta interpretation, and
bounded assembly of a completed assistant turn. It is not an HTTP client or an
agent framework.

This replaces the earlier streaming-decoder-only release target. A decoder is
useful implementation evidence, but is not by itself a credible official
package for developers building real AI applications.

## Boundary

The package accepts and produces owned protocol values. An application owns:

- endpoint selection, DNS/TLS, HTTP execution, credentials, retries,
  cancellation, and timeouts;
- model/provider policy and any provider-specific request extension;
- deciding whether a completed tool call is allowlisted and executing it;
- persistence, audit records, and conversation retention.

The package owns:

- text and tool-result chat messages and typed function-tool definitions;
- request JSON encoding and non-streaming completion decoding;
- chat-completions SSE data semantics, including `[DONE]`;
- role, text, refusal, tool-call, finish-reason, usage, and provider-error
  semantic events;
- a limit-bounded assembler that turns ordered deltas into one owned assistant
  turn without executing any tool.

Multimodal content parts, Responses API, Assistants API, batch APIs, provider
private reasoning fields, logprobs, and function-call legacy syntax are not
v1 compatibility claims. They require a separately scoped contract; unknown
fields must never silently become a supported semantic guarantee.

## Public organization

```text
lib/official/openai_compat.tk
  stable facade; re-exports the supported public surface

lib/official/openai_compat/types.tk
  errors, limits, usage, turns, and stream values

lib/official/openai_compat/request.tk
  messages, tools, request construction, and deterministic JSON encoding

lib/official/openai_compat/response.tk
  non-streaming completion and provider-error decoding

lib/official/openai_compat/stream.tk
  one completed SseEvent -> ordered semantic events

lib/official/openai_compat/assemble.tk
  bounded, ordered stream-delta -> completed assistant turn assembly
```

The root module uses existing `pub import` re-exports so consumers retain the
natural single import:

```toka
import official/openai_compat::{ChatRequest, StreamAssembler, decode_sse_event}
```

Submodule paths are implementation organization, not a second dialect or a
promise that every internal helper is public API.

## v1 compatibility matrix

| Protocol surface | v1 behavior |
|---|---|
| Request roles | `system`, `developer`, `user`, `assistant`, and `tool` are typed and encoded. |
| Request content | Owned text content and tool-result text are encoded; multimodal parts are rejected by scope rather than guessed. |
| Function tools | Name, optional description, and validated JSON-object parameter schema are encoded; execution is never owned. |
| Non-stream completion | Typed assistant text/refusal/tool calls, finish reason, and usage are decoded. |
| Streaming completion | Role, text, refusal, indexed tool-call deltas, finish reason, usage, `[DONE]`, and API-error payloads are interpreted in wire order. |
| Stream assembly | A caller pushes ordered semantic events; bounds prevent unbounded text, tool-call count, or tool-argument growth. Completion requires an explicit terminal event. |
| Malformed input | Invalid JSON, incompatible shape, invalid field type, duplicate/conflicting tool state, or an exceeded limit returns `OpenAiCompatError`. |
| Provider extensions | Not interpreted in v1. The decoder may ignore unknown fields only when doing so cannot hide malformed data for a supported field. |

## Acceptance gate

Before standalone extraction, the package must provide deterministic fixtures
for each matrix row, including request JSON, a normal non-stream response,
streamed text, refusal, fragmented tool arguments, usage, `[DONE]`, provider
API errors, malformed shapes, and all configured bounds. The
`examples/agent-service` fixture must construct a request through the package
and assemble a streamed assistant turn through it while keeping HTTP and tool
policy in the application.

After those checks pass, the normal standalone-package gate applies: source
checkout qualification, tagged archive digest, public registry consumer,
offline replay, and the retained compiler-repository consumer fixture.
