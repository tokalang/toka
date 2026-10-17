# Toka 0.18.0 Compatibility Notes & SDK Acceptance Checklist

## Compatibility Notes

### 1. Positioning & Relationship with `v0.17.0` / `v0.17.1`
- **Development Line Positioning**: Toka `0.18.0` is the subsequent release on the `0.1x` line following `0.17.0` Compositional Consistency and the `v0.17.1` borrow-escape and drop-unwinding patch. It introduces static consuming async trait methods taking `cede self`.
- **Invariants Preserved**: The receiver borrow dependency propagation and upstream borrow-escape checking implemented in `v0.17.1` are fully preserved without regression. All 60 compositional convergence regression probes, 41 historical CTest suites, and 14 historical negative probes remain verified and passing.
- **No Lifetime Syntax Extension**: Toka `0.18.0` introduces no explicit lifetime syntax (`'a`, etc.). Structured concurrency and linear ownership transfer (`cede`) remain the sole lifetime management model.
- **Zero External Tooling Impact**: No modifications are made to `trg` (tree-sitter search engine) or `tokakv` (embedded database).
- **Immutability of Historical Assets**: Published `v0.17.0` and earlier Git tags, release tarballs, and historical qualification receipts remain immutable.

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
- **Recompilation Across Versions**: Binary object files (`.o`) and interface files (`.tki`) built with Toka `0.17.x` or earlier must be recompiled using the `0.18.0` `tokac` compiler.
- **Cross-Module `.tki` Export**: Async trait definitions and implementations intended for cross-module consumption must be compiled with `--emit-interface` so their metadata and verified local bodies (`checked-local-v1`) are available to downstream consumers.

### 5. Version Identity Architecture
- **Product Release Version**: `0.18.0`
  - Emitted by CLI tools (`tokac --version`, `toka --version`, `tokafmt --version`, `tokalsp --version`).
  - Recorded in `CMakeLists.txt`, `sdk.json`, and official release tarballs (`toka-v0.18.0-<target>.tar.gz`).
- **Compiler Interface Key**: `0.9.9-38`
  - Defined in `include/toka/InterfaceVersion.h` (`TOKA_COMPILER_INTERFACE_VERSION`).
  - Emitted in `.tki` interface metadata headers (`// @meta compiler_version: 0.9.9-38`).
  - Decoupled from product release tags.

## SDK Acceptance Checklist

1. **Tool Version Identifiers**:
   - `tokac --version` reports `0.18.0`
   - `toka --version` reports `0.18.0`
   - `tokafmt --version` reports `0.18.0`
   - `tokalsp --version` reports `0.18.0`
2. **Standard Build**:
   - Built in clean directory with `-DBUILD_TESTING=OFF`
   - Archive layout includes `bin/`, `lib/`, `include/`, `docs/`, `sdk.json`
3. **Qualification Gates**:
   - 29 negative probes pass with fail-closed rejection and zero leftover artifacts.
   - 3 positive suites pass with compilation, linkage, and native execution (`rc == 0`).
   - 60 compositional convergence regression probes pass 100%.
   - 41 historical CTest suites pass 100%.
