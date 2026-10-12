# 0.15 Generic Recursion Scope & Diagnostic Boundary Specification

**Status:** Accepted for 0.15 Local Integration  
**Applies to:** Compiler Frontend (`Sema`), Type Specialization Cache, and Cross-Module TKI Interface

---

## 1. Overview & Architectural Design

Prior to 0.15, generic function instantiation in Toka rejected self-calls or mutual calls whose specialization was still in flight, preventing recursive generic algorithm implementation.

In 0.15, generic recursion is introduced with rigorous compile-time boundaries:
- **Two-phase specialization lifecycle**: Instantiations are tracked with states `Unchecked` -> `InFlight` -> `Valid` / `Invalid`.
- **Tarjan Strongly Connected Component (SCC) group resolution**: Mutually recursive generic functions are tracked on an active template stack (`m_ActiveGenericTemplateStack`). A mutual cycle is committed atomically as `Valid` only when all constituent members complete type and borrow validation without errors. If any member fails, the entire cycle is invalidated.
- **Tainted instance isolation**: Invalidation marks all cycle specializations as `Invalid`. Any subsequent lookup of an invalid cache entry fails closed with diagnostic `error[E04576]`, preventing corrupted intermediate evidence from leaking.

---

## 2. In-Scope Supported Constructs

The 0.15 implementation supports **synchronous generic free functions**:

1. **Direct Homomorphic Recursion**:
   - `fn countdown<T>(n: i32, val: T) -> T` recursively calling `countdown(n - 1, val)` with identical specialization type arguments.
2. **Mutual Homomorphic Recursion**:
   - `fn is_even<T>(n: i32, val: T) -> bool` and `fn is_odd<T>(n: i32, val: T) -> bool` mutually calling each other.
   - Independent of declaration order within the compilation unit (forward and reverse declaration order permuted).
3. **Recursive Algorithms**:
   - Divide-and-conquer binary search (`search<T: @PartialEq + @PartialOrd>`), tree traversal, and node counting.
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

The following constructs are strictly outside the 0.15 scope and are deterministically rejected by the compiler:

| Rejected Construct | Diagnostic Code | Enforcement Mechanism & Behavior |
|---|---|---|
| **Asynchronous Generic Recursion** | `error[E0406]` | `async fn f<T>` direct recursion, mutual async recursion, or mixed sync/async recursion cycles are rejected at Sema entry. Enforced across `--check-only`, `-c -o`, and `--emit-llvm` modes with **zero artifact creation** (no `.o` or `.ll` files emitted). |
| **Polymorphic / Type-Expanding Recursion** | `error[E0406]` | Calls that expand type parameters (e.g., `f<T>` calling `f<Option<T>>` or `f<[T]>`) are intercepted on the active template stack in under 50ms, preventing infinite specialization loops. |
| **Recursive Return Type Mismatch** | `error[E0408]` | Type checker strictly verifies that recursive call sites match the declared return type signature. |
| **Trait Bound Violations** | `error[E0606]` | Generic type constraints (`T: @Trait`) are validated prior to admitting recursive call specializations. |
| **Illegal Borrow Conflicts** | `error[E0441]` | Simultaneous mutable borrow aliasing across recursive call arguments is rejected. |
| **Double Cede / Moved Value Use** | `error[E0438]` | Using a moved/ceded value across or within recursive frame boundaries is rejected. |
| **Mutual Cycle Failure Propagation** | `error[E04576]` | If any member in a mutually recursive cycle encounters an error, the entire cycle fails closed, preventing partial specialization leakage. |
| **Tainted Instance Reuse** | `error[E04576]` | Cached entries resulting from failed recursive specializations are rejected on subsequent lookup. |

> [!NOTE]
> Non-recursive generic async functions (such as `async fn process<T>(val: T) -> T`) remain fully supported and are verified against regression by control probe `P13`.

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
