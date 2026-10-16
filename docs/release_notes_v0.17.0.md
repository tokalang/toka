# Toka 0.17.0 Release Notes

Toka 0.17.0 introduces **Restricted Compositional Consistency ("受限组合收敛" Restricted Compositional Convergence v1)**, extending the `0.16.0` Scoped Tasks (`显式汇聚受限 v1`) structured concurrency foundation to composite task-handle carriers, multi-task container wrappers, method receivers (`self#` and `cede self#`), and cross-module `.tki` retained-body interfaces.

## Key Highlights

### 1. Restricted Compositional Convergence (`受限组合收敛 v1`)
- **Task-Handle Carrier Tracking**: Shapes and structs carrying `TaskHandle<T>` fields (directly or through nested carrier fields) now preserve field-sensitive borrow and scope dependency metadata (`FieldDependencySet`) across initialization, projection, variable binding, reassignment, and helper calls.
- **Multi-Task Carrier Independence**: Carriers holding multiple `TaskHandle<T>` fields track field-level lifetime dependencies independently. Starting an owned task field (`h.t_owned.start`) on a carrier whose sibling field (`h.t_borrowed`) borrows a local buffer is permitted without false-positive rejection, while starting the borrowed field or a consuming expression that touches any borrowed field fails closed (`error[E04584]`).
- **Method Receiver Ownership & `cede self#` Semantics**:
  - Consuming methods declared with `cede self#` that start an owned internal task handle are statically verified against the receiver's field-level borrow provenance (`r5_pos1_cede_self_receiver_start`).
  - Borrowed receiver calls (`self#`) attempting to start an internal task field (`self.t.start` or `(self.t).start`) or `cede self#` calls on carriers holding any borrowed task field are rejected with `error[E04584]`.
  - Receiver methods with `effects: self <- t` cannot launder unjoined scope obligations across scope boundaries or bypass `join_scoped` / `cancel_and_join` convergence (`error[E0456]`).

### 2. Fail-Closed Escape & Scope Bypass Prevention
- **Detached Start on Borrowed Task & Carrier Paths (`error[E04584]`)**: Starting a task handle (or consuming carrier expression) that borrows local stack state without structured scope attachment is rejected across direct variables, split variables, unboxed field projections, nested carriers, field reassignments, and external `.tki` forwarders.
- **Carrier & Wrapper Scope Escape Prevention (`error[E0456]`)**:
  - Returning or escaping a carrier/wrapper containing a local-borrowing `TaskHandle<T>` outside its lexical scope is rejected (`error[E0455]` / `error[E0456]`).
  - Spawning a task into `scope1#` while joining only an unrelated `scope2#` (dual-scope laundering, including via carrier methods or `effects: self <- ...` wrappers) is rejected with `error[E0456]`.
  - Whole-carrier or field-level reassignment that overwrites an active unjoined scoped task handle is rejected with `error[E0456]`.
- **Bodyless `.tki` Fail-Closed Boundary (`error[E0456]`)**: Passing a local-borrowing `TaskHandle<T>` to a bodyless `.tki` function declaration (where the compiler cannot inspect the callee body to verify non-escape or scope convergence) fails closed at compile time (`error[E0456]`), whereas retained-body `.tki` functions (`checked-local-v1`) are verified and supported.
- **Zero-Artifact Multi-Mode Guarantee**: All negative escape and scope violations fail closed with exit code `1` and emit zero `.o`, `.ll`, or executable artifacts across `--check-only`, `-c`, `--emit-llvm`, and full link invocations.

### 3. Relationship & Compatibility with `v0.16.0`
- **100% Preservation of Valid `v0.16.0` Workflows**: All `v0.16.0` `TaskScope`, `spawn_scoped`, `join_scoped`, `cancel_and_join`, uninterruptible cancel drain barriers, cold forwarder functions (`fn forward(cede t: TaskHandle<T>) -> TaskHandle<T> <- t`), and standalone owned `TaskHandle<T>` patterns remain 100% compatible.
- **Safe Composition Unblocked**: Patterns that were previously over-approximated on multi-task carriers or `cede self#` receiver methods now compile and execute cleanly when the started task path is dependency-free.
- **Soundness Holes Closed**: Indirect escape routes via composite carriers, nested field reassignments, `effects: self <- t` scope laundering, and bodyless `.tki` consumer calls that could bypass `0.16.0` single-handle checks are now statically rejected.

## Release & Platform Policy
- **Core Blocking Platforms**: Linux x64, Linux ARM64, and macOS ARM64.
- **Continuous Dogfood**: Windows validation is maintained continuously and independently.
- **Installed Qualification**: Standard SDK packaging (`BUILD_TESTING=OFF`) and checkout-free artifact replay verify all `0.17.0` compositional convergence and sourceless `.tki` controls directly on unpacked release archives.
