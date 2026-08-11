# `official/openai_compat` v1 — Chat Completions Protocol Package

Status: **released v1**. The canonical source is
[`tokalang/openai_compat`](https://github.com/tokalang/openai_compat), whose
first registry-eligible release is
[`v0.1.1`](https://github.com/tokalang/openai_compat/releases/tag/v0.1.1).
It passed standalone Linux/macOS qualification and fresh public-registry plus
offline-consumer replay. The preceding `v0.1.0` source snapshot is intentionally
not a catalog release because its archive did not use the required package-root
layout.

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
| Non-stream completion | `decode_completion_json` returns either a typed assistant text/refusal/tool-call turn or a structured provider API error. Exactly one choice at index `0` is supported. |
| Streaming completion | Role, text, refusal, indexed tool-call deltas, finish reason, usage, `[DONE]`, and API-error payloads are interpreted in wire order. A chunk has at most one choice; usage-only terminal chunks may have no choice. |
| Stream assembly | A caller lends ordered semantic events to `StreamAssembler::push`; the assembler copies only the state it retains. Bounds prevent unbounded text, tool-call count, or tool-argument growth. Completion requires an explicit terminal event. |
| Malformed input | Invalid JSON, incompatible shape, invalid field type, duplicate/conflicting tool state, or an exceeded limit returns `OpenAiCompatError`. |
| Provider extensions | Not interpreted in v1. The decoder may ignore unknown fields only when doing so cannot hide malformed data for a supported field. |

## Acceptance gate

The in-repository qualification now covers every matrix row: request JSON, a
normal non-stream response, streamed text, refusal, fragmented tool arguments,
usage, `[DONE]`, provider API errors, malformed shapes, conflicting tool
identity, and configured assembly bounds. The `examples/agent-service` fixture
constructs its request and assembles its streamed turn through this package
while retaining HTTP and tool policy in the application.

After those checks pass, the normal standalone-package gate applies: source
checkout qualification, tagged archive digest, public registry consumer,
offline replay, and the retained compiler-repository consumer fixture.
