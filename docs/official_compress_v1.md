# `official/compress` v1.2 — Bounded Streaming Gzip/Zlib/Zstd and HTTP Policy

Status: **implemented and locally qualified through explicit native-link and
locked/offline package evidence; not yet published**.

## Role and boundary

`official/compress` is an optional package above `std`. It provides the narrow
transformation boundary missing from the bundled libraries: given owned chunks,
incrementally produce compressed or decompressed owned chunks. Its optional
`official/compress/http` module may compose the bundled HTTP value types, but
`stdx/net/http` must not acquire a reverse dependency on it. The base module
still owns neither TCP streams, HTTP headers, files, nor archive containers.

The package is intentionally native because zlib and libzstd (>= 1.4.0) are mature, audited format
implementations with platform packages on the supported targets. The public
Toka surface never exposes zlib/zstd structures or pointers. Package-private C
bridges are the sole FFI boundary; they transfer result allocations once into
`std::Bytes` through documented unsafe native-adoption adapters.

## v1 API and ownership contract

```toka
auto encoder# = Encoder::gzip(-1).unwrap()
auto wire = encoder#.write(cede source_chunk).unwrap()
auto final_wire = encoder#.finish().unwrap()

auto decoder# = Decoder::zstd(64 * 1024 * 1024).unwrap()
auto plain = decoder#.write(cede wire_chunk).unwrap()
auto final_plain = decoder#.finish().unwrap()
```

- `Encoder::{gzip,zlib}` accept zlib levels `-1..9`. `Encoder::zstd` accepts Zstd level `-1` or `0` for default (level 3), or `1..22`.
- `Decoder::{gzip,zlib,zstd}` require a positive maximum total decompressed byte
  count (`max_output_bytes`). This is a caller-owned resource policy, not a hidden global default. Zstd decoders additionally enforce a fixed 128MB maximum window memory ceiling (`ZSTD_d_windowLogMax = 27`).
- `write` consumes one `Bytes` input and returns a distinct owned `Bytes`
  output. Empty output is valid for a streaming step.
- `finish` is the sole flush/validation point, returns final output, and closes
  the object. Any failure also closes it, preventing reuse of partial state.
- Gzip, Zlib, and Zstd are separate, explicit formats. The package rejects trailing bytes and
  concatenated frames rather than guessing a continuation policy.

`CompressError` is structured as `(kind, code, message)` and distinguishes
invalid setup, closed streams, allocation failure, malformed data, truncated
input, unsupported trailing bytes, and output-limit violations.

## Native and package contract

`package.tk` declares `native.required`, shared `native.sources`, and a
macOS/Linux `pkg_config = ("zlib", "libzstd")` target block requiring `libzstd >= 1.4.0`. `toka build` verifies those
fields against the locked package source, compiles the private bridges, and
links zlib and libzstd only for a consumer that imports the locked package. This is not a
hidden base-runtime dependency: programs without a native package remain free
of zlib/zstd and their toolchain requirements.

Qualification proves:

1. `pkg-config zlib` and `pkg-config --atleast-version=1.4.0 libzstd` can compile the declared C bridges;
2. public `import official/compress` builds and runs through `toka build` with
   those bridges;
3. split-input Gzip, Zlib, and Zstd round trips work across levels (including default `-1`/`0`, `1`, `19`, `22`);
4. invalid, truncated, checksum-corrupted, trailing-byte, and expansion-limited input fails closed;
5. `official/compress/http` negotiates, encodes, and explicitly decodes
   complete bodies with malformed, truncated, output-limit, ratio-limit, and
   duplicate-header normalization redlines;
6. a local locked package is replayable in `TOKA_OFFLINE=1` before the native
   public-import test runs;
7. a `stdx/net/http`-only consumer builds without zlib/zstd discovery, and negative linkage checks verify no unimported binary linkage to `libzstd`.

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
member policy, and no Brotli implementation. Those each need a
separate format/policy decision and their own resource contracts.
