# `official/compress` v1.1 — Bounded Streaming Gzip/Zlib and HTTP Policy

Status: **implemented and locally qualified through explicit native-link and
locked/offline package evidence; not yet published**.

## Role and boundary

`official/compress` is an optional package above `std`. It provides the narrow
transformation boundary missing from the bundled libraries: given owned chunks,
incrementally produce compressed or decompressed owned chunks. Its optional
`official/compress/http` module may compose the bundled HTTP value types, but
`stdx/net/http` must not acquire a reverse dependency on it. The base module
still owns neither TCP streams, HTTP headers, files, nor archive containers.

The package is intentionally native because zlib is a mature, audited format
implementation with platform packages on the supported targets. The public
Toka surface never exposes zlib structures or pointers. The package-private C
bridge is the sole FFI boundary; it transfers result allocations once into
`std::Bytes` through its documented unsafe native-adoption adapter.

## v1 API and ownership contract

```toka
auto encoder# = Encoder::gzip(-1).unwrap()
auto wire = encoder#.write(cede source_chunk).unwrap()
auto final_wire = encoder#.finish().unwrap()

auto decoder# = Decoder::gzip(64 * 1024 * 1024).unwrap()
auto plain = decoder#.write(cede wire_chunk).unwrap()
auto final_plain = decoder#.finish().unwrap()
```

- `Encoder::{gzip,zlib}` accept zlib levels `-1..9`.
- `Decoder::{gzip,zlib}` require a positive maximum total decompressed byte
  count. This is a caller-owned resource policy, not a hidden global default.
- `write` consumes one `Bytes` input and returns a distinct owned `Bytes`
  output. Empty output is valid for a streaming step.
- `finish` is the sole flush/validation point, returns final output, and closes
  the object. Any failure also closes it, preventing reuse of partial state.
- Gzip and zlib are separate, explicit formats. v1 rejects trailing bytes and
  concatenated gzip members rather than guessing a continuation policy.

`CompressError` is structured as `(kind, code, message)` and distinguishes
invalid setup, closed streams, allocation failure, malformed data, truncated
input, unsupported trailing bytes, and output-limit violations.

## Native and package contract

`package.tk` declares `native.required`, `native.sources`, and
`native.libraries = ("zlib")`. `toka build` verifies those fields against the
locked package source, compiles the private bridge, and links zlib only for a
consumer that imports the locked package. This is not a hidden base-runtime
dependency: programs without a native package remain free of zlib and its
toolchain requirements.

Qualification proves:

1. `pkg-config zlib` can compile the declared C bridge;
2. public `import official/compress` builds and runs through `toka build` with
   that bridge;
3. split-input Gzip and Zlib round trips work;
4. invalid, truncated, and expansion-limited input fails closed;
5. `official/compress/http` negotiates, encodes, and explicitly decodes
   complete bodies with malformed, truncated, output-limit, ratio-limit, and
   duplicate-header normalization redlines;
6. a local locked package is replayable in `TOKA_OFFLINE=1` before the native
   public-import test runs;
7. a `stdx/net/http`-only consumer builds with zlib discovery intentionally
   unavailable.

## v1.1 optional HTTP Content-Encoding policy

```toka
import official/compress/http::{GzipRequestLimits, decode_gzip_request_body,
                                encode_response_for_request}

auto encoded = encode_response_for_request(
    cede response,
    request.headers.get("accept-encoding"),
    -1
).unwrap()

auto limits = GzipRequestLimits::new(8 * 1024 * 1024, 32).unwrap()
auto request = decode_gzip_request_body(cede gzip_request, limits).unwrap()
```

The HTTP module is a response/request policy layer, not an HTTP server hook:

- Response negotiation supports only `gzip` and `identity`. A missing
  `Accept-Encoding` chooses gzip; an empty field chooses identity. Explicit
  values override `*`, repeated codings retain their highest valid `q`, and a
  tie selects gzip deterministically. If both supported representations have
  `q=0`, the result is `NotAcceptable` rather than a silent fallback.
- `gzip_response` and `encode_response_for_request` finish the encoder before
  constructing the response. They write one `Content-Encoding: gzip`, one
  `Vary: Accept-Encoding`, and the matching `Content-Length`; a pre-encoded
  non-identity response is rejected rather than compressed twice.
- `decode_gzip_request_body` is explicit and accepts only
  `Content-Encoding: gzip`. It completes decoder validation, removes the
  coding/transfer framing headers, and writes the decoded `Content-Length`.
  `GzipRequestLimits` requires both a positive total decoded-byte limit and a
  positive decoded-to-compressed ratio limit.

HTTP header names remain case-insensitive; `HeaderMap` normalizes stored names
to lowercase. The policy does not automatically decode server requests or
alter `stdx/net/http` behavior, leaving status codes, admission policy, and
streaming strategy with the application.

## Stop boundary

There is no implicit HTTP `Content-Encoding`, no HTTP-core dependency, no
`AsyncStream` wrapper, no archive format, no raw DEFLATE, no concatenated-
member policy, and no Brotli or Zstd implementation. Those each need a
separate format/policy decision and their own resource contracts.
