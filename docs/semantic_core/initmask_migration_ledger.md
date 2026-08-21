# InitMask Migration Ledger

**Status:** Active prioritized inventory; exhaustive enumeration pending.

This document records every usage, mutation, compatibility synchronization,
flow snapshot/merge, constructor synthesis, and dirty-reference tracking point
for legacy `InitMask`, `DirtyReferentMask`, and `m_LastInitMask` across Toka
Semantic Analysis (`src/Sema/` and `include/toka/`).

> [!NOTE]
> **Observed Baseline at Commit `441c42f5`**:
> - Fail test suite: `361 passed, 0 failed` (via `tools/scripts/test_verify_fail.py`)
> - Conformance test suite: `299 passed, 0 failed` (via `tools/run_conformance.py`)
> - Semantic TKI replay suite: `44 passed, 0 failed` (via `tools/scripts/test_semantic_replay.sh`)
> - CTest unit suite: `15 passed, 0 failed` (via `ctest --test-dir build`)

---

## 1. Six Functional Roles of Legacy Mask Symbols

Every appearance of mask symbols in the compiler falls into one of six distinct
functional roles:

1. **Semantic Read**: Actively evaluates a mask to accept or reject a program
   construct (e.g. read legality, member unset check, writability).
2. **Semantic Write**: Updates liveness/initialization bits in response to an AST
   construction, handoff, or assignment.
3. **Compatibility Synchronization**: Bridges `ExactPlaceFacts` into `InitMask`
   via `applyToLegacyInitMask(...)` so legacy readers see consistent state.
4. **Control-Flow Snapshot & Merge**: Captures, restores, and bitwise-ANDs
   branch states across `if`, `guard`, `match`, and `loop` joins.
5. **Constructor Synthesis (`m_LastInitMask`)**: Synthesizes aggregate
   initialization status during struct/record/tuple literal analysis.
6. **Dirty Reference Tracking (`DirtyReferentMask`)**: Tracks referent
   cleanliness when an `&mut` reference temporarily borrows an incomplete place.

---

## 2. Priority Classification Taxonomy

| Class | Category | Target Strategy / Precondition |
|---|---|---|
| **Class A** | **Whole-Place Semantic Decisions** | Migrate directly to `SymbolInfo::placeFact()` (`PlaceStateFact`) and `ExactPlaceFacts::isDefinitelyLive()`. |
| **Class B** | **Projection & Member Liveness** | Governed by the new Projection Tracking Requirements. Exact-first with legacy fallback until non-admitted projections are formalized. |
| **Class C** | **Dirty Reference Referent State** | Deletion candidate pending executable reachability proof via Debug invariant and verified projection lifetime closure (`BorrowedPath` / `LifeDependencySet`). |
| **Class D** | **Initializer Synthesis & Expression State** | Retain `m_LastInitMask` as an expression-level constructor synthesis helper, strictly decoupled from statement-level delayed-init facts. |

---

## 3. Prioritized Decision & Mutation Inventory

### 3.1 Semantic Read Sites

| Function / File | Operation | Current Decision / Role | ExactPlace Equivalent | Classification & Migration Note |
|---|---|---|---|---|
| `checkVariableExpr`<br>`Sema_Expr.cpp:1954-1972` | Semantic Read | Fallback check for uninitialized aggregate / non-admitted shape read rejection (`ERR_USE_UNSET`) | `ExactPlace.isDefinitelyLive()` (already used for plain scalars & admitted projections) | **Class B/C Fallback**: Plain scalars already use `ExactPlace`. Mask serves non-admitted aggregates and references. |
| `checkMemberExpr`<br>`Sema_Expr_Member.cpp:152-178` | Semantic Read | Reads `maskToCheck` for field read availability when `!usesExactProjection` (`ERR_USE_UNSET`) | `ExactPlace.projectionFact(DirectField, i)` (only for admitted direct fields) | **Class B Fallback**: Non-admitted structs/tuples fall back to `maskToCheck`. Requires `InitializationProjectionPlan`. |
| `checkArrayIndexExpr`<br>`Sema_Expr_Member.cpp:567-573` | Semantic Read | Checks `Info->InitMask & (1ULL << idx)` for fixed-array element read availability (`ERR_USE_UNSET`) | `ExactPlace.projectionFact(FixedArrayElement, idx)` | **Class B Fallback**: Non-admitted arrays fall back to `InitMask`. |
| `checkBinaryExpr`<br>`Sema_Expr_Binary.cpp:728-730` | Semantic Read | Fallback writability check: `InfoPtr->InitMask != ~0ULL` grants `isLHSWritable` | `hasPlaceState(InfoPtr->placeFact(), Never)` (already used for admitted locals) | **Class A/B Fallback**: Plain locals already use `ExactPlace`; mask serves legacy non-admitted aggregates. |
| `checkBinaryExpr`<br>`Sema_Expr_Binary.cpp:874-876` | Semantic Read | Checks `!(EffectiveInfo->InitMask & bit)` for member LHS uninitialized writability | `!hasExactlyPlaceState(projectionFact(DirectField, i), Live)` | **Class B Fallback**: Fallback for non-admitted member assignments. |
| `checkBlockStmt`<br>`Sema_Stmt.cpp:547` | Semantic Read | Checks `(sourceInfo->InitMask & signature) != signature` for dirty reference escape (`ERR_DIRTY_REF_ESCAPE`) | Structured Borrow / Lifetime Check (`BorrowedPath`) | **Class C**: Candidate for deletion pending executable unreachable invariant verification. |
| `checkReturnStmt`<br>`Sema_Stmt.cpp:709` | Semantic Read | Checks `DirtyReferentMask != ~0ULL` at return exit | Structured Return Contract (`LifeDependencySet`) | **Class C**: Candidate for deletion pending executable unreachable invariant verification. |
| `checkVarDecl`<br>`Sema_Stmt.cpp:1905` | Semantic Read | Checks `(srcPtr->InitMask & fullMask) != fullMask` to detect reference borrowing uninit referent | Fail-Closed Borrow Gate & `PlaceState` | **Class C**: Candidate for deletion pending executable unreachable invariant verification. |

### 3.2 Semantic Write Sites (Exact-First with Fallback)

| Function / File | Operation | Current Mutation & Role | Migration Strategy |
|---|---|---|---|
| `checkFunctionDecl`<br>`Sema.cpp:4600` | Semantic Write | `Info.InitMask = 0` for `init` formal parameter | Retain synchronization while `InitMask` exists; primary authority is `setWhole(PlaceState::Never)`. |
| `checkCallExpr`<br>`Sema_Expr_Call.cpp:2692, 2696` | Semantic Write | Sets `place->InitMask = 0` or `~0ULL` for outcome-matched post-states | Primary authority is `transitionWhole(...)`; mask write is compatibility sync. |
| `propagateInit`<br>`Sema_Expr_Binary.cpp:1282-1284` | Semantic Write | Updates `Sym->InitMask` on assignment propagation | **P0 Defect**: `isPartial` sets non-reference root `InitMask` to `~0ULL`, admitting uninitialized sibling reads and omitting `DropFlag`. Must fail-closed by rejecting ordinary borrow of non-Live places. |
| `checkBinaryExpr`<br>`Sema_Expr_Binary.cpp:1309` | Semantic Write | Sets `Info->InitMask = ~0ULL` on whole-place assignment | Primary authority is `transitionWhole(...)` & `repopulateAllProjections()`. |
| `checkBinaryExpr`<br>`Sema_Expr_Binary.cpp:1360` | Semantic Write | `Info->InitMask |= bitsToSet` (Exact-first with fallback) | Exact projection transition is attempted first; fallback bitmask is updated. |
| `checkBinaryExpr`<br>`Sema_Expr_Binary.cpp:1386` | Semantic Write | `Info->InitMask |= (1ULL << constant)` (Exact-first with fallback) | Exact index transition is attempted first; fallback bitmask is updated. |
| `checkVarDecl`<br>`Sema_Stmt.cpp:2063-2068` | Semantic Write | Assigns `Info.InitMask = m_LastInitMask` or `0` on declaration | Decouple constructor expression synthesis from variable fact storage. |

### 3.3 Compatibility Synchronization Sites

| Function / File | Purpose & Role |
|---|---|
| `Sema.cpp:874, 878` | Synchronizes `ExactPlace` and `InitMask` during partial-move plan installation |
| `Sema_Expr_Call.cpp:951-952` | Calls `applyToLegacyInitMask` after callee `init` argument handoff |
| `Sema_Expr.cpp:701` | Calls `applyToLegacyInitMask` inside `restoreVisibleAnalysisState` |
| `Sema_Expr.cpp:2615-2616` | Synchronizes `InitMask` from `hasExactlyPlaceState(placeFact(), Live)` in `if/else` |
| `Sema_Expr.cpp:2739, 5166` | Calls `applyToLegacyInitMask` during `guard` and `for` state restorations |
| `include/toka/Sema.h:66` | `installPartialMovePlan`: sets plan and applies `InitMask` |

### 3.4 Control-Flow Snapshot & Merge Sites

| Function / File | Construct | Current Parallel Merge Pattern |
|---|---|---|
| `Sema_Expr.cpp:632, 690, 722-743` | `AnalysisState` | Concurrently captures `InitMasks` and `ExactPlaces`; merges via bitwise-AND alongside `ExactPlaceFacts::operator|=`. |
| `Sema_Expr.cpp:2442, 2549, 2582, 2594` | `if` / `else` | Independently merges `thenM & elseM` and `thenExactPlaces | elseExactPlaces`. |
| `Sema_Expr.cpp:2723, 2731, 2747, 2844, 2856, 2869, 2891` | `guard` | Manages `masksBefore`, `masksThen`, `masksElse` alongside `ExactPlaceFacts`. |
| `Sema_Expr.cpp:2977, 3015, 3026` | `loop` | Merges `entryMask & bodyMask` alongside `ExactPlaceFacts`. |
| `Sema_Expr.cpp:3380, 3410` | `match` | Merges `masksBody & masksElse` alongside `ExactPlaceFacts`. |
| `Sema_Expr.cpp:5145, 5158, 5232, 5240, 5281` | `for` loops | Merges arm masks alongside `ExactPlaceFacts`. |

### 3.5 Constructor Synthesis Sites (`m_LastInitMask`)

| Function / File | Purpose & Role |
|---|---|
| `Sema_Expr.cpp:1005, 1023, 1364, 1979, 2092, 3950` | Resets or sets `m_LastInitMask` around sub-expression evaluation |
| `Sema_Expr_Init.cpp:1234, 1353, 1407, 1417, 1425, 1443, 1487, 1530` | Synthesizes field masks for struct, tuple, anonymous record, and variant initializers |
| `Sema_Stmt.cpp:2063` | Stores synthesized constructor mask into `SymbolInfo::InitMask` |

---

## 4. Complete Raw Symbol Inventory

```text
src/Sema/Sema.cpp:874:    info.ExactPlace.setPlan(info.partialMovePlan(), info.InitMask);
src/Sema/Sema.cpp:878:  info.InitMask = info.ExactPlace.applyToLegacyInitMask(info.InitMask);
src/Sema/Sema.cpp:4600:      Info.InitMask = 0;
src/Sema/Sema_Expr_Call.cpp:951:            it->second.InitMask =
src/Sema/Sema_Expr_Call.cpp:952:                exactPlace.second.applyToLegacyInitMask(it->second.InitMask);
src/Sema/Sema_Expr_Call.cpp:2692:        place->InitMask = 0;
src/Sema/Sema_Expr_Call.cpp:2696:        place->InitMask = ~0ULL;
src/Sema/Sema_Expr_Init.cpp:1234:    memberMasks[pair.first] = m_LastInitMask;
src/Sema/Sema_Expr_Init.cpp:1353:        memberMasks[defField.Name] = m_LastInitMask;
src/Sema/Sema_Expr_Init.cpp:1407:        m_LastInitMask = memberMasks[pair.first];
src/Sema/Sema_Expr_Init.cpp:1417:        if ((m_LastInitMask & expected) == expected) {
src/Sema/Sema_Expr_Init.cpp:1425:  m_LastInitMask = mask;
src/Sema/Sema_Expr_Init.cpp:1443:    m_LastInitMask = 0;
src/Sema/Sema_Expr_Init.cpp:1487:    m_LastInitMask = ~0ULL;
src/Sema/Sema_Expr_Init.cpp:1530:  m_LastInitMask = ~0ULL;
src/Sema/Sema_Expr_Member.cpp:152:          uint64_t maskToCheck = Info->InitMask;
src/Sema/Sema_Expr_Member.cpp:154:            maskToCheck = Info->DirtyReferentMask;
src/Sema/Sema_Expr_Member.cpp:568:                     (Info->InitMask & (1ULL << constant->Value)) != 0);
include/toka/Sema.h:50:  uint64_t InitMask =
include/toka/Sema.h:53:  // projections. `InitMask` remains a compatibility liveness view until its
include/toka/Sema.h:66:    ExactPlace.setPlan(plan, InitMask);
include/toka/Sema.h:70:  // If this symbol is a Reference (&T), this mask tracks the InitMask of the
include/toka/Sema.h:74:  uint64_t DirtyReferentMask = ~0ULL;
include/toka/Sema.h:407:  uint64_t m_LastInitMask =
src/Sema/Sema_Expr_Binary.cpp:722:          if (InfoPtr->DirtyReferentMask != ~0ULL)
src/Sema/Sema_Expr_Binary.cpp:728:          if (InfoPtr->InitMask != ~0ULL)
src/Sema/Sema_Expr_Binary.cpp:874:                    isUnset = !(EffectiveInfo->InitMask & bit) &&
src/Sema/Sema_Expr_Binary.cpp:875:                              !(EffectiveInfo->DirtyReferentMask & bit);
src/Sema/Sema_Expr_Binary.cpp:1257:    // [Fix] Update InitMask logic for uninitialized variables
src/Sema/Sema_Expr_Binary.cpp:1277:            Sym->DirtyReferentMask |= updateBits;
src/Sema/Sema_Expr_Binary.cpp:1279:            Sym->DirtyReferentMask = ~0ULL;
src/Sema/Sema_Expr_Binary.cpp:1282:            Sym->InitMask = ~0ULL;
src/Sema/Sema_Expr_Binary.cpp:1284:            Sym->InitMask |= updateBits;
src/Sema/Sema_Expr_Binary.cpp:1304:          Info->DirtyReferentMask = ~0ULL;
src/Sema/Sema_Expr_Binary.cpp:1309:          Info->InitMask = ~0ULL;
src/Sema/Sema_Expr_Binary.cpp:1348:                  Info->DirtyReferentMask |= bitsToSet;
src/Sema/Sema_Expr_Binary.cpp:1360:                    Info->InitMask |= bitsToSet;
src/Sema/Sema_Expr_Binary.cpp:1386:              Info->InitMask |= (1ULL << constant->Value);
src/Sema/Sema_Expr.cpp:632:        masks[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:690:      info->InitMask = pair.second;
src/Sema/Sema_Expr.cpp:701:      info->InitMask = pair.second.applyToLegacyInitMask(info->InitMask);
src/Sema/Sema_Expr.cpp:1005:  m_LastInitMask = ~0ULL;
src/Sema/Sema_Expr.cpp:1023:    m_LastInitMask = ~0ULL;
src/Sema/Sema_Expr.cpp:1364:    m_LastInitMask = 0;
src/Sema/Sema_Expr.cpp:1954:      } else if (Info.InitMask == 0) {
src/Sema/Sema_Expr.cpp:1967:            if ((Info.InitMask & expected) != expected) {
src/Sema/Sema_Expr.cpp:1979:    m_LastInitMask = Info.InitMask;
src/Sema/Sema_Expr.cpp:2092:        m_LastInitMask = 0;
src/Sema/Sema_Expr.cpp:2442:      infoPtr->InitMask = state == PlaceState::Never ? 0 : ~0ULL;
src/Sema/Sema_Expr.cpp:2549:          info->InitMask = thenM & elseM;
src/Sema/Sema_Expr.cpp:2582:        info->InitMask = pair.second;
src/Sema/Sema_Expr.cpp:2594:            info->InitMask = ~0ULL;
src/Sema/Sema_Expr.cpp:2615:            info->InitMask = hasExactlyPlaceState(info->placeFact(),
src/Sema/Sema_Expr.cpp:2723:      masksBefore[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:2731:        CurrentScope->Symbols[pair.first].InitMask = pair.second;
src/Sema/Sema_Expr.cpp:2739:        info.InitMask = pair.second.applyToLegacyInitMask(info.InitMask);
src/Sema/Sema_Expr.cpp:2747:        masks[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:2844:            pair.second.InitMask = masksElse[pair.first];
src/Sema/Sema_Expr.cpp:2856:            pair.second.InitMask = masksThen[pair.first];
src/Sema/Sema_Expr.cpp:2869:          pair.second.InitMask = thenMask & elseMask;
src/Sema/Sema_Expr.cpp:2891:          pair.second.InitMask = thenMask & entryMask;
src/Sema/Sema_Expr.cpp:2977:      masksBefore[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:3015:        masksBody[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:3026:        pair.second.InitMask = entryMask & bodyMask;
src/Sema/Sema_Expr.cpp:3380:          info->InitMask = bodyMask & elseMask;
src/Sema/Sema_Expr.cpp:3410:          info->InitMask = entryMask & bodyMask;
src/Sema/Sema_Expr.cpp:3685:          if (info->IsReference() && info->DirtyReferentMask != ~0ULL) {
src/Sema/Sema_Expr.cpp:3732:          if (info->IsReference() && info->DirtyReferentMask != ~0ULL) {
src/Sema/Sema_Expr.cpp:3950:    m_LastInitMask = ~0ULL;
src/Sema/Sema_Expr.cpp:5145:      masksBefore[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:5158:        CurrentScope->Symbols[pair.first].InitMask = pair.second;
src/Sema/Sema_Expr.cpp:5166:        info.InitMask = pair.second.applyToLegacyInitMask(info.InitMask);
src/Sema/Sema_Expr.cpp:5197:              placeInfo->InitMask = isLive ? ~0ULL : 0;
src/Sema/Sema_Expr.cpp:5232:            mergedMasks[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:5240:            uint64_t armMask = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:5281:          pair.second.InitMask = mergedMasks[pair.first];
src/Sema/Sema_Stmt.cpp:513:    // If any symbol is a Reference with DirtyReferentMask != Full,
src/Sema/Sema_Stmt.cpp:520:    // DirtyReferentMask on assignment. BETTER APPROACH based on plan: "Check if
src/Sema/Sema_Stmt.cpp:526:      if (info.IsReference() && info.DirtyReferentMask != ~0ULL) {
src/Sema/Sema_Stmt.cpp:529:        // Actually, we should check the SOURCE variable's current InitMask.
src/Sema/Sema_Stmt.cpp:547:          if ((sourceInfo->InitMask & signature) != signature) {
src/Sema/Sema_Stmt.cpp:709:          if (info->IsReference() && info->DirtyReferentMask != ~0ULL) {
src/Sema/Sema_Stmt.cpp:1892:        // [Hot Potato] Propagate InitMask from Source to Reference
src/Sema/Sema_Stmt.cpp:1905:        if ((srcPtr->InitMask & fullMask) != fullMask) {
src/Sema/Sema_Stmt.cpp:1907:          Info.DirtyReferentMask = srcPtr->InitMask;
src/Sema/Sema_Stmt.cpp:1909:          Info.DirtyReferentMask = ~0ULL; // Clean
src/Sema/Sema_Stmt.cpp:2063:      Info.InitMask = m_LastInitMask;
src/Sema/Sema_Stmt.cpp:2065:      Info.InitMask = 0;
src/Sema/Sema_Stmt.cpp:2068:        Info.InitMask == 0 ? PlaceState::Never : PlaceState::Live;
```
