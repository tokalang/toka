# `official/openai_compat` v1

This incubating package owns the transport-neutral request and streaming
semantics of the `/v1/chat/completions` OpenAI-compatible profile. It builds
request JSON from owned messages and tool definitions, then interprets the
`data` field of an already-framed `stdx/net/sse::SseEvent`:

```toka
import official/openai_compat::{ChatMessage, ChatRequest, OpenAiCompatLimits, decode_sse_event}

auto request# = ChatRequest::new("example-model").unwrap()
request#.add_message(cede ChatMessage::user("weather in Tokyo"))
request#.set_stream(true, true)
auto request_json = request#.to_json().unwrap()

auto events = decode_sse_event(cede sse_event,
    OpenAiCompatLimits::new(65536:usize, 32:usize).unwrap())
```

Requests support owned `system`, `developer`, `user`, `assistant`, and tool
result messages, plus validated function-tool schemas. Streaming currently
emits text deltas, indexed tool-call deltas, completion records, and structured
provider API-error events. `[DONE]` is intentionally recognized here—not in
`stdx/net/sse`—because it is provider protocol data rather than an SSE framing
rule.

## Boundary

The package does not open HTTP connections, parse SSE, store credentials,
execute tools, or implement OpenAI's Responses API. The application owns the
HTTP client, timeout/cancellation context, endpoint, API key, and tool policy.
Malformed request schemas, JSON, and chat-completions shapes are typed errors;
a well-formed provider `{ "error": ... }` payload remains an `ApiError` event
so callers can audit it as provider output.

`OpenAiCompatLimits` bounds JSON bytes per SSE event and tool calls per chunk.
These limits are in addition to the lower SSE line/event/buffer limits.

The normal non-streaming response decoder and bounded stream assembler remain
v1 release gates. The authoritative scope and compatibility matrix are in
[`docs/official_openai_compat_v1.md`](../../docs/official_openai_compat_v1.md).

## Qualification

```sh
python3 official/openai_compat/tests/qualify_package.py
```

The fixture covers text, indexed tool-call argument deltas, `[DONE]`, provider
API errors, malformed JSON, and configured bounds. It requires no credential,
network call, or model account.
