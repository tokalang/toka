# `official/compress` v1.2

`official/compress` is an opt-in zlib/libzstd-backed package for streaming
Gzip, Zlib, and Zstd encoding/decoding. Its optional
`official/compress/http` module provides an explicit HTTP `Content-Encoding`
policy; it does not alter the HTTP core, act as an archive library, or become
a framework.

```toka
import official/compress::{Decoder, Encoder}

auto encoder# = Encoder::gzip(-1).unwrap()
auto first = encoder#.write(cede input_chunk).unwrap()
auto trailer = encoder#.finish().unwrap()

auto decoder# = Decoder::zstd(64 * 1024 * 1024).unwrap()
auto plain = decoder#.write(cede compressed_chunk).unwrap()
auto final_plain = decoder#.finish().unwrap()
```

Each `write` consumes one owned `Bytes` chunk and returns only the bytes
produced by that step. `finish` must be called once to flush an encoder or
validate a decoder trailer; it closes the handle on both success and failure.
The decoder requires an explicit total output limit, so compressed input never
silently expands into an unbounded allocation. Zstd decoding additionally
enforces a fixed 128 MiB maximum window-memory ceiling.

The package uses zlib and libzstd (>= 1.4.0) only in its private C boundaries.
Its public API exposes no native handle or raw pointer. `toka build` compiles
the declared bridges and links both libraries automatically for a locked
`official/compress` consumer; base Toka programs do not acquire either native
dependency.

The base streaming module deliberately excludes automatic HTTP handling, stream
adapters, concatenated frames, raw DEFLATE, archive containers, Brotli, and
automatic retry/recovery after a malformed compressed stream.

## Optional HTTP policy

```toka
import official/compress/http::{GzipRequestLimits, decode_gzip_request_body,
                                encode_response_for_request}

auto response = encode_response_for_request(
    cede response,
    request.headers.get("accept-encoding"),
    -1
).unwrap()

auto limits = GzipRequestLimits::new(8 * 1024 * 1024, 32).unwrap()
auto request = decode_gzip_request_body(cede gzip_request, limits).unwrap()
```

The policy negotiates `gzip` or `identity` from `Accept-Encoding`, including
`q` values and `*`. A gzip response is finished before it gets its
`Content-Encoding`, `Vary`, and `Content-Length` headers. Request decompression
is never automatic: callers explicitly invoke it with both decoded-byte and
compression-ratio limits. Malformed, truncated, oversized, or over-expanded
gzip fails closed.

The base `stdx/net/http` package remains independent of zlib and libzstd.
Import this module only in applications that choose the compression policy.

## Qualification

Run from this package root:

```text
python3 tests/qualify_package.py
```
