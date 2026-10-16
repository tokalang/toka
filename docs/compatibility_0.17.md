# Toka 0.17.0 Compatibility Notes & SDK Acceptance Checklist

## Compatibility Notes

### 1. Restricted Compositional Consistency ("受限组合收敛" Restricted Compositional Convergence v1)
- Extends `0.16.0` Scoped Tasks (`显式汇聚受限 v1`) from direct `TaskHandle<T>` variables to composite carriers (`shape` / `struct` types containing one or more `TaskHandle<T>` fields, nested carriers, method receivers, and retained-body `.tki` helpers).
- **Field-Sensitive Carrier Provenance**:
  - Each `TaskHandle<T>` field inside a carrier tracks its own local borrow dependency set (`FieldDependencySet`) and scope registration obligation independently.
  - Multi-task carriers containing both an owned task (`t_owned`) and a local-borrowing task (`t_borrowed`) allow starting `t_owned.start` while rejecting `t_borrowed.start` (`error[E04584]`).
- **Method Receiver Ownership (`self#` vs `cede self#`)**:
  - Starting an internal task via a consuming receiver method (`pub fn run(cede self#) -> i32 { self.t.start; ... }`) is permitted when `self` (and its task fields) carry no local stack borrows.
  - Starting an internal task through a mutable borrow receiver (`self#`) without `cede` ownership, or calling a `cede self#` start method when any task field on the carrier borrows local state, is rejected with `error[E04584]`.

### 2. Relationship with `v0.16.0` & Tightened Soundness Boundaries
- **Backward Compatibility with Valid `v0.16.0` Code**:
  - All valid `0.16.0` structured concurrency programs using `std/async::{TaskScope, spawn_scoped, join_scoped, cancel_and_join}`, cold task forwarders, and sourceless `.tki` modules continue to compile and run identically in `0.17.0`.
  - All `0.15.0` generic recursion features, `0.14.0` unwrap (`!`) / static async traits / framed transport, and `0.13.x` contracts remain 100% preserved.
- **Intentional Tightening of Unsound Escape Loopholes**:
  - Code that previously laundered a local-borrowing `TaskHandle<T>` through a struct field, nested carrier reassignment, `effects: self <- t` method receiver, dual-scope mismatch (`spawn` on `s1#`, `join_scoped` on `s2#`), or bodyless `.tki` declaration is now rejected at compile time:
    - **Borrowed Task / Carrier Detached Start**: Rejected with `error[E04584]`.
    - **Carrier / Scope Escape, Dual-Scope Laundering & Bodyless `.tki` Sink**: Rejected with `error[E0456]`.
    - **Direct Return Escape**: Rejected with `error[E0455]`.
    - **Detached Start inside Scoped Coordinator**: Rejected with `error[E04583]`.
    - **Concurrent Mutable Capture Conflict**: Rejected with `error[E0440]`.
    - **Cross-Boundary Child Result Borrow**: Rejected with `error[E04658]`.
  - Non-task structs and scalar expressions do not incur false-positive `FieldDependencySet` propagation.

### 3. Version Identity Architecture (Product Version vs Interface Version)
- **Product Release Version**: `0.17.0`
  - Emitted by CLI tools (`tokac --version`, `toka --version`, `tokafmt --version`, `tokalsp --version`).
  - Recorded in `CMakeLists.txt`, `sdk.json`, and official release tarballs (`toka-v0.17.0-<target>.tar.gz`).
- **Compiler Interface Key**: `0.9.9-38`
  - Defined in `include/toka/InterfaceVersion.h` (`TOKA_COMPILER_INTERFACE_VERSION`).
  - Emitted in `.tki` interface metadata headers (`// @meta compiler_version: 0.9.9-38`), semantic cache manifests, and diagnostic JSON records (`"compiler_version": "0.9.9-38"`).
  - Decoupled from product release tags. Acceptance verifiers check the product version without conflating it with the compiler interface key.

## SDK Acceptance Checklist

1. **Tool Version Identifiers**:
   - `tokac --version` reports `0.17.0`
   - `toka --version` reports `0.17.0`
   - `tokafmt --version` reports `0.17.0`
   - `tokalsp --version` reports `0.17.0`
2. **SDK Identity Descriptor (`sdk.json`)**:
   - `schema`: `"toka.sdk-identity"`, `version`: `1`
   - `version_label`: `"v0.17.0"`
   - `build_testing`: `false`, `source_dirty`: `false`
   - Binary digests matching physical `bin/` files.
3. **Standalone Development Loop**:
   - Package manager `toka fetch`, `toka build`, `toka run`, `toka test` operate without `TOKA_LIB` or repository checkout.
   - Offline dependency resolution succeeds against `package.lock`.
4. **Candidate Feature Verification (0.17 Compositional Consistency & Scoped Tasks)**:
   - Positive execution: carrier consumption, carrier forwarding, multi-task carrier independent owned `.start`, `cede self#` receiver `.start`, dual-scope full convergence, retained-body `.tki` forwarding/spawning, and `0.16` scoped task lifecycle barriers execute with exit code `0`.
   - Source-hidden `.tki` compilation, link, and execution succeed without provider source present, while missing `.tki`, corrupted `.tki`, and cross-module / bodyless `.tki` escape attempts fail closed.
   - Multi-mode negative controls fail closed across `--check-only`, `-c -o`, `--emit-llvm`, and executable link modes with expected diagnostics (`E04584`, `E0456`, `E0455`, `E04583`, `E0440`, `E04658`) and zero emitted artifacts.
