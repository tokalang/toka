# Toka 0.16.0 Compatibility Notes & SDK Acceptance Checklist

## Compatibility Notes

### 1. Scoped Tasks ("显式汇聚受限 v1" Explicit Convergence Restricted v1)
- Asynchronous tasks can be bound to structured concurrency scopes via `std/async::{TaskScope, spawn_scoped, join_scoped, cancel_and_join}`.
- All tasks spawned under a `TaskScope` must be explicitly converged before scope exit via `join_scoped(scope#).await` or `cancel_and_join(scope#).await`.
- Stack-allocated variables in the parent frame remain guaranteed valid for the lifetime of all child tasks.
- Cancellation establishes an uninterruptible drain barrier ensuring all child tasks and their resources are dropped before parent stack frame teardown.

### 2. Prohibited Concurrency Patterns & Diagnostic Enforcement
- **Return Escape**: Returning `TaskHandle` outside the spawning frame or scope is rejected with `error[E0455]`.
- **Container Escape**: Embedding `TaskHandle` in struct fields, tuples, shapes, vectors, or heap containers is rejected with `error[E0456]`.
- **Unjoined Scope Exit**: Exiting scope while child tasks remain active without explicit join or drain is rejected with `error[E0456]`.
- **Detached Task Start**: Calling `Task::start` without scope attachment is rejected with `error[E04583]`.
- **Concurrent Mutation Conflict**: Spawning tasks with conflicting mutable borrows of shared captures is rejected with `error[E0440]`.
- **Cross-Boundary Borrow**: Borrowing internal child results across scope boundaries is rejected with `error[E04658]`.
- **Forged Scope Bypass**: Fabricating pseudo-scopes or bypassing lifetime rules via wrappers is rejected with `error[E0456]`.
- Multi-mode negative controls guarantee zero object, LLVM IR, or binary artifact emission across `--check-only`, `-c -o`, and `--emit-llvm` compiler invocations.

### 3. Preserved Contracts & Zero Regression
- All existing 0.15 generic recursion features (direct and mutual recursion, Tarjan SCC atomic commit, tainted instance quarantine) remain 100% backward-compatible.
- All existing 0.14 features (unwrap operator `!`, static async traits, framed transport) and 0.13 language features continue to function without alteration.

### 4. Version Identity Architecture (Product Version vs Interface Version)
- **Product Release Version**: `0.16.0`
  - Emitted by CLI tools (`tokac --version`, `toka --version`, `tokafmt --version`, `tokalsp --version`).
  - Recorded in `CMakeLists.txt`, `sdk.json`, and official release tarballs (`toka-v0.16.0-<target>.tar.gz`).
- **Compiler Interface Key**: `0.9.9-38`
  - Defined in `include/toka/InterfaceVersion.h` (`TOKA_COMPILER_INTERFACE_VERSION`).
  - Emitted in `.tki` interface metadata headers (`// @meta compiler_version: 0.9.9-38`), semantic cache manifests, and diagnostic JSON records (`"compiler_version": "0.9.9-38"`).
  - Decoupled from product release tags. Acceptance verifiers must check the product version without conflating it with the compiler interface key.

## SDK Acceptance Checklist

1. **Tool Version Identifiers**:
   - `tokac --version` reports `0.16.0`
   - `toka --version` reports `0.16.0`
   - `tokafmt --version` reports `0.16.0`
   - `tokalsp --version` reports `0.16.0`
2. **SDK Identity Descriptor (`sdk.json`)**:
   - `schema`: `"toka.sdk-identity"`, `version`: `1`
   - `version_label`: `"v0.16.0"`
   - `build_testing`: `false`, `source_dirty`: `false`
   - Binary digests matching physical `bin/` files.
3. **Standalone Development Loop**:
   - Package manager `toka fetch`, `toka build`, `toka run`, `toka test` operate without `TOKA_LIB` or repository checkout.
   - Offline dependency resolution succeeds against `package.lock`.
4. **Candidate Feature Verification (0.16 Scoped Tasks)**:
   - Positive execution: normal convergence, early exit, cascade cancel, forwarder wrapper, pre-drain cancel order, and mid-drain cancel barrier execute with exit code 0.
   - Causal destruction asserts child resource drop occurs strictly before parent stack teardown.
   - Uninterruptible drain barrier protects scope cleanup from re-entrant mid-drain cancellation.
   - Source-hidden TKI compilation, link, and execution succeed without provider source present.
   - Negative controls fail closed: return escape (`E0455`), container escape (`E0456`), unjoined exit (`E0456`), detached start (`E04583`), mutation conflict (`E0440`), and borrowed result (`E04658`) reject compilation with zero emitted artifacts.
