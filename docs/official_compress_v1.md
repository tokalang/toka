# `official/compress` v1 — Bounded Streaming Gzip/Zlib RFC

Status: **implemented and locally qualified through explicit native-link and
locked/offline package evidence; not yet published**.

## Role and boundary

`official/compress` is an optional package above `std`. It provides the narrow
transformation boundary missing from the bundled libraries: given owned chunks,
incrementally produce compressed or decompressed owned chunks. It owns neither
TCP streams, HTTP headers, files, archive containers, nor content-encoding
policy. `stdx/net/http` must not acquire a reverse dependency on it.

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
`native.libraries = ("zlib")`. The current resolver preserves this static
metadata but does not yet compile arbitrary native package sources as part of
`toka build`; v1 therefore has a reproducible explicit native qualification
path. This is an existing package-toolchain boundary, not a hidden requirement
or a reason to make base Toka programs link zlib.

Qualification proves:

1. `pkg-config zlib` can compile the declared C bridge;
2. public `import official/compress` links and runs with that bridge;
3. split-input Gzip and Zlib round trips work;
4. invalid, truncated, and expansion-limited input fails closed;
5. a local locked package is replayable in `TOKA_OFFLINE=1` before the native
   public-import test runs.

## Stop boundary

There is no implicit HTTP `Content-Encoding`, no `AsyncStream` wrapper, no
archive format, no raw DEFLATE, no concatenated-member policy, and no Brotli or
Zstd implementation. Those each need a separate format/policy decision and
their own resource contracts.
