# Toka 0.18.0 Compatibility Notes & SDK Acceptance Checklist

## Compatibility Notes

### 1. Positioning & Relationship with `v0.17.0` / `v0.17.1`
- **Development Line Positioning**: Toka `0.18.0` is the subsequent release on the `0.1x` line following `0.17.0` Compositional Consistency and the `v0.17.1` borrow-escape and drop-unwinding patch. It introduces static consuming async trait methods taking `cede self`.
- **No 1.0 Stability Claim**: As an iterative development release on the `0.1x` line, Toka `0.18.0` does not claim `1.0` stability. Language features and internal compiler ABIs continue to evolve.
- **Strict Scope of Async Trait Support**: Support is strictly restricted to static dispatch, single-ownership consuming methods (`cede self`) with monomorphized generic dispatch. Dynamic dispatch (`dyn @AsyncTrait`), borrow receivers (`self#`, `&Self`, `~Self`), unrelaxed constructor receivers (`init cede self`), and static methods without self (`fn new() -> async Self`) are strictly disallowed (`error[E0617]` / `error[E0618]`).
- **Invariants Preserved**: The receiver borrow dependency propagation and upstream borrow-escape checking implemented in `v0.17.1` are fully preserved without regression. All 60 compositional convergence regression probes, 141 full CTest suites (0 failed), and negative probes across all compiler modes remain verified and passing.
- **No Lifetime Syntax Extension**: Toka `0.18.0` introduces no explicit lifetime syntax (`'a`, etc.). Structured concurrency and linear ownership transfer (`cede`) remain the sole lifetime management model.
- **Zero External Tooling Impact**: No modifications are made to `trg` (tree-sitter search engine) or `tokakv` (embedded database).
- **Immutability of Historical Assets**: Published `v0.17.0`, `v0.17.1`, and earlier Git tags, release tarballs, and historical qualification receipts remain immutable.

### 2. Admitted Language Forms in `0.18.0`
The following pattern is admitted for static dispatch:

```toka
// Admitted in 0.18.0: consuming cede self static async trait method
pub trait @AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError>
}

pub shape Resource(inner: TaskHandle<i32>, tag: i32)

impl Resource@AsyncClose {
    pub fn close(cede self) -> async Result<i32, CloseError> {
        auto owned = cede self
        auto v = owned.inner.await
        return Result<i32, CloseError>::Ok(v + owned.tag)
    }
}

pub fn close_any<T: @AsyncClose>(cede x: T) -> async Result<i32, CloseError> {
    auto owned = cede x
    return owned.close().await
}
```

### 3. Fail-Closed Boundaries & Non-Relaxed Forms
All unauthorized, unverified, or escaping forms continue to fail closed at compile time:

| Category | Disallowed Form | Diagnostic Code | Compiler Behavior |
| :--- | :--- | :--- | :--- |
| Unrelaxed Receiver Syntax | `fn m(cede self#) -> async R` | `error[E0618]` | Rejected; zero artifacts |
| Unrelaxed Receiver Syntax | `fn m(init cede self) -> async R` | `error[E0618]` | Rejected; zero artifacts |
| Plain Self Receiver | `fn m(self) -> async R` | `error[E0618]` | Rejected; zero artifacts |
| Explicit Borrow Receivers | `fn m(self: &Self) -> async R` | `error[E0618]` | Rejected; zero artifacts |
| Explicit Mut Borrow Receivers| `fn m(self: ~Self) -> async R` | `error[E0618]` | Rejected; zero artifacts |
| Static Method Without Self | `fn new() -> async Self` | `error[E0618]` | Rejected; zero artifacts |
| Dynamic Trait Object | `dyn @AsyncClose` | `error[E0617]` | Rejected; zero artifacts |
| Signature / Receiver Mismatch| Trait/Impl receiver discrepancy | `error[E0428]` | Rejected; zero artifacts |
| Forged Wrapper | Non-cede `block_on(h)` detached `.start` | `error[E0456]` | Rejected; zero artifacts |
| Untrusted Forwarding | Synchronous forwarding without `<- x` | `error[E0454]` | Rejected; zero artifacts |
| Local Borrow Return | Returning borrowed state from async trait | `error[E0455]` / `error[E04584]` | Rejected; zero artifacts |
| Carrier Escape | Detached start on `RefCarrier` | `error[E0455]` / `error[E04584]` | Rejected; zero artifacts |
| Cross-Module Escape | Bodyless `.tki` borrowed receiver sink | `error[E0456]` | Rejected; zero artifacts |

### 4. Migration & Cache Guidance
- **Recompilation Across Versions & No Binary ABI Guarantee**: Binary object files (`.o`) and interface files (`.tki`) built with Toka `0.17.x` or earlier are **not** guaranteed to be binary compatible. Toka does not provide a blanket cross-version binary ABI guarantee. When upgrading to `0.18.0`, existing build caches must be cleared and all `.o` objects and `.tki` interface files must be recompiled using the `0.18.0` `tokac` compiler.
- **Cross-Module `.tki` Export**: Async trait definitions and implementations intended for cross-module consumption must be compiled with `--emit-interface` so their metadata and verified local bodies (`checked-local-v1`) are available to downstream consumers.
- **Retained-Body vs Bodyless `.tki` Boundary**: Hand-written or stripped bodyless `.tki` declarations cannot consume or forward borrowed receivers or local-borrowing `TaskHandle<T>` instances; attempting to do so triggers compile-time rejection (`error[E0456]`).

### 5. Version Identity Architecture & Platform Policy
- **Product Release Version**: `0.18.0`
  - Emitted by CLI tools (`tokac --version`, `toka --version`, `tokafmt --version`, `tokalsp --version`).
  - Recorded in `CMakeLists.txt`, `sdk.json`, and official release tarballs (`toka-v0.18.0-<target>.tar.gz`).
- **Compiler Interface Key**: `0.9.9-38`
  - Defined in `include/toka/InterfaceVersion.h` (`TOKA_COMPILER_INTERFACE_VERSION`).
  - Emitted in `.tki` interface metadata headers (`// @meta compiler_version: 0.9.9-38`).
  - Decoupled from product release tags.
- **Platform Tiering**:
  - **Tier 1 (Core Blocking Targets)**: `linux-x64`, `linux-arm64`, and `macos-arm64`. Formal release SDK packages are built and published exclusively for Tier 1 targets.
  - **Tier 2 (Continuous Dogfood Target)**: `windows-x64` (MSYS2/MinGW-w64 SDK path). Validated continuously via `.github/workflows/windows-dogfood.yml` for toolchain correctness and ecosystem validation; non-blocking for Tier 1 release distribution.

## SDK Acceptance Checklist

1. **Tool Version Identifiers**:
   - `tokac --version` reports `0.18.0`
   - `toka --version` reports `0.18.0`
   - `tokafmt --version` reports `0.18.0`
   - `tokalsp --version` reports `0.18.0`
2. **Standard Build**:
   - Built in clean directory with `-DBUILD_TESTING=OFF`
   - Archive layout includes `bin/`, `lib/`, `include/`, `docs/`, `sdk.json`
   - `sdk.json` candidate revision matches release commit, `source_dirty: false`, `build_testing: false`
3. **Qualification Gates**:
   - 29 negative probes pass with fail-closed rejection and zero leftover artifacts across `--check-only`, `-c`, and `--emit-llvm` (87 commands total).
   - 75 contract mutation test cases pass fail-closed (including P1-2a/b/c/d).
   - 3 positive suites pass with compilation, linkage, and native execution (`rc == 0`) across 5 lifecycle steps (15 commands total).
   - 60 compositional convergence regression probes pass 100% (including 18 native executions `rc == 0`).
   - 110 command execution receipts validated with 0 errors.
   - 141 full CTest suites pass 100% (141/141 passed with 0 failed).
   - 4-platform CI gates pass 100% (Pull Request Gate run 39143108284 and Windows Dogfood run 39143108277).
