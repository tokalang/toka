# Toka 0.20.0 Release Notes

Toka 0.20.0 is a **breaking raw pointer hat & multi-layer pointer semantics release** on the `0.2x` development branch following `v0.19.0`. This release switches the default compiler language epoch (`TOKA_DEFAULT_LANGUAGE_EPOCH`) to `"0.20"` while preserving the `"0.19"` encapsulation epoch (`TOKA_DEFAULT_ENCAP_EPOCH = "0.19"`): it replaces the legacy two-token `nul *` nullable raw pointer syntax with the single-character `?` raw pointer hat (`?T`, `?p`), enables orthogonal multi-layer pure raw pointer hat chains (`**T`, `*?T`, `?*T`, `??T`, `***T`..`???T`) with per-layer `#`/`$` rebinding permissions (`?#T`, `*#?T`), establishes **1-layer bare-name projection** (`p` peels only the outermost raw pointer layer), enforces strict inner pointee nullability invariance and `unsafe` non-null `*T` discipline, retires the temporary `--encap-epoch=0.18` transition flag, and bumps `.tki` interfaces to **Format Version 7** (`language_epoch: 0.20`, `encap_epoch: 0.19`, `compiler_version: 0.20.0-1`). Complete migration instructions, diagnostic tables, and rollback procedures are documented in [`docs/compatibility_0.20.md`](compatibility_0.20.md).

---

## Key Highlights & Breaking Changes

### 1. Single-Character `?` Raw Pointer Hat & Multi-Layer Pure Raw Hat Chains

In `0.19.0` and earlier, a nullable raw pointer was written `nul *T` (type) and `nul *p` (binding/expression), and multi-layer raw pointer hats were restricted by `E0495`. Starting in `0.20.0`, `?` is the single-character nullable raw pointer hat, composing orthogonally with `*` (non-null raw pointer) and `#` (per-layer rebinding authority):

```toka
// ====================================================================
// Old (0.19.0): two-token `nul *` spelling
// ====================================================================
extern fn libc_malloc(size: usize) -> nul *void
extern fn libc_free(nul *ptr: void) -> void

pub shape RawNode(
    pub val: i32,
    pub nul *#next: RawNode
)

// ====================================================================
// New (0.20.0): single-character `?` hat & multi-layer raw chains
// ====================================================================
extern fn libc_malloc(size: usize) -> ?void
extern fn libc_free(?ptr: void) -> void

pub shape RawNode(
    pub val: i32,
    pub ?#next: RawNode
)

fn link_two(head#: RawNode, ?tail: RawNode) {
    head.?#next = ?tail
}

fn inspect_2layer() -> i32 {
    auto v1 = 10:i32
    auto v2 = 20:i32
    unsafe {
        auto *p1 = *v1
        auto ?#q1 = *v2 : ?i32

        auto **pp = **p1
        auto *?#pq = *?#q1
        auto ?*qp = **pp : ?*i32
        auto ??qq = *?#pq : ??i32

        // Bare name `pp` peels 1 layer (`**i32` -> `*i32`); bind `*pp_in` and read `i32` payload
        auto *pp_in = pp
        auto total# = pp_in

        // 1-layer narrowing on `?*i32` (`?*i32` -> `**i32`)
        guard ?*qp {
            auto *qp_in = qp
            total += qp_in
        } else {
            return -1
        }

        // Stepwise 1-layer narrowing on `??i32` (`??i32` -> `*?i32`, then `?i32` -> `*i32`)
        guard ??qq {
            auto ?inner_q = qq
            guard ?inner_q {
                total += inner_q
            } else {
                return -2
            }
        } else {
            return -3
        }

        // R-1: Bare name `pq` selects the inner `?i32` slot; writing `null` requires no unwrap
        pq = null
        if ?q1 != null {
            return -4
        }
        return total
    }
}
```

### 2. 1-Layer Bare-Name Projection & `NonZeroFact` Narrowing / Invalidation

Toka `0.20.0` cleanly separates three semantic layers for raw pointers:

1. **`DeclaredLayers` vs. 1-Layer `ValueType` Projection**:
   - Every pure raw pointer binding records its ordered outer-to-inner vector of `(* | ?)` layers and per-layer `#`/`$` permissions.
   - Prefixing with the full hat chain (`*p`, `?p`, `**pp`, `*?pp`, `?*pp`, `??pp`) selects the outer handle itself.
   - Writing the bare name (`p` or `pp`) peels **exactly one outer raw layer**:
     - For a 1-layer `*T` (or narrowed `?T`), `p` selects the `T` payload.
     - For a 2-layer `*?T`, `pp` selects the inner `?T` pointer slot (`R-1`: assigning `pp = null` or `pp = *x` writes the inner pointer slot directly without unwrapping).
2. **1-Layer Flow-Sensitive Narrowing (`NonZeroFact`, `R-4` / `R-5`)**:
   - `guard ?p { ... } else { ... }` and `if ?p != null { ... }` narrow only the **outermost** nullable layer (`?R -> *R`). Multi-layer chains (`??T`) narrow one layer at a time (`guard ??pp { auto ?inner = pp; guard ?inner { ... } }`).
   - `(?p).unwrap()` narrows only its returned value (`*R`), leaving the source binding `?p` unnarrowed in subsequent statements (`R-5`).
   - Active `NonZeroFact` entries are invalidated upon direct handle rebinding (`?#p = ...`), address/alias mutation, or calls that may overwrite the rebindable slot.

### 3. Raw Pointer Safety Boundaries (`R-2`, `R-3`) & Preserved Rejections

| Category | Example | Enforcement in `0.20.0` |
| :--- | :--- | :--- |
| **`null -> *R` Forbidden Even in `unsafe` (`R-2`)** | `unsafe { auto *p = null:*i32 }` | Rejected with `error[E0483]` |
| **Inner Pointee Nullability Invariance (`R-3`)** | `**T -> *?T` or `?*T -> ??T` | Rejected with `error[E04606]` / `error[E0409]` |
| **Rebindable Parameter Slot Invariance** | Passing `*#q` to `fn(?#p: T)` | Rejected with `error[E04571]` / `error[E0409]` |
| **Outer Value Widening** | `*T -> ?T`, `**T -> ?*T`, `*?T -> ??T` | Allowed when inner pointee layers and permissions match |
| **Legacy `nul *` Removed** | `fn(nul *p: i32)` / `-> nul *i32` | Rejected with `error[E0488]` |
| **Postfix `?` / `??` Removed** | `p?`, `p??`, `self.?next?` | Rejected with `error[E0487]` |
| **Type Suffix `T?` Removed** | `x: i32?` | Rejected with `error[E0484]` |
| **Mixed Raw + Managed Multi-Layer Hat** | `fn(&^i32)` / `fn(&?p: i32)` | Rejected with `error[E0495]` / `error[E0492]` |
| **Async Cancellation `.await?`** | `task.await?` | Preserved unchanged |

### 4. TKI Format Version 7, Dual-Epoch Replay Hash & Sidecar Isolation

- **`.tki` Format Version 7 & Dual-Epoch Binding**:
  - Generated `.tki` files emit `@meta compiler_version: 0.20.0-1`, `@meta format_version: 7`, `@meta language_epoch: 0.20`, and `@meta encap_epoch: 0.19`.
  - `replay_surface_hash` binds both `language_epoch: 0.20` and `encap_epoch: 0.19` alongside normalized declarations.
  - Memory Evidence sidecars (`.tke`), Semantic Manifest Envelopes (`.tsm`), and Attestation sidecars record and verify `interface_format: "7"`, `language_epoch: "0.20"`, and `encap_epoch: "0.19"`.
  - Stale Format 6 `.tki` files, missing/mismatched `language_epoch`, or mismatched `encap_epoch` are rejected fail-closed with `error[E0901]` in sourceless builds (or trigger automatic cache rebuilds when `.tk` sources are present).
- **Retirement of `--encap-epoch=0.18`**:
  - In accordance with the `0.19.0` deprecation schedule, `--encap-epoch=0.18` is removed in `0.20.0`.
- **Formatter (`tokafmt`) & LSP (`tokalsp`) Synchronization**:
  - `tokafmt` natively formats `?`, `*`, and `#` prefix, member, and type hat chains (`??p`, `?*#p`, `self.?#next`, `?*#i32`) as well as `.await?` idempotently (`format(format(x)) == format(x)`).
  - `tokalsp` hover, go-to-definition, references, completion, and rename preserve multi-layer raw pointer hat chains without stripping or corrupting `?`/`*`/`#` prefixes.

---

## Platform Policy (`toka.release-platforms.0.20.v1`), `0.19.0` Maintenance & Rollback

- **Tier 1 (Core Blocking Release Platforms)**:
  - **`linux-x64`** (Ubuntu 22.04, Clang / LLVM 20.1.8)
  - **`linux-arm64`** (Ubuntu 24.04 ARM64, Clang / LLVM 20.1.8)
  - **`macos-arm64`** (macOS 15 Apple Silicon, Clang / LLVM 20.1.8)
  - Official release archives (`toka-v0.20.0-<target>.tar.gz`) and `SHA256SUMS` are published for Tier 1 targets under policy `toka.release-platforms.0.20.v1`.
- **Tier 2 (Continuous Dogfood Target)**:
  - **`windows-x64`** (MSYS2 / MinGW-w64 Clang 20.1.8 SDK path, validated via `.github/workflows/windows-dogfood.yml`; non-blocking for Tier 1 release archive publication).
- **`0.19.0` Rollback Baseline**:
  - Published `v0.19.0` Git tags, release tarballs (`toka-v0.19.0-<target>.tar.gz`), `SHA256SUMS`, and qualification receipts remain **immutable** and serve as the rollback baseline for `0.19` projects.
  - Projects rolling back to `v0.19.0` must pin the `v0.19.0` archive together with `0.19.0`-compatible source code (prior to `?` raw hat migration) and freshly rebuilt Format 6 `.tki` caches.
