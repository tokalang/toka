# DirtyReferentMask Reachability & Elimination Audit

**Status:** Read-only audit complete. No reachable dirty reference exists in accepted programs. Clean deletion recommended over ReferentAuthority formalization.

---

## 1. Executive Summary

During the Layer 1 Initialization Completeness Audit, `DirtyReferentMask` was identified in `include/toka/Sema.h` and across various semantic analysis files as a legacy tracking field intended to propagate uninitialized/dirty bitmasks across reference bindings (`&T`).

With the remediation of the uninitialized borrow vulnerability (where `Never` or partially moved places are strictly rejected fail-closed at the `&` operator with `E0410`), this audit evaluates whether `DirtyReferentMask` remains reachable, whether any valid programs consume it, and whether it should be formalized into a new `ReferentAuthority` system or cleanly deleted.

### Key Audit Conclusions:
1. **Unreachable in Accepted Programs:** Entry-point borrow guards reject creating references to non-Live places; no accepted program can ever construct a reference with `DirtyReferentMask != ~0ULL`.
2. **Ignored for Projection Borrows:** When borrowing a live sub-field (e.g. `&pair.right`), string lookup `findSymbol("pair.right")` fails against the root symbol table, leaving `DirtyReferentMask` unmodified at `~0ULL` (Clean).
3. **Zero Standard Library & Test Dependencies:** Neither the standard library nor the test corpus (Pass, Fail, Conformance, Semantic Replay) relies on initializing aggregates via references.
4. **Clean Deletion Recommended:** Because references in Toka are strictly post-initialization views over `Live` places and cannot transport `init` authority (`INIT-24`), `DirtyReferentMask` should be completely removed rather than architecting unnecessary `ReferentAuthority` complexity.

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
- If `srcPtr` is partially moved (e.g. after `cede pair.left`), borrowing `&pair` is rejected because `!ExactPlace.isDefinitelyLive()`.
- If `srcPtr` is conditionally moved (state `{Live, Moved}`), borrowing whole `&srcPtr` is likewise rejected because it is not definitely live.

Consequently, no AST that passes borrow checking can ever have `(srcPtr->InitMask & fullMask) != fullMask` for a whole place borrow.

---

### Q2: Does a projection borrow of a live aggregate set `DirtyReferentMask`?

**Finding: NO.**

When a valid program borrows a live sub-field projection of an aggregate:
```toka
auto pair = Pair(left = Token(id = 1), right = Token(id = 2))
auto taken = cede pair.left
auto &borrow = &pair.right // pair.right is definitely Live
```
1. `m_LastBorrowSource` is recorded as `"pair.right"`.
2. In `Sema_Stmt.cpp:1870`, the compiler attempts to look up the source in the local scope:
   ```cpp
   SymbolInfo *srcPtr = nullptr;
   if (CurrentScope->findSymbol(m_LastBorrowSource, srcPtr)) { ... }
   ```
3. `CurrentScope->Symbols` is keyed exclusively by root identifier names (e.g., `"pair"`), never by compound dot-paths (e.g., `"pair.right"`).
4. `findSymbol("pair.right", srcPtr)` evaluates to `false` and `srcPtr` remains `nullptr`.
5. The entire dirty-mask derivation block (lines 1871–1911) is skipped.
6. `borrow.DirtyReferentMask` remains at its default constructor value `~0ULL` (Clean).

---

### Q3: How does referent tracking resolve when `m_LastBorrowSource` is a projection path (e.g. `pair.right`)?

**Finding:**
- **In PAL (Physical Aliasing & Lifetime):** Resolved correctly. `canonicalizeAccessPath(makeAccessPath(m_LastBorrowSource))` parses the dot-notation into an `AccessPath` struct, allowing the PAL checker to record projection-level alias loans and transient lifetimes.
- **In Legacy Init Tracking (`DirtyReferentMask` / `InitMask`):** Not resolved. The string lookup `findSymbol(m_LastBorrowSource)` treats `"pair.right"` as an atomic variable name and misses, resulting in zero tracking updates to `DirtyReferentMask`.

This asymmetry demonstrates that `DirtyReferentMask` was never designed for structured projection paths and has always been a no-op for sub-field borrows.

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

**Finding: YES — Clean deletion is the sound and optimal architecture.**

- In Toka's language semantics, references (`&T`) are strictly views over already initialized, definitely `Live` places.
- Principle `INIT-24` formalizes that ordinary references do not transport initialization authority.
- Designing a `ReferentAuthority` system would add complexity to track a capability that Toka explicitly disallows.
- Deleting `DirtyReferentMask` simplifies `SymbolInfo`, eliminates dead propagation code across `Sema_Expr_Binary.cpp`, `Sema_Expr.cpp`, `Sema_Stmt.cpp`, and `Sema_Expr_Member.cpp`, and moves the compiler closer to a single, unified `ExactPlaceState` reality.

---

## 3. Inventory of `DirtyReferentMask` Occurrences for Future Removal

The following locations reference `DirtyReferentMask` and can be systematically cleaned up in the next migration phase:

1. **`include/toka/Sema.h`**:
   - Declaration `uint64_t DirtyReferentMask = ~0ULL;` in `struct SymbolInfo`.
2. **`src/Sema/Sema_Stmt.cpp`**:
   - Lines 526–550: Scope-exit "Hot Potato" check on `DirtyReferentMask`.
   - Lines 709–715: Block exit dirty check.
   - Lines 1904–1910: Initialization of `DirtyReferentMask` from `srcPtr->InitMask`.
3. **`src/Sema/Sema_Expr_Binary.cpp`**:
   - Line 757: Writability bypass `if (InfoPtr->DirtyReferentMask != ~0ULL)`.
   - Line 946: `isUnset` check incorporating `DirtyReferentMask`.
   - Lines 1348, 1350, 1372, 1416: Bitwise propagation on reference assignment.
4. **`src/Sema/Sema_Expr.cpp`**:
   - Lines 3747–3751, 3794–3798: Escape check on `PassExpr` / `BreakExpr`.
5. **`src/Sema/Sema_Expr_Member.cpp`**:
   - Lines 153–155: Fallback `maskToCheck = Info->DirtyReferentMask;`.

---

## 4. Conclusion & Next Steps

`DirtyReferentMask` is completely unreachable by any valid program, provides no value for projection borrows, and has zero legitimate consumers. It should be scheduled for direct deletion in the subsequent Class B/C migration steps without introducing intermediate `ReferentAuthority` abstractions.
