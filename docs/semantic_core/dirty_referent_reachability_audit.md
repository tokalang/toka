# DirtyReferentMask Reachability & Elimination Audit

**Status:** Deletion complete and verified across all test suites. `DirtyReferentMask` removed without regressions. Diagnostic codes `E0411` and `E0412` reserved.

---

## 1. Executive Summary

During the Layer 1 Initialization Completeness Audit, `DirtyReferentMask` was identified in `include/toka/Sema.h` and across various semantic analysis files as a legacy tracking field intended to propagate uninitialized/dirty bitmasks across reference bindings (`&T`).

With the remediation of the uninitialized borrow vulnerability (where `Never` or partially moved places are strictly rejected fail-closed at the `&` operator with `E0410`), this audit evaluated whether `DirtyReferentMask` was reachable, confirmed zero dependencies across all codebases and test suites, verified invariant proof under Debug/ASan execution, and completed the clean removal of the field and its associated propagation logic.

### Key Audit Conclusions & Results:
1. **Unreachable in Accepted Programs:** Entry-point borrow guards reject creating references to non-Live places; no accepted program can ever construct a reference with `DirtyReferentMask != ~0ULL`.
2. **Dynamic Invariant Proof:** A debug invariant (`assert(Info.DirtyReferentMask == ~0ULL)`) was instrumented and executed across all 412 Pass tests, 376 Fail tests, 305 Conformance tests, 44 Semantic Replay cases, 15 CTests, 3 Cache Validation suites, and the FZ3 ASan/UBSan reliability fuzzer with 0 assertion failures.
3. **Structured Projection Verification:** Verified that `BorrowedPath`, `LifeDependencySet`, and `PALCheckerState` fully enforce lifetime depth checking, borrow loans, and return escape contracts on projection paths without relying on legacy string lookups.
4. **Clean Deletion Completed:** Removed `DirtyReferentMask` from `struct SymbolInfo` (`Sema.h`), `Sema_Stmt.cpp`, `Sema_Expr_Binary.cpp`, `Sema_Expr.cpp`, and `Sema_Expr_Member.cpp`.
5. **Reserved Diagnostics:** Diagnostic codes `E0411` (`ERR_ESCAPE_UNSET`) and `E0412` (`ERR_DIRTY_REF_ESCAPE`) remain reserved in `DiagnosticDefs.def` to preserve toolchain stability.

---

## 2. Detailed Audit Questions & Findings

### Q1: Does any accepted program create a dirty reference after fail-closed borrow rejection?

**Finding: NO.**

In `src/Sema/Sema_Stmt.cpp`, a reference was historically marked dirty if and only if the source symbol had `(srcPtr->InitMask & fullMask) != fullMask`.
- If `srcPtr` is uninitialized (`Never`), borrowing `&srcPtr` is rejected fail-closed at the unary/address-of entry point (`Sema_Expr_Unary.cpp:190-205`, `Sema_Expr_Unary.cpp:345-360`, `Sema_Expr.cpp:1455-1510`) emitting `E0410`.
- If `srcPtr` is partially moved (e.g. after `cede pair.left`), borrowing `&pair` is rejected because `!ExactPlace.isDefinitelyLive()`.
- If `srcPtr` is conditionally moved (state `{Live, Moved}`), borrowing whole `&srcPtr` is likewise rejected because it is not definitely live.

Consequently, no AST that passes borrow checking can ever construct a dirty reference.

---

### Q2: Does a projection borrow of a live aggregate set `DirtyReferentMask`?

**Finding: NO.**

When a program borrows a live sub-field projection of an aggregate:
```toka
auto pair = Pair(left = Token(id = 1), right = Token(id = 2))
auto taken = cede pair.left
auto &borrow = &(pair.right) // pair.right is definitely Live
```
1. `m_LastBorrowSource` is recorded as `"pair.right"`.
2. Modern PAL and lifetime tracking parse `m_LastBorrowSource` into an `AccessPath` (`Info.BorrowedPath`), committing transient loans and enforcing lifetime barriers independently of `findSymbol`.
3. Conformance and negative tests confirm that cross-scope projection borrows (`tests/fail/projection_borrow_shorter_lived_escape.tk`), projection return dependencies (`tests/conformance/ownership/projection_reborrow_and_return_dependency.tk`), and hole-filling attempts (`tests/fail/projection_reference_hole_fill_rejected.tk`) are strictly and soundly governed by `PALCheckerState` and `LifeDependencySet`.

---

### Q3: How does referent tracking resolve when `m_LastBorrowSource` is a projection path (e.g. `pair.right`)?

**Finding:**
- **In PAL (Physical Aliasing & Lifetime):** Resolved correctly via `canonicalizeAccessPath(makeAccessPath(m_LastBorrowSource))`.
- **In Legacy Init Tracking:** `findSymbol(m_LastBorrowSource)` treated compound paths as unresolvable atomic identifiers, resulting in a no-op.

---

### Q4: Does any standard library or Conformance test rely on dirty-reference initialization?

**Finding: ZERO CONSUMERS.**

An exhaustive audit of `lib/`, `tests/pass/`, `tests/conformance/`, and `tests/semantic_replay/` confirmed zero occurrences of borrowing an uninitialized place or initializing individual aggregate fields through a reference. All partial initialization attempts through references reside in `tests/fail/` and are rejected fail-closed.

---

### Q5: Can `DirtyReferentMask` be cleanly deleted instead of designing `ReferentAuthority`?

**Finding: YES — Clean deletion executed.**

- In Toka's language semantics, references (`&T`) are strictly views over already initialized, definitely `Live` places.
- Principle `INIT-24` formalizes that ordinary references do not transport initialization authority.
- `DirtyReferentMask` has been cleanly deleted across all compiler source files.
