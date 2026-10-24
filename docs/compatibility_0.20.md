# Toka 0.20.0 / 0.20.1 Compatibility Notes, Migration Guide & SDK Acceptance Checklist

## 1. Release Positioning & Breaking Contract Overview

- **Breaking Raw Pointer Hat Contract Migration (`0.19` -> `0.20` Language Epoch)**:
  - Toka `0.20.0` / `0.20.1` introduces `TOKA_DEFAULT_LANGUAGE_EPOCH = "0.20"` (`include/toka/InterfaceVersion.h`), bumps `TOKA_INTERFACE_FORMAT_VERSION` from `"6"` to `"7"`, and updates `TOKA_COMPILER_INTERFACE_VERSION` to `"0.20.1-1"`, while retaining `TOKA_DEFAULT_ENCAP_EPOCH = "0.19"`.
  - In `0.20.1`, any expression or statement that **peels a raw pointer layer (`*` or narrowed `?`) to read, write, project, index, or call its pointee** (`p`, `p = v`, `p += 1`, `p.field`, `p[i]`, `p.0`, `p.method()`, `fp()`, `p.await`, `p.wait`, or multi-layer peel `**pp -> pp`) requires an explicit `unsafe { ... }` block or `unsafe <expr>`, and is otherwise rejected with `error[E0625]`. Operations that preserve the raw pointer handle identity (`*p`, `?q`, `**pp`, `*#p = *#b`, `?#q = null`, `guard ?q`, `(?q).is_null()`, `*p == *q`, `*p as Addr`) remain safe without `unsafe`.
  - The legacy two-token nullable raw pointer spelling `nul *` (`nul *T`, `nul *p`) is retired and rejected with `error[E0488]`.
  - Single-character `?` is now the canonical nullable raw pointer hat (`?T`, `?p`), symmetric with non-null `*` (`*T`, `*p`).
  - Pure raw pointer multi-layer hat chains (`**T`, `*?T`, `?*T`, `??T`, `***T`..`???T`) with per-layer `#`/`$` rebinding permissions (`?#T`, `*#?T`, `?#?p`, `?*#p`) are supported across local bindings, parameters, returns, `shape` fields, member access (`self.?next`, `self.?#next`, `self.??pp`), and constructor punning (`Node(?next = null)`).
  - Bare-name access on a pure raw pointer binding (`p`) now performs **strictly 1-layer projection** (peeling only the outermost raw pointer layer), separating declared layer vectors (`DeclaredLayers`), expression value types (`ValueType`), and flow-sensitive non-null narrowing facts (`NonZeroFact`).
  - Temporary `--encap-epoch=0.18` CLI support (scheduled for retirement in `0.20+` by `0.19.0`) is removed.
- **No `1.0` Stability or Cross-Version ABI Claim**:
  - Toka `0.20.0` is an iterative pre-1.0 development release on the `0.2x` line and does not provide cross-version binary (`.o`) or interface (`.tki`) compatibility with `0.19.x` or earlier releases.
- **Preserved Language & Runtime Invariants**:
  - **Encapsulation & Visibility (`encap_epoch: 0.19`)**: Default-private `shape` fields, inline `pub`/`pkg` field visibility modifiers, authoritative package identity (`R1` / `PLAN-1`), and effective visibility closure (`PLAN-2`) from `0.19.0` are unchanged.
  - **Safe Handle Non-Nullability**: Safe owning and borrowing handles (`^T`, `~T`, `&T`) remain strictly non-null; `T?` (`error[E0484]`), `nul ^T` / `nul ~T` (`error[E0485]`), `none` (`error[E0486]`), and postfix `p?` / `p??` (`error[E0487]`) remain permanently rejected.
  - **Async Cancellation Suffix**: Postfix `.await?` remains the dedicated async cancellation-outcome operator and is unaffected by prefix `?` raw pointer hats.

---

## 2. Source Migration Guide (`0.19.0` -> `0.20.0`)

### 2.1 Migrating `nul *` Declarations, Member Access & Multi-Layer Raw Chains

Replace all `nul *` type, binding, parameter, field, and member-access hats with `?`, and update multi-layer raw pointer bindings to use 1-layer bare-name projection:

```toka
// BEFORE (0.19.0)
extern fn libc_malloc(size: usize) -> nul *void
extern fn libc_free(nul *ptr: void) -> void

pub shape RawNode(
    pub val: i32,
    pub nul *#next: RawNode
)

fn advance(nul *#cursor: RawNode) {
    guard *cursor {
        *cursor = cursor.*next
    } else {
        return
    }
}

// AFTER (0.20.0)
extern fn libc_malloc(size: usize) -> ?void
extern fn libc_free(?ptr: void) -> void

pub shape RawNode(
    pub val: i32,
    pub ?#next: RawNode
)

fn advance(?#cursor: RawNode) {
    guard ?cursor {
        ?#cursor = cursor.?next
    } else {
        return
    }
}
```

### 2.2 Three-Way Separation: `DeclaredLayers`, `ValueType`, and `NonZeroFact`

In `0.20.0`, raw pointer semantics strictly separate three dimensions:

1. **1-Layer Bare-Name Projection (`ValueType`)**:
   - Given `auto *?#pp: i32 = ...` (outer non-null `*`, inner rebindable nullable `?#`, payload `i32`):
     - `*?pp` selects the **2-layer handle** (`*?i32`).
     - `pp` (bare name) peels **1 layer**, selecting the **inner 1-layer nullable handle** (`?i32`). Writing `pp = null` or `pp = *target` directly mutates that inner `?i32` slot without requiring an unwrap (`R-1`).
     - Binding the inner pointer to a 1-layer variable (`auto ?inner = pp`) and narrowing it (`guard ?inner { ... }`) allows reading or writing the `i32` payload via `inner` inside `unsafe`.
2. **Flow-Sensitive 1-Layer Narrowing (`NonZeroFact`, `R-4` / `R-5`)**:
   - `guard ?p { ... } else { ... }` and `if ?p != null { ... }` establish a `NonZeroFact` on the **outermost layer only**:
     - For `auto ?p: RawNode`, `guard ?p { ... }` narrows the outer layer to `*RawNode` inside the guarded block, enabling `p.val` access.
     - For `auto ??pp: i32`, `guard ??pp { auto ?inner = pp; guard ?inner { ... } }` narrows one layer at a time: `guard ??pp` narrows only the outer layer (`??i32 -> *?i32`), `auto ?inner = pp` projects the inner `?i32` layer via 1-layer bare-name access, and `guard ?inner` narrows that inner pointer before reading `inner` inside `unsafe`.
   - `(?p).unwrap()` produces a non-null `*R` **return value** for that expression only; it does **not** mutate or narrow the source binding `?p` in subsequent statements (`R-5`).
3. **Three-Way `NonZeroFact` Invalidation**:
   - Any active `NonZeroFact` on a binding path is immediately invalidated by:
     1. Direct handle rebinding (`?#p = ...`, `??#pp = ...`),
     2. Address-taking or mutable borrow/alias writes that can overwrite the slot, or
     3. Passing the rebindable handle (`?#p`) to a call that may mutate the caller's slot.

### 2.3 Security Invariants & Diagnostic Reference

| Rule / Boundary | Surface Example | Outcome & Diagnostic |
| :--- | :--- | :--- |
| **Raw Pointee Access Without `unsafe` (`0.20.1`)** | `return p`, `p = 1`, `p.val`, `p[i]`, `p.m()` on `*T` / narrowed `?T` outside `unsafe` | Rejected with `error[E0625]` (wrap pointee access in `unsafe { ... }` / `unsafe <expr>`, or keep handle hats `*p` / `?p` when selecting the pointer handle itself) |
| **No `null -> *R` in `unsafe` (`R-2`)** | `unsafe { auto *p = null:*i32 }` | Rejected with `error[E0483]` (`unsafe` does not exempt non-null `*R` from `null`) |
| **Outer Value Widening Allowed** | `**T -> ?*T`, `*?T -> ??T` | Permitted when inner pointee layers and permissions match |
| **Inner Pointee Nullability Invariance (`R-3`)** | `**T -> *?T`, `?*T -> ??T` | Rejected with `error[E04606]` / `error[E0409]` (prevents writing `null` into a non-null inner `*T` slot) |
| **Rebindable Parameter Slot Invariance** | Passing `*#q` to `fn(?#p: T)` | Rejected with `error[E04571]` / `error[E0409]` (prevents callee writing `null` into caller's `*#q` slot) |
| **Unnarrowed Nullable Member Access** | `p.val` on unnarrowed `?Node` | Rejected with `error[E0447]` |
| **Mixed Raw + Managed Multi-Layer Hat** | `fn(&^i32)` / `fn(&?p: i32)` | Rejected with `error[E0495]` / `error[E0492]` (multi-layer hat chains are restricted to pure raw `*`/`?`) |
| **Declaration / Type Hat Mismatch** | `fn(p: *i32)` / `fn(p: ?i32)` | Rejected with `error[E0494]` |
| **Legacy `nul *` Spelling** | `fn(nul *p: i32)` / `-> nul *i32` | Rejected with `error[E0488]` (with migration hint to use `?`) |
| **Postfix Nullable Assertion** | `p?` / `p??` / `self.?next?` | Rejected with `error[E0487]` |
| **Safe Nullable Type Suffix** | `x: i32?` | Rejected with `error[E0484]` |

### 2.4 Migrating Raw Pointer Pointee Access in `0.20.1` (`E0625`)

In `0.20.1`, Toka closes the raw pointer safety boundary between **handle-identity preservation** (safe) and **pointee dereference/projection** (`unsafe` required):

```toka
// 1. Reading, writing, or projecting through a raw pointee requires `unsafe`:
fn read_and_bump(*p#: i32) -> i32 {
    // BEFORE (rejected in 0.20.1 with error[E0625]):
    // p += 1
    // return p

    // AFTER (0.20.1):
    unsafe {
        p += 1
        return p
    }
}

// 2. Passing, copying, comparing, or rebinding the raw pointer handle itself
//    by keeping its hat (`*p`, `?q`, `**pp`, `*#p = *#b`) remains safe without `unsafe`:
fn rebind_and_forward(*#slot: i32, *next: i32, ?maybe: i32) -> *i32 {
    *#slot = *next
    guard ?maybe {
        return ?maybe
    } else {
        return *slot
    }
}
```

---

## 3. `.tki` Format 7, Dual-Epoch Replay Hash, Sidecars & Cache Isolation

### 3.1 `.tki` & Sidecar Metadata Contract

| Artifact / Field | `0.19.0` Value | `0.20.0` / `0.20.1` Value | Enforcement |
| :--- | :--- | :--- | :--- |
| `compiler_version` | `0.9.9-38` | `0.20.1-1` | Toolchain interface identity (`TOKA_COMPILER_INTERFACE_VERSION`) |
| `format_version` | `6` | `7` | Hard rejection (`error[E0901]: Interface format version mismatch (expected 7, got 6)`) |
| `language_epoch` | *(absent)* | `0.20` | Hard rejection on missing or mismatched epoch (`error[E0901]: Interface language_epoch mismatch`) |
| `encap_epoch` | `0.19` | `0.19` | Hard rejection on missing or mismatched epoch (`error[E0901]: Interface encap_epoch mismatch`) |
| `replay_surface_hash` | Binds `encap_epoch` + decls | Binds `language_epoch: 0.20` + `encap_epoch: 0.19` + decls | Hard rejection on header forgery or declaration tampering (`error[E0901]`) |
| `.tke` / `.tsm` / Attestation | `interface_format: "6"` | `interface_format: "7"`, `language_epoch: "0.20"`, `encap_epoch: "0.19"` | Fail-closed sidecar identity and SHA-256 verification |

### 3.2 Dependency & Cache Rebuild Procedure

1. **Verify Tool Identities**:
   ```bash
   tokac --version    # must report 0.20.0
   toka --version     # must report 0.20.0
   tokafmt --version  # must report 0.20.0
   tokalsp --version  # must report 0.20.0
   ```
2. **Purge Stale Caches & Prebuilt Artifacts**:
   - Delete all `0.19.x` `.tki`, `.o`, `.tke`, and `.tsm` files (`rm -rf build/ .toka-cache/ "$TOKA_BUILD_DIR"`).
3. **Migrate Source & Rebuild Upstream Modules**:
   - Migrate any remaining `nul *` spellings to `?` and recompile upstream libraries with `tokac -c --emit-interface` so their `.tki` files record `format_version: 7`, `language_epoch: 0.20`, and `encap_epoch: 0.19`.

---

## 4. Platform Tiering, `0.19.0` Maintenance Policy & Rollback Procedure

### 4.1 Supported Platforms (`toka.release-platforms.0.20.v1`)

| Tier | Platform Label | Environment | Release Status |
| :--- | :--- | :--- | :--- |
| **Tier 1 (Core Blocking)** | `linux-x64` | Ubuntu 22.04 x86_64, Clang/LLVM 20.1.8 | Official release archive (`toka-v0.20.0-linux-x64.tar.gz`) |
| **Tier 1 (Core Blocking)** | `linux-arm64` | Ubuntu 24.04 ARM64, Clang/LLVM 20.1.8 | Official release archive (`toka-v0.20.0-linux-arm64.tar.gz`) |
| **Tier 1 (Core Blocking)** | `macos-arm64` | macOS 15 Apple Silicon, Clang/LLVM 20.1.8 | Official release archive (`toka-v0.20.0-macos-arm64.tar.gz`) |
| **Tier 2 (Dogfood)** | `windows-x64` | Windows x64 MSYS2/MinGW-w64 Clang 20.1.8 | Continuous CI dogfood gate (non-blocking); no standalone release tarball |

### 4.2 `0.19.0` On-Demand Maintenance & Pinned Rollback Procedure

- **`0.19.0` Maintenance Baseline**:
  - The published `v0.19.0` release (`toka-v0.19.0-<target>.tar.gz`, `SHA256SUMS`, Git tag `v0.19.0`, and qualification receipts) remains **immutable** as the rollback baseline for `0.19` users.
  - The `0.19` series is feature-frozen and maintained **strictly on demand** for critical correctness, security, or installation regressions.
- **How to Pin or Roll Back to `v0.19.0`**:
  1. Download the pinned `v0.19.0` archive for your platform and verify its SHA-256 against the `v0.19.0` `SHA256SUMS`.
  2. Check out the `0.19.0`-compatible revision of your project source and dependencies (before `?` raw pointer hat migration). **A `v0.19.0` compiler binary cannot parse `0.20` `?` raw pointer hats or `Format Version 7` `.tki` files.**
  3. Clear any `0.20.0` build/cache directories (`build/`, `TOKA_BUILD_DIR`) and rebuild cleanly with the `v0.19.0` toolchain.

---

## 5. SDK Acceptance Checklist (`v0.20.0`)

1. **Tool Version Identities**:
   - `tokac --version`, `toka --version`, `tokafmt --version`, and `tokalsp --version` all report `0.20.0`.
2. **Standard SDK Archive (`BUILD_TESTING=OFF`)**:
   - Archive name `toka-v0.20.0-<target>.tar.gz` contains `bin/`, `lib/`, `include/`, `docs/`, and `sdk.json`.
   - `sdk.json` binds `candidate_revision`, `version_label: "v0.20.0"`, `source_dirty: false`, `build_testing: false`, and all four tool SHA-256 digests.
   - `docs/` includes `release_notes_v0.20.0.md` and `compatibility_0.20.md`.
3. **Installed Candidate & Replay Controls (`toka.0.20-candidate-controls`)**:
   - Default compiler entry enforces `language_epoch: 0.20`, `encap_epoch: 0.19`, and `Format Version 7` `.tki` emission/consumption.
   - Emitted `.tki` interfaces record `format_version: 7`, `language_epoch: 0.20`, and `encap_epoch: 0.19`; missing, corrupt, Format 6, `language_epoch: 0.19`, and `encap_epoch: 0.18` interfaces are rejected fail-closed (`error[E0901]`).
   - All 12 negative probes reject with exit code `1`, authoritative diagnostic code (`E0488`, `E0487`, `E0484`, `E0483`, `E04606`, `E04571`, `E0447`, `E0495`, `E0418`), and zero residual `.o`/`.ll` artifacts across `--check-only`, `-c`, and `--emit-llvm`.
   - All positive runtime suites (2-layer & 3-layer raw hat chains, 1-layer projection, `NonZeroFact` narrowing/invalidation, and sourceless Format 7 `.tki` consumer) compile, link, and execute natively with `rc == 0`.
   - `tokafmt` formatting of `0.20` raw pointer hat chains and `.await?` is idempotent (`format(format(x)) == format(x)`).
   - All 41 encapsulation/visibility checks and 60 compositional convergence regression probes pass (`rc == 0`).
   - All CTest suites pass (`0` failed).
