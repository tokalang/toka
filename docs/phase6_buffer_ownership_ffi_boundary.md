# Phase 6 Buffer Ownership and FFI Boundary Contract

Status: **Implemented contract — owner-based protocol migration complete**

This document freezes the ownership and ABI contract for the zero-secondary-copy
network refactoring. It deliberately does not change the meaning of Toka raw
pointers or the existing compatibility APIs.

## 1. Layer boundary

The network stack has three boundaries:

```text
stdx/net (HTTP, WebSocket, TLS policy/orchestration)
        ↓ owned buffer API and opaque TLS session
std/net (TcpStream, TlsSession, async buffer adapter)
        ↓ private FFI call; pointer valid only for one I/O attempt
lib/sys + C runtime (recv/send/SSL_read/SSL_write/reactor)
```

Raw pointers are permitted in `lib/sys` and in the implementation of a
`std/net` adapter. The TLS read/write/close FFI declarations, reactor retry
loops, buffer-pointer derivation, and opaque `TlsSession` ownership now live in
`std/net`. `stdx/net` owns a `TlsSession`, never an OpenSSL handle, and delegates
all owner-based I/O to `std/net`.

`AsyncStream` retains pointer methods and `to_u8_ptr` as explicit
source-compatible legacy adapters for existing low-level callers. They are not
used by HTTP, WebSocket, or TLS policy code, and no new protocol API may depend
on them. In particular, no `stdx/net` value stores a raw data pointer or an
OpenSSL handle across an `.await`.

The existing pointer APIs (`read_async(*buf, len)`, `write_async(*buf, len)`)
remain source-compatible. They are legacy unsafe/FFI adapters and are not used
by new `stdx/net` code.

## 2. Buffer owner invariant

An asynchronous I/O operation has exactly one buffer owner at every point:

1. Before submission, the caller owns a mutable `Vec<u8>` buffer or a frozen
   `Bytes` buffer.
2. On submission, ownership is moved into the asynchronous operation's frame.
3. While suspended, the frame is the sole owner; no operation may resize,
   reallocate, or free the backing storage.
4. On completion, timeout, cancellation, or error, ownership is returned to the
   caller in the operation result, or explicitly dropped by a documented
   consuming API.

No owner-based API may return only a byte count after consuming an input buffer. A read
operation must return both the buffer owner and the number/status of bytes
read. A write operation must either return the owner or explicitly document
that the buffer is consumed on every outcome.

## 3. Required result shape

The implementation uses these owner-carrying Toka shapes:

```text
pub shape AsyncReadResult (
    buffer: Vec<u8>,       // still owned by the caller after return
    bytes_read: usize,
    eof: bool
)

pub shape AsyncWriteResult (
    buffer: Vec<u8>,       // returned for retry/drop decisions
    bytes_written: usize,
    complete: bool
)

pub shape AsyncIoError (
    buffer: Vec<u8>,       // owner is returned even on failure
    message: string
)
```

For a successful read, `buffer[0..bytes_read]` is initialized data. For EOF,
`eof == true` and `bytes_read == 0`. For timeout, cancellation, or I/O error,
the buffer is still returned and no uninitialized bytes may be exposed.

The adapter may internally set the vector length to its writable capacity before
calling the C function and restore the initialized length immediately after the
call. This internal length manipulation must not be observable across a suspend
point.

## 4. Pointer validity rule

Within `std/net`, the only valid lifetime for a data pointer is:

```text
derive pointer → invoke one sys/FFI operation → use returned count → discard pointer
```

The pointer may not be stored in `AsyncStream`, `TlsSession`,
`HttpResponseStream`, a header view, a WebSocket frame state, or any coroutine
field. A pointer must be
re-derived after every `.await` and after every operation that may resize a
buffer.

Reactor control pointers (for example an output wait-key) follow the same rule.
The next adapter revision will replace pointer out-parameters with value-returning
sys functions where practical.

## 5. Public API contract

New APIs are owner-based and are the only APIs allowed for new protocol code:

```text
TcpStream::read_into_async(cede owner#: Vec<u8>, max_len: usize, timeout_ms: i32)
    -> async Result<AsyncReadResult, AsyncIoError>
TcpStream::write_from_async(cede owner#: Vec<u8>, timeout_ms: i32)
    -> async Result<AsyncWriteResult, AsyncIoError>
AsyncStream::read_into_async(cede owner#: Vec<u8>, max_len: usize, timeout_ms: i32)
    -> async Result<AsyncReadResult, AsyncIoError>
AsyncStream::write_from_async(cede owner#: Vec<u8>, timeout_ms: i32)
    -> async Result<AsyncWriteResult, AsyncIoError>
```

`read_into_async` returns an owner-carrying read result. `write_from_async`
returns an owner-carrying write result so a partial write can be retried or the
remaining owner can be dropped deliberately. The old pointer APIs remain
compatibility-only and are not called from `stdx/net`.

`Bytes::from_vec` freezes a completed mutable buffer without copying it,
`Bytes::into_vec` thaws it without copying it, and `Bytes::as_slice` exposes a
borrowed `bytes` view. This deliberately keeps incremental parsers on
`Vec<u8>` while allowing completed binary payloads to be transferred or retained
without a secondary allocation.

Because `TcpStream`/`AsyncStream` methods use a mutable identity receiver
(`self#`), these owner-based methods are intended to be awaited on the owning
stream. They must not be started as an independent task while borrowing a
caller-local stream. A spawned operation must first move the stream into the
task (`cede`) and consume it there.

## 6. View lifetime

An offset view is valid only while its owner remains live. Therefore an
`HttpHeaderView` stores offsets and lengths, while the containing response keeps
the owning header buffer alive. A view must not be returned independently of its
owner and must not outlive the response/stream that owns the buffer.

## 7. Verification gates

The implementation is accepted only while these gates remain green:

- `g13_net_buffer_abi_test.tk` proves owner return on closed-stream errors;
- `g12_stdx_http_client_server_test.tk` proves incremental owner-based reads
  and writes through HTTP chunked streaming;
- TLS and HTTPS/WSS qualification exercise the same owner-based API through
  `TlsSession`;
- a static search must show no `unsafe_get`, TLS FFI declaration, or stored
  raw TLS handle in `stdx/net` protocol code;
- `g12` and `g13` must compile only after these gates pass.
