# 0.15 Generic Recursion Scope & Diagnostic Boundary Specification

**Status:** Accepted for 0.15 Local Integration  
**Applies to:** Compiler Frontend (`Sema`), Type Specialization Cache, and Cross-Module TKI Interface

---

## 1. Overview & Architectural Design

Prior to 0.15, generic function instantiation in Toka rejected self-calls or mutual calls whose specialization was still in flight, preventing recursive generic algorithm implementation.

In 0.15, generic recursion is introduced with rigorous compile-time boundaries:
- **Two-phase specialization lifecycle**: Instantiations are tracked with states `Unchecked` -> `InFlight` -> `Valid` / `Invalid`.
- **Tarjan Strongly Connected Component (SCC) group resolution**: Mutually recursive generic functions are tracked on an active template stack (`m_ActiveGenericTemplateStack`). A mutual cycle is committed atomically as `Valid` only when all constituent members complete type and borrow validation without errors. If any member fails, the entire cycle is invalidated.
- **Tainted instance isolation**: Invalidation marks all cycle specializations as `Invalid`. Any subsequent lookup of an invalid cache entry fails closed, aborting specialization without reusing corrupted intermediate evidence or masking the root error.

---

## 2. In-Scope Supported Constructs

The 0.15 implementation supports **synchronous generic free functions**:

1. **Direct Homomorphic Recursion**:
   - `fn countdown<T>(n: i32, val: T) -> T` recursively calling `countdown(n - 1, val)` with identical specialization type arguments.
2. **Mutual Homomorphic Recursion**:
   - `fn is_even<T>(n: i32, val: T) -> bool` and `fn is_odd<T>(n: i32, val: T) -> bool` mutually calling each other.
   - Independent of declaration order within the compilation unit (forward and reverse declaration order permuted).
3. **Recursive Algorithms**:
   - Divide-and-conquer binary search (`search<T: @PartialOrd>`), tree traversal, and node counting.
4. **Explicit Return Borrow Contracts (`<- param`)**:
   - Recursive propagation and de-borrowing of declared return references (`fn pick_first<T>(a: &T, b: &T, depth: i32) -> &T <- a`).
5. **Precise Ownership Cede & Destructor Invariants**:
   - Transfer of linear types (`cede val: Vec<T>`) across recursive frames guarantees exact single handoff destruction (`g_drop == 1`).
   - Frame-local variable allocation and unwinding guarantees exact-depth stack unwind destruction (`g_drop == 6` across 5 recursive frames).
6. **Cross-Module Source & Source-Hidden TKI Usage**:
   - Cross-module import of generic recursive functions.
   - Provider compilation to `.tki` header and `.o` object, source removal, and standalone consumer compilation and execution.

---

## 3. Out-of-Scope Boundaries & Diagnostic Enforcement

The following constructs are strictly outside the 0.15 scope and are deterministically rejected by the compiler with dedicated, non-overlapping diagnostic codes:

| Category | Example / Syntax | Diagnostic Code | Enforcement Mechanism & Behavior |
|---|---|---|---|
| **Asynchronous Generic Recursion** | `fn f<T>(v: T) -> async i32 { return f(v).await }` | `error[E0406]` | Synchronous free function recursion entry guard detects `async` return specifier. Rejection enforced across `--check-only`, `-c -o`, and `--emit-llvm` modes with **zero artifact creation** (no `.o` or `.ll` files emitted; probes `N08`, `N09`, `N10`). |
| **Mixed Sync/Async Cycle (Sync Entry)** | `fn s<T>(v: T) -> i32 { return a(v) }` where `a` is async | `error[E04661]` | Task result origin unproven when synchronous recursion frame attempts un-awaited transfer into task binding (`N11`). |
| **Polymorphic / Type Expansion** | `fn f<T>(v: T) -> i32 { return f([v]) }` | `error[E0406]` | Active generic template stack detects nested instantiation of the same template with expanding type argument `[T]`. Intercepted statically without runaway depth (~20ms on reference host, bounded by template stack depth; `N05`). |
| **Recursive Return Type Mismatch** | `fn f<T>(v: T) -> i32 { return "str" }` | `error[E0408]` | Return expression fails unification against declared return type signature (`N01`). |
| **Trait Bound Violations** | `fn f<T: @Encap>(v: T) -> i32` called with unconstrained type | `error[E0606]` | Generic specialization fails prerequisite trait constraint checking before admit (`N02`). |
| **Illegal Borrow Conflicts** | `auto x# = 10; auto &y# = &x; auto val = x;` inside recursive body | `error[E0441]` | Accessing value while mutably borrowed detected during recursive function analysis (`N03`). |
| **Double Cede / Moved Value Use** | `fn f<T>(cede a: T) { f(cede a); f(cede a) }` | `error[E0438]` | Linear value consumed across first call point is unavailable for subsequent use (`N04`). |
| **Mutual Cycle Failure Propagation** | `fn a<T>() -> i32 { return b<T>() }` where `b` has semantic error | Preserved Root Diagnostic (e.g., `E04576` for `val + 1`) | Tarjan SCC transactional commit detects constituent member failure; entire cycle is marked `Invalid` and fails closed (`N06`). The original failure diagnostic inside the invalid body is faithfully reported without leaking corrupted specializations into caches. |
| **Tainted Instance Quarantine** | Calling previously invalidated specialization `a<i32>()` | Preserved Root Diagnostic (e.g., `E04576` for `val + 1`) | Instantiation cache lookup detects `ValidationState::Invalid` and aborts immediately, preventing tainted evidence reuse (`N07`). |

> [!NOTE]
> Diagnostic code `error[E04576]` (`ERR_SEMA_OPERANDS_OF_MUST_BE_NUMERIC_GOT`) observed in test probes `N06` and `N07` is the root semantic type error resulting from evaluating `return val + 1` with `val: str`. It is **not** a dedicated error code for Tarjan cycle failure or cache contamination. The cycle rollback and cache quarantine mechanisms do not invent synthetic error codes; they preserve and surface the underlying semantic diagnostic while ensuring that no invalid specialization is committed or reused.
>
> Non-recursive generic async functions (such as `fn process<T>(val: T) -> async T`) remain fully supported and are verified against regression by control probe `P13`.

---

## 4. Verification & Continuous Integration Gate

Regression testing is wired into CMake / CTest via the test target:

```bash
ctest -R toka_generic_recursion -V
```

The underlying runner [`tools/scripts/test_generic_recursion.py`](file:///Users/zhyi/GitDP/tokalang/toka/tools/scripts/test_generic_recursion.py) executes 30 test vectors against the test fixtures located in [`tests/semantics/generic_recursion/`](file:///Users/zhyi/GitDP/tokalang/toka/tests/semantics/generic_recursion/):
- **8 Positive Recursive Algorithms & Specialized Probes** (`P01`, `P04`, `P06`, `P07`, `P09`, `P10`, `P11`, `P12`)
- **8 Non-Recursive & Non-Generic Controls** (`P02`, `P03`, `P05`, `P08`, `P10_lib`, `P11_ctl`, `P12_ctl`, `P13_async_ctl`)
- **11 Safety Boundary Tests** (`N01`-`N11` covering type mismatch, trait bounds, borrow conflicts, double cede, polymorphic expansion, mutual failure, tainted cache reuse, and async direct/mutual/mixed cycles)
- **1 Cleanup Drop Tracking Assertion** (Exact cede handoff drop = 1, frame locals drop = 6)
- **1 Source-Hidden TKI Compilation & Execution Audit** (Complete absence of provider source; consumer executes and verifies output 55)
- **1 Async Multi-Mode Zero-Artifact Audit** (`check-only`, `-c -o`, and `--emit-llvm` modes all exit with code 1 and create zero output files)
