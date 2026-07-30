# `official/openai_compat` v1

This package interprets the `data` field of an already-framed
`stdx/net/sse::SseEvent` as the streaming
`/v1/chat/completions` OpenAI-compatible profile:

```toka
import official/openai_compat::{OpenAiCompatLimits, decode_sse_event}

auto events = decode_sse_event(cede sse_event,
    OpenAiCompatLimits::new(65536:usize, 32:usize).unwrap())
```

It emits text deltas, indexed tool-call deltas, completion records, and
structured provider API-error events. `[DONE]` is intentionally recognized
here—not in `stdx/net/sse`—because it is provider protocol data rather than an
SSE framing rule.

## Boundary

The package does not open HTTP connections, parse SSE, store credentials,
execute tools, or implement OpenAI's Responses API. The application owns the
HTTP client, timeout/cancellation context, endpoint, API key, and tool policy.
Malformed JSON and malformed chat-completions shapes are typed decode errors;
a well-formed provider `{ "error": ... }` payload remains an `ApiError` event
so callers can audit it as provider output.

`OpenAiCompatLimits` bounds JSON bytes per SSE event and tool calls per chunk.
These limits are in addition to the lower SSE line/event/buffer limits.

## Qualification

```sh
python3 official/openai_compat/tests/qualify_package.py
```

The fixture covers text, indexed tool-call argument deltas, `[DONE]`, provider
API errors, malformed JSON, and configured bounds. It requires no credential,
network call, or model account.
