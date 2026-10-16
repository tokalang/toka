# Toka 0.17.0 Compatibility Notes & SDK Acceptance Checklist

## Compatibility Notes

### 1. Positioning & Relationship with `v0.16.0`
- **Development Line Positioning**: Toka `0.17.0` is a subsequent development release on the `0.1x` line following `0.16.0` Scoped Tasks (`显式汇聚受限 v1`). It focuses on compositional consistency improvements, soundness fixes for borrowed tasks across wrappers, aggregate carrier fields, ordinary method receivers, and `.tki` calls, and false-rejection fixes for valid programs.
- **No New Syntax or `1.0` Claim**: `0.17.0` inherits the `0.16.0` `显式汇聚受限 v1` model without introducing new lifetime annotations or syntax extensions, and does not claim `1.0` stability.
- **Immutability of `v0.16.0` Assets**: Published `v0.16.0` Git tags, original release tarballs, and historical qualification receipts remain immutable.

### 2. Previously Mis-Accepted Escape Routes Now Rejected (Soundness Fixes)
Programs that previously bypassed single-variable `TaskHandle<T>` checks through composite carriers, reassignments, receiver effects, or opaque interfaces are now rejected at compile time:

| Category | Previously Mis-Accepted Form | `0.17.0` Behavior & Diagnostic |
| :--- | :--- | :--- |
| Carrier / Split-Var Detached Start | Binding a carrier holding a local-borrowing `TaskHandle<T>` to a local or split variable and invoking `.start` (`c.t.start`, `(c.t).start`, or two-step projection) | Rejected (`error[E04584]`) |
| Nested Carrier & Field Reassignment Start | Mutating a carrier or nested holder field (`h.c.t = make_borrow_task(buf)` or via helper method/forwarder) and then invoking `.start` | Rejected (`error[E04584]`) |
| Borrowed Receiver Start (`self#`) | Invoking `self.t.start` or `(self.t).start` inside a method taking a mutable borrow receiver (`self#`, without `cede`) | Rejected (`error[E04584]`) |
| Consuming Receiver (`cede self#`) with Borrowed Sibling | Calling a `cede self#` start method on a multi-task carrier when any task field on the carrier borrows local state | Rejected (`error[E04584]`) |
| Carrier Return / Wrapper Escape | Returning or escaping a struct/shape carrier containing a local-borrowing `TaskHandle<T>` or overwriting an active scoped handle via whole-carrier reassignment | Rejected (`error[E0455]` / `error[E0456]`) |
| Dual-Scope & Receiver-Effect Laundering | Spawning a task on `s1#` (directly or via `effects: self <- t` / helper / `match` branch) and only joining `s2#` before exit | Rejected (`error[E0456]`) |
| Bodyless `.tki` Borrowed-Task Sink | Passing a local-borrowing `TaskHandle<T>` to a bodyless `.tki` declaration whose body is unavailable for escape/convergence inspection | Rejected (`error[E0456]`) |

```toka
// Rejected in 0.17.0 with error[E04584]: borrowed receiver cannot detached-start self.t
shape Holder(t: TaskHandle<i32>)
impl Holder {
    pub fn bad_start(self#) -> i32 {
        self.t.start // error[E04584]
        return 0:i32
    }
}
```

### 3. Previously Mis-Rejected Valid Programs Now Accepted (False-Rejection Fixes)
Within the verified compositional subset, `0.17.0` unblocks valid patterns that were previously over-rejected:
- **Field-Sensitive Multi-Task Carriers**: In a carrier holding multiple `TaskHandle<T>` fields (`shape TwoTasks(t1: TaskHandle<i32>, t2: TaskHandle<i32>)`), if `t1` borrows a local buffer while `t2` is owned (dependency-free), starting `holder.t2.start` (inline, via split variable, or through a cold forwarder) is now accepted while `holder.t1.start` remains rejected (`error[E04584]`).
- **Ordinary Consuming Receiver (`cede self#`) Owned Start**: Ordinary `impl` methods taking `cede self#` (`pub fn run_owned(cede self#) -> i32 { self.t.start; ... }`) are accepted when the caller's carrier instance carries no local borrow dependencies.
  - *Scope boundary note*: This repairs `cede self#` consuming semantics on ordinary `impl` methods; it does **not** introduce consuming `cede self` async trait methods.
- **Retained-Body `.tki` Helpers**: Cross-module cold forwarders, consumers, and scoped spawners exported in `.tki` with retained bodies (`// @meta local_body_policy: checked-local-v1`) are verified compositionally and accepted when scope and borrow rules are satisfied.
- **Non-Task Value & Scalar Precision**: Scalar arithmetic and non-task struct projections no longer trigger false-positive `FieldDependencySet` propagation on unrelated standard library modules.

### 4. Source, `.tki` Interface, and Cache Migration Guidance
- **Recompilation Required Across Versions**: Toka `0.17.0` does not promise blanket binary or `.tki` compatibility with `0.16.0` build artifacts. When upgrading to `0.17.0`, clean existing build caches and recompile all `.o` objects and `.tki` interface files with the `0.17.0` `tokac` compiler.
- **Retained-Body vs Bodyless `.tki` Boundary**:
  - Cross-module functions that accept or forward `TaskHandle<T>` values with local borrows must be compiled with `--emit-interface` so their retained body (`checked-local-v1`) is available to downstream type/escape checking.
  - Hand-written or stripped bodyless `.tki` declarations cannot accept local-borrowing `TaskHandle<T>` arguments (`error[E0456]`).

### 5. Version Identity Architecture (Product Version vs Interface Version)
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
   - Positive execution: carrier consumption, carrier forwarding, multi-task carrier independent owned `.start`, ordinary `cede self#` receiver `.start`, dual-scope full convergence, retained-body `.tki` forwarding/spawning, and verified `0.16` scoped task lifecycle baselines execute with exit code `0`.
   - Source-hidden `.tki` compilation, link, and execution succeed without provider source present, while missing `.tki`, corrupted `.tki`, and cross-module / bodyless `.tki` escape attempts fail closed.
   - Multi-mode negative controls fail closed across `--check-only`, `-c -o`, `--emit-llvm`, and executable link modes with expected diagnostics (`E04584`, `E0456`, `E0455`) and zero emitted artifacts.
