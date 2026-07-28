# `official/compress` v1

`official/compress` is an opt-in zlib-backed package for streaming Gzip and
zlib encoding/decoding. It is not an HTTP policy layer, archive library, or
framework; callers decide where compressed bytes come from and go.

```toka
import official/compress::{Decoder, Encoder}

auto encoder# = Encoder::gzip(-1).unwrap()
auto first = encoder#.write(cede input_chunk).unwrap()
auto trailer = encoder#.finish().unwrap()

auto decoder# = Decoder::gzip(64 * 1024 * 1024).unwrap()
auto plain = decoder#.write(cede compressed_chunk).unwrap()
auto final_plain = decoder#.finish().unwrap()
```

Each `write` consumes one owned `Bytes` chunk and returns only the bytes
produced by that step. `finish` must be called once to flush an encoder or
validate a decoder trailer; it closes the handle on both success and failure.
The decoder requires an explicit total output limit, so compressed input never
silently expands into an unbounded allocation.

The package uses zlib only in its private C boundary. Its public API exposes no
native handle or raw pointer. `toka build` compiles the declared bridge and
links zlib automatically for a locked `official/compress` consumer; base Toka
programs do not acquire a zlib dependency.

v1 deliberately excludes HTTP content-encoding policy, stream adapters,
concatenated gzip members, raw DEFLATE, archive containers, Brotli, Zstd, and
automatic retry/recovery after a malformed compressed stream.

## Qualification

Run from this package root:

```text
python3 tests/qualify_package.py
```
