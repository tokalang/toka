# `official/openai_compat` v1

This incubating package owns the transport-neutral request, response, and
streaming semantics of the `/v1/chat/completions` OpenAI-compatible profile.
It builds request JSON from owned messages and tool definitions; decodes normal
completion JSON; and interprets the `data` field of an already-framed
`stdx/net/sse::SseEvent`:

```toka
import official/openai_compat::{ChatMessage, ChatRequest, OpenAiAssemblyLimits, OpenAiCompatLimits, StreamAssembler, decode_sse_event}

auto request# = ChatRequest::new("example-model").unwrap()
request#.add_message(cede ChatMessage::user("weather in Tokyo"))
request#.set_stream(true, true)
auto request_json = request#.to_json().unwrap()

auto events = decode_sse_event(cede sse_event,
    OpenAiCompatLimits::new(65536:usize, 32:usize).unwrap())

auto assembler# = StreamAssembler::new(
    cede OpenAiAssemblyLimits::new(262144:usize, 32:usize, 65536:usize).unwrap())
auto ordered_events# = events.unwrap()
auto semantic_event# = ordered_events#.pop().unwrap()
assembler#.push(semantic_event).unwrap()
// Push remaining ordered events, then obtain one owned assistant turn.
auto turn = assembler#.finish().unwrap()
```

Requests support owned `system`, `developer`, `user`, `assistant`, and tool
result messages, plus validated function-tool schemas. Normal completion JSON
decodes to `OpenAiCompletion::{AssistantTurn, ApiError}`. Streaming emits role,
text, refusal, indexed tool-call deltas, usage, completion records, and
structured provider API-error events. `[DONE]` is intentionally recognized
here—not in `stdx/net/sse`—because it is provider protocol data rather than an
SSE framing rule.

## Boundary

The package does not open HTTP connections, parse SSE, store credentials,
execute tools, or implement OpenAI's Responses API. The application owns the
HTTP client, timeout/cancellation context, endpoint, API key, and tool policy.
Malformed request schemas, JSON, and chat-completions shapes are typed errors;
a well-formed provider `{ "error": ... }` payload remains an `ApiError` event
so callers can audit it as provider output.

`OpenAiCompatLimits` bounds JSON bytes per completion/SSE event and tool calls
per decoded chunk. `OpenAiAssemblyLimits` additionally bounds accumulated
output, tool-call count, and tool-argument bytes. These limits are in addition
to the lower SSE line/event/buffer limits.

The in-repository source and locked-local package gates pass. The authoritative
scope and compatibility matrix are in
[`docs/official_openai_compat_v1.md`](../../docs/official_openai_compat_v1.md);
standalone extraction and public registry release remain later gates.

## Qualification

```sh
python3 official/openai_compat/tests/qualify_package.py
```

The fixture covers request encoding, normal and streamed completions, indexed
fragmented tool arguments, `[DONE]`, provider API errors, malformed JSON,
conflicting tool state, and configured bounds. It requires no credential,
network call, or model account.
