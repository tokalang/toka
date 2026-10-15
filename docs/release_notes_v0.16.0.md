# Toka 0.16.0 Release Notes

Toka 0.16.0 introduces **Scoped Tasks ("显式汇聚受限 v1" Explicit Convergence Restricted v1)**, establishing structured concurrency primitives with compile-time escape safety, deterministic stack-bound lifetimes, uninterruptible cancel drain barriers, and source-hidden interface distribution.

## Key Highlights

### 1. Structured Concurrency with Scoped Tasks
- **Lexical Scope Concurrency**: Concurrent async tasks are bound to an explicit `TaskScope` (`TaskScope::new()`), ensuring child execution is strictly anchored to coordinator lifetimes.
- **Explicit Convergence Model**: Child tasks spawned via `spawn_scoped<T>(scope#, cede task)` must be converged before leaving scope. Coordinators converge either normally via `join_scoped(scope#).await` or through cooperative cancellation via `cancel_and_join(scope#).await`.
- **Zero Use-After-Free**: Stack-allocated locals (buffers, shapes, resources) captured by reference or value across concurrent tasks remain valid until all child tasks have fully terminated and dropped.

### 2. Compile-Time Safety & Escape Prevention
- **Return Escape Prevention**: Task handles cannot be returned from the spawning function or scope body (`error[E0455]`).
- **Container Escape Prevention**: Task handles cannot be embedded inside structs, shapes, tuples, arrays, or heap-allocated containers (`error[E0456]`).
- **Unjoined Scope Exit Guard**: Attempting to exit an active scope with unjoined or un-drained child tasks fails closed at compile time (`error[E0456]`).
- **Detached Start Ban**: Detached background task initiation without structured scope attachment is rejected (`error[E04583]`).
- **Concurrency Mutation Conflict**: Spawning tasks with conflicting concurrent mutable accesses to shared captures is rejected (`error[E0440]`).
- **Cross-Boundary Borrow Guard**: Borrowing internal child task results across scope boundaries is statically rejected (`error[E04658]`).

### 3. Deterministic Lifecycle & Uninterruptible Cancel Drain Barrier
- **Hierarchical Cascade Cancellation**: Canceling a parent coordinator automatically dispatches cooperative cancellation signals to all spawned child tasks.
- **Uninterruptible Drain Barrier**: `cancel_and_join` establishes an uninterruptible drain barrier. Mid-drain cancellation signals injected into the coordinator will not abort or bypass resource cleanup; child tasks run drop handlers and reach terminal state before the scope completes.
- **Strict Causal Drop Ordering**: Encapsulation drop hooks (`impl T@Encap { fn drop(self#) }`) for child resources fire deterministically before parent stack frame teardown.

### 4. Cold Forwarder Wrappers & Sourceless TKI Export
- **Cold Handle Forwarding**: Helper functions can pass unstarted task handles using explicit borrow annotations (`fn forward(cede t: TaskHandle<T>) -> TaskHandle<T> <- t`) while preserving compile-time lifetime connections.
- **Source-Hidden TKI Compilation**: Scoped concurrency libraries compile cleanly into `.tki` interface files and `.o` objects (`tokac -c provider.tk --emit-interface -o provider.o`). Downstream consumers compile and link against precompiled binaries with zero source exposure.

## Release & Platform Policy
- **Core Blocking Platforms**: Linux x64, Linux ARM64, and macOS ARM64.
- **Continuous Dogfood**: Windows validation is maintained continuously and independently.
- **Installed Qualification**: The standard SDK packaging and checkout-free replay verify scoped task controls on standalone unpacked packages without ambient compiler source dependencies.
