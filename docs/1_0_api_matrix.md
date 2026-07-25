# Toka 1.0 Five-Tier API Stability Matrix

**Status**: Qualification Layer Classification  
**Purpose**: Categorize interfaces across `core`, `std`, and `stdx` into five distinct stability tiers for Toka 1.0.

---

## 1. Stability Tier Definitions

1. **Tier 1: Language 1.0**: Core syntax, ownership mechanics (`borrow`, `cede`, `move`), value/handle sigils (`#`, `$`), postfix `!`, and async function morphology (`async`, `.await`, `.start`).
2. **Tier 2: Core / Std 1.0**: Fundamental runtime data structures (`Vec`, `Bytes`, `string`, `str`), `Option`/`Result`, `@Iterable`/`@Iterator` traits, `File`/`Reader`/`Writer`, `Task`/`Context`.
3. **Tier 3: Stdx 1.0**: Standard extensions module (`HttpRequest`/`HttpResponseStream`, `HttpHeaderView`, TLS stream adapter, WebSocket framing).
4. **Tier 4: Legacy / Deprecated Shims**: Pre-1.0 C-style raw pointer APIs (`read_async(*buf, len)`, `to_u8_ptr`). Retained temporarily for low-level compatibility; NO new implementation in `stdx` may rely on Legacy APIs.
5. **Tier 5: Experimental / Post-1.0**: Parameterized task spawning, connection pools, HTTP/2, structured task scopes, advanced async combinators.

---

## 2. API Classification Matrix

| Module / Subject | API / Feature Surface | Tier | Safety Contract & Scope Notes |
|---|---|---|---|
| **Syntax & Ownership** | `borrow`, `cede`, `move`, `~T`, `#`, `$` | **Tier 1: Language 1.0** | Core permission and transfer model. |
| **Error Handling** | Postfix `!`, `Option<T>`, `Result<T, E>` | **Tier 1: Language 1.0** | Early return cleanup and fallible computation. |
| **Async Mechanics** | `fn -> async T`, `.await`, `.wait`, `.start` | **Tier 1: Language 1.0** | Async function invocation & task start. |
| **`core/string.tk`** | `string`, `str`, `bytes`, `from`, `as_str` | **Tier 2: Core/Std 1.0** | Safe owned and slice UTF-8 strings. |
| **`std/bytes.tk`** | `Bytes::from_vec`, `b#.into_vec()`, `as_slice` | **Tier 2: Core/Std 1.0** | Owner-carrying buffer stealing container. |
| **`std/net.tk`** | `TcpStream`, `TcpListener`, `connect_async_timeout` | **Tier 2: Core/Std 1.0** | Async TCP socket I/O engine. |
| **`std/io.tk`** | `File::open`, `read_to_string()`, `write()`, `close()` | **Tier 2: Core/Std 1.0** | Synchronous file I/O operations. |
| **`stdx/net/http.tk`** | `HttpRequest`, `HttpResponse`, `HttpResponseStream` | **Tier 3: Stdx 1.0** | Owner-carrying HTTP/1.1 framing. |
| **`stdx/net/http.tk`** | `HttpHeaderView`, `name()`, `value()` | **Tier 3: Stdx 1.0** | Non-owning borrowed offset views. |
| **`stdx/net/websocket.tk`** | `WebSocketFrame`, `WsStream` | **Tier 3: Stdx 1.0** | WebSocket async frame processing. |
| **Legacy Shims** | `read_async(*buf, len)`, `to_u8_ptr` | **Tier 4: Legacy** | Retained as compatibility shims; new stdx code prohibited from depending on them. |
| **Async Extensions** | Async blocks, TaskScope, connection pools | **Tier 5: Experimental** | Deferred for post-1.0 design. |
