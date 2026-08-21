# DirtyReferentMask Reachability & Elimination Audit

**Status:** Preliminary static reachability audit; deletion candidate pending executable proof.

---

## 1. Executive Summary

During the Layer 1 Initialization Completeness Audit, `DirtyReferentMask` was identified in `include/toka/Sema.h` and across various semantic analysis files as a legacy tracking field intended to propagate uninitialized/dirty bitmasks across reference bindings (`&T`).

With the remediation of the uninitialized borrow vulnerability (where `Never` or partially moved places are strictly rejected fail-closed at the `&` operator with `E0410`), this audit evaluates whether `DirtyReferentMask` remains reachable, whether any valid programs consume it, and what dynamic proofs are required before its clean deletion.

### Key Audit Conclusions & Rectifications:
1. **Unreachable in Accepted Programs:** Entry-point borrow guards reject creating references to non-Live places; no accepted program can ever construct a reference with `DirtyReferentMask != ~0ULL`.
2. **Projection Tracking Requires Structured Proof:** In legacy code, borrowing a live sub-field (e.g., `&pair.right`) caused `findSymbol("pair.right")` string lookup to fail. This not only bypassed `DirtyReferentMask` derivation, but also bypassed legacy symbol-level scope-depth checks. We must verify that `BorrowedPath`, `LifeDependencySet`, and `PALCheckerState` fully and soundly take over lifetime checks, borrow loans, and return escape contracts before deleting legacy code paths.
3. **Zero Standard Library & Test Dependencies:** Neither the standard library nor the test corpus (Pass, Fail, Conformance, Semantic Replay) relies on initializing aggregates via references.
4. **Executable Proof Requirement:** Static grep is insufficient. A dynamic Debug invariant must be instrumented into the compiler to prove that across all regression suites, conformance tests, and mutation fuzzers, no accepted program ever derives `DirtyReferentMask != ~0ULL`.
5. **Class C Alignment:** Aligns with `docs/semantic_core/initmask_migration_ledger.md` by classifying Class C as a deletion candidate pending executable verification.

---

## 2. Detailed Audit Questions & Findings

### Q1: Does any accepted program create a dirty reference after fail-closed borrow rejection?

**Finding: NO.**

In `src/Sema/Sema_Stmt.cpp` (lines 1904–1910), a reference is marked dirty if and only if the source symbol satisfies:
```cpp
if (!hasPlaceState(srcPtr->placeFact(), PlaceState::Never) &&
    (srcPtr->InitMask & fullMask) != fullMask) {
  Info.DirtyReferentMask = srcPtr->InitMask;
} else {
  Info.DirtyReferentMask = ~0ULL; // Clean
}
```
For `(srcPtr->InitMask & fullMask) != fullMask` to be reached by an accepted program:
- If `srcPtr` is uninitialized (`Never`), borrowing `&srcPtr` is rejected fail-closed at the unary/address-of entry point (`Sema_Expr_Unary.cpp:190-205`, `Sema_Expr_Unary.cpp:345-360`, `Sema_Expr.cpp:1455-1510`) emitting `E0410`.
- If `srcPtr` is partially moved (e.g. after `cede pair.left`), borrowing whole `&pair` is rejected because `!ExactPlace.isDefinitelyLive()`.
- If `srcPtr` is conditionally moved (state `{Live, Moved}`), borrowing whole `&srcPtr` is likewise rejected because it is not definitely live.

Consequently, no AST that passes borrow checking can ever have `(srcPtr->InitMask & fullMask) != fullMask` for a whole place borrow.

---

### Q2: Does a projection borrow of a live aggregate set `DirtyReferentMask`?

**Finding: NO, but the string lookup bypass must be formally replaced.**

When a program borrows a live sub-field projection of an aggregate:
```toka
auto pair = Pair(left = Token(id = 1), right = Token(id = 2))
auto taken = cede pair.left
auto &borrow = &pair.right // pair.right is definitely Live
```
1. `m_LastBorrowSource` is recorded as `"pair.right"`.
2. In legacy code, `CurrentScope->findSymbol("pair.right", srcPtr)` evaluates to `false` because the symbol table is keyed only by root variable names.
3. While this left `DirtyReferentMask` as `~0ULL` (Clean), it also skipped the old string-based scope depth check `getScopeDepth(m_LastBorrowSource)`.
4. Modern PAL and lifetime tracking now parse `m_LastBorrowSource` into an `AccessPath` (`Info.BorrowedPath`), committing transient loans and enforcing lifetime barriers independently of `findSymbol`.
5. Before deleting `DirtyReferentMask`, regression tests must prove that cross-scope projection borrows, projection return dependencies, and reborrowing are strictly guarded by `PALCheckerState` and `LifeDependencySet`.

---

### Q3: How does referent tracking resolve when `m_LastBorrowSource` is a projection path (e.g. `pair.right`)?

**Finding:**
- **In PAL (Physical Aliasing & Lifetime):** Resolved correctly. `canonicalizeAccessPath(makeAccessPath(m_LastBorrowSource))` parses the dot-notation into an `AccessPath` struct, allowing the PAL checker to record projection-level alias loans and transient lifetimes.
- **In Legacy Init Tracking (`DirtyReferentMask` / `InitMask`):** Not resolved. The string lookup `findSymbol(m_LastBorrowSource)` treats `"pair.right"` as an atomic variable name and misses, resulting in zero tracking updates to `DirtyReferentMask`.

This confirms that `DirtyReferentMask` was never structurally functional for projections and should be retired once `AccessPath` lifetime checks are confirmed exhaustive.

---

### Q4: Does any standard library or Conformance test rely on dirty-reference initialization?

**Finding: ZERO CONSUMERS.**

An exhaustive audit of `lib/`, `tests/pass/`, `tests/conformance/`, and `tests/semantic_replay/` confirms:
1. `uninit` declarations are always followed by:
   - Local whole-place `init x = ...`;
   - Aggregate whole-place initialization `p = Point(...)`;
   - Outcome matching / pattern construction.
2. Zero occurrences exist of borrowing an uninitialized place (`auto &ref = &uninit_var`) or initializing individual aggregate fields through a reference.
3. Every test attempting partial initialization through a reference resides in `tests/fail/` (e.g., `init_custom_drop_reference_sibling_read.tk`, `init_shared_member_reference_partial_init.tk`) and is strictly expected to fail with `E0410` / `E04572`.

---

### Q5: Can `DirtyReferentMask` be cleanly deleted instead of designing `ReferentAuthority`?

**Finding: YES — Clean deletion is sound once dynamic invariants confirm unreachability.**

- In Toka's language semantics, references (`&T`) are strictly views over already initialized, definitely `Live` places.
- Principle `INIT-24` formalizes that ordinary references do not transport initialization authority.
- Deleting `DirtyReferentMask` removes dead propagation code across `Sema_Expr_Binary.cpp`, `Sema_Expr.cpp`, `Sema_Stmt.cpp`, and `Sema_Expr_Member.cpp`.
- To preserve toolchain compatibility, error codes `E0411` (`ERR_DIRTY_REF_ESCAPE`) and `E0412` (`ERR_USE_OF_UNINITIALIZED_DIRTY_REFERENCE`) will remain reserved.

---

## 3. Dynamic Proof Strategy Before Deletion

Before removing `DirtyReferentMask` from the codebase:
1. **Debug Invariant Assertion**: Instrument `Sema_Stmt.cpp` at the reference binding site with:
   ```cpp
   assert(Info.DirtyReferentMask == ~0ULL && "Internal invariant violation: accepted program created a dirty reference!");
   ```
2. **Comprehensive Projection Test Suite**: Add positive and negative tests covering:
   - Nested projection reborrowing (`auto &r2 = &r1.field`);
   - Projection return dependency propagation;
   - Cross-scope projection lifetime enforcement;
   - Mutable projection writes on admitted places;
   - Rejection of aggregate partial initialization through references.
3. **Execution Gate**: Execute all regression suites, conformance tests, cache validation scripts, and ASan/UBSan fuzzer audits under the instrumented invariant.
