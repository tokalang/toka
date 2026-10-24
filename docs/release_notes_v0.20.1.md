# Toka 0.20.1 Release Notes

Toka 0.20.1 is a semantic safety-boundary closure release on the `0.20` line enforcing explicit `unsafe` (`error[E0625]`) on all bare-name raw pointer pointee reads, writes, compound assignments, member/index/tuple projections, method calls, indirect function calls, `.await`/`.wait`, and multi-layer raw pointer peels (`**T -> *T`, `*?T -> ?T`), while preserving safe handle-identity operations (`*p`, `?q`, `**pp`, `*#p = *#b`, `?#q = null`, `guard ?q`, `(?q).is_null()`, `*p == *q`, `*p as Addr`) and safe managed handles (`&T`, `~T`, `^T`) without `unsafe`.

（Toka 0.20.1 是 `0.20` 系列的裸指针安全边界收口版本，对裸指针去帽触达目标内存的全部路径——裸名读取、写入、复合赋值、字段/下标/元组投影、方法调用、函数指针间接调用、`.await`/`.wait` 及多层裸指针剥离（`**T -> *T`、`*?T -> ?T`）——统一强制要求显式 `unsafe` 上下文（违者报 `error[E0625]`），同时保持带帽句柄身份传递与重绑定（`*p`、`?q`、`*#p = *#b`、`?#q = null`、`guard ?q`）及受管安全句柄（`&T`、`~T`、`^T`）在安全上下文中零摩擦可用。）

## Key Changes

### 1. Unified Raw Pointer Pointee `unsafe` Gate (`E0625`)
- **Single-Rooted Semantic Gate**: Enforces `error[E0625]` (`Raw pointer pointee access requires an 'unsafe' block or expression; keep the pointer hat to pass or rebind the raw pointer handle itself`) at the 1-layer bare-name raw pointer peel sites in `Sema` (`checkIdentifierExpr`, `checkUnaryExpr`, `checkPrefixHatChainExpr`, `resolveAssignmentTarget`, and `checkMemberExpr`).
- **Complete Path Coverage**: Covers bare-name value reads (`return p`, `auto x = p`), direct and compound writes (`p = 1`, `p += 1`), access paths (`p.field`, `p.field = 1`, `p[i]`, `p.0`, `p.method()`), indirect callable/async operations through raw pointers (`fp()`, `p.await`, `p.wait`), and multi-layer 1-layer peels (`**pp -> pp`, `*?#pq -> pq = null`, `self.pp`).
- **Safe Handle Identity Preserved**: Preserves `unsafe`-free handle-identity operations whenever the raw pointer hat is retained (`*p`, `?q`, `**pp`, `*#p = *#b`, `?#q = null`, `guard ?q { ... }`, `if ?q != null`, `(?q).is_null()`, `(?q).is_not_null()`, `*p == *q`, `*p as Addr`), as well as all safe managed handles (`&T`, `~T`, `^T`).

### 2. Sourceless `.tki` Replay & Cache Isolation (`0.20.1-1`)
- **Toolchain Interface Version Bump**: Updates `TOKA_COMPILER_INTERFACE_VERSION` to `"0.20.1-1"` (`Format Version 7`, `language_epoch: 0.20`, `encap_epoch: 0.19`), invalidating stale pre-`0.20.1` `.tki` caches and ensuring sourceless generic/inline bodies in `.tki` files undergo identical `E0625` verification upon instantiation.

## Release & Platform Policy
- **Core Blocking Platforms (Tier 1)**: `linux-x64`, `linux-arm64`, and `macos-arm64`.
- **Continuous Dogfood Target (Tier 2)**: `windows-x64` (MSYS2 / MinGW-w64 Clang SDK path).
- **Qualification Contract**: Conforms to `toka.release-platforms.0.20.v1` with 82/82 installed SDK candidate control receipts and full CTest regression verification.
