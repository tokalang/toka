# Toka 0.15.0 Release Notes

Toka 0.15.0 introduces generic recursion for synchronous free functions, featuring a two-phase specialization qualification lifecycle, Tarjan SCC transactional cycle resolution, and strict compile-time boundaries for async and polymorphic recursion.

## Key Highlights

### 1. Synchronous Generic Free Function Recursion
- **Direct & Mutual Recursion**: Full language-level support for direct recursion (`fn f<T>(v: T) -> T { return f(v) }`) and mutual recursion (`fn is_even<T>(...)` <-> `fn is_odd<T>(...)`).
- **Declaration Order Invariance**: Mutually recursive functions are resolved regardless of lexical declaration order within the compilation unit.
- **Algorithms & Data Structures**: Enables canonical divide-and-conquer binary search, tree traversal, node counting, and recursive data processing over generic types.
- **Cross-Module & Source-Hidden TKI**: Generic recursive functions can be exported to `.tki` interface files and compiled objects (`.o`), enabling clean downstream consumer usage without requiring original provider source files.

### 2. Rigorous Semantic Lifecycle & Cycle Safety
- **Two-Phase Qualification**: Specializations transition cleanly from `Unchecked` to `InFlight` to `Valid` or `Invalid`.
- **Tarjan SCC Transactional Rollback**: Mutually recursive groups form strongly connected components that commit atomically. If any member in a cycle fails semantic or type validation, the entire cycle is invalidated, preventing partially verified or corrupted instances from entering caches.
- **Tainted Instance Quarantine**: Specializations marked `Invalid` fail closed upon any subsequent cache lookup, preventing tainted evidence reuse.
- **Ownership & Destructor Precision**: Linear types passed via `cede` across recursive frames maintain exact single-handoff destruction (`g_drop == 1`). Unwinding nested recursion frames guarantees exact per-frame local variable destruction (`g_drop == 6` across 5 frames).
- **Explicit Borrow Return Contracts**: Retains explicit borrow annotations (`<- param`) across recursive calls with full borrow-checker validation.

### 3. Compile-Time Safety Boundaries & Diagnostic Enforcement
- **Async Generic Recursion Guard**: Asynchronous generic recursion (direct, mutual, or mixed cycles) is strictly prohibited. Sema rejects recursive async calls with `error[E0406]` across all compiler modes (`--check-only`, `-c -o`, `--emit-llvm`), guaranteeing zero artifact emission. Existing non-recursive generic async functions remain 100% supported without regression.
- **Polymorphic Recursion Guard**: Type-expanding recursion (e.g., `f<T>` calling `f<Option<T>>` or `f<[T]>`) is caught by active template stack cycle detection in under 50ms with `error[E0406]`.
- **Precise Diagnostic Mapping**: Standard type mismatches (`error[E0408]`), trait bound violations (`error[E0606]`), borrow conflicts (`error[E0441]`), and double-cede violations (`error[E0438]`) retain dedicated diagnostics.

## Release & Platform Policy
- **Core Blocking Platforms**: Linux x64, Linux ARM64, and macOS ARM64.
- **Continuous Dogfood**: Windows validation is maintained continuously and independently.
- **Installed Qualification**: The standard SDK packaging and checkout-free replay verify generic recursion controls on standalone unpacked packages without ambient compiler source dependencies.
