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
| **Class C** | **Dirty Reference Referent State** | **Completed Deletion**: Cleanly removed `DirtyReferentMask` across the compiler (`c169e2f2`). Lifetimes and escapes fully enforced via `BorrowedPath` and `LifeDependencySet`. |
| **Class D** | **Initializer Synthesis & Expression State** | Retain `m_LastInitMask` as an expression-level constructor synthesis helper, strictly decoupled from statement-level delayed-init facts. |

---

## 3. Prioritized Decision & Mutation Inventory

### 3.1 Semantic Read Sites

| Function / File | Operation | Current Decision / Role | ExactPlace Equivalent | Classification & Migration Note |
|---|---|---|---|---|
| `checkVariableExpr`<br>`Sema_Expr.cpp:1954-1972` | Semantic Read | Fallback check for uninitialized aggregate / non-admitted shape read rejection (`ERR_USE_UNSET`) | `ExactPlace.isDefinitelyLive()` (already used for plain scalars & admitted projections) | **Class B Fallback**: Plain scalars already use `ExactPlace`. Mask serves non-admitted aggregates. |
| `checkMemberExpr`<br>`Sema_Expr_Member.cpp:152-178` | Semantic Read | Reads `maskToCheck` for field read availability when `!usesExactProjection` (`ERR_USE_UNSET`) | `ExactPlace.projectionFact(DirectField, i)` (only for admitted direct fields) | **Class B Fallback**: Non-admitted structs/tuples fall back to `maskToCheck`. Requires `InitializationProjectionPlan`. |
| `checkArrayIndexExpr`<br>`Sema_Expr_Member.cpp:567-573` | Semantic Read | Checks `Info->InitMask & (1ULL << idx)` for fixed-array element read availability (`ERR_USE_UNSET`) | `ExactPlace.projectionFact(FixedArrayElement, idx)` | **Class B Fallback**: Non-admitted arrays fall back to `InitMask`. |
| `checkBinaryExpr`<br>`Sema_Expr_Binary.cpp:728-730` | Semantic Read | Fallback writability check: `InfoPtr->InitMask != ~0ULL` grants `isLHSWritable` | `hasPlaceState(InfoPtr->placeFact(), Never)` (already used for admitted locals) | **Class A/B Fallback**: Plain locals already use `ExactPlace`; mask serves legacy non-admitted aggregates. |
| `checkBinaryExpr`<br>`Sema_Expr_Binary.cpp:874-876` | Semantic Read | Checks `!(EffectiveInfo->InitMask & bit)` for member LHS uninitialized writability | `!hasExactlyPlaceState(projectionFact(DirectField, i), Live)` | **Class B Fallback**: Fallback for non-admitted member assignments. |
| `checkBlockStmt`<br>`Sema_Stmt.cpp` | Semantic Read | *(Deleted)* Formerly checked `DirtyReferentMask` for scope exit | Structured Borrow / Lifetime Check (`BorrowedPath`) | **Class C (Completed)**: Cleanly deleted in `c169e2f2`. |
| `checkReturnStmt`<br>`Sema_Stmt.cpp` | Semantic Read | *(Deleted)* Formerly checked `DirtyReferentMask` at return exit | Structured Return Contract (`LifeDependencySet`) | **Class C (Completed)**: Cleanly deleted in `c169e2f2`. |
| `checkVarDecl`<br>`Sema_Stmt.cpp` | Semantic Read | *(Deleted)* Formerly checked `(srcPtr->InitMask & fullMask)` to set `DirtyReferentMask` | Fail-Closed Borrow Gate & `PlaceState` | **Class C (Completed)**: Cleanly deleted in `c169e2f2`. |

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
src/Sema/Sema_Expr_Binary.cpp:942:                    isUnset = !(EffectiveInfo->InitMask & bit);
src/Sema/Sema_Expr_Binary.cpp:1308:    // [Fix] Update InitMask logic for uninitialized variables
src/Sema/Sema_Expr_Binary.cpp:1327:          Sym->InitMask |= updateBits;
src/Sema/Sema_Expr_Binary.cpp:1346:          Info->InitMask = ~0ULL;
src/Sema/Sema_Expr_Binary.cpp:1390:                    Info->InitMask |= bitsToSet;
src/Sema/Sema_Expr_Binary.cpp:1416:              Info->InitMask |= (1ULL << constant->Value);
src/Sema/Sema_Expr.cpp:627:static std::map<std::string, uint64_t> captureVisibleInitMasks(Scope *ScopePtr) {
src/Sema/Sema_Expr.cpp:632:        masks[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:690:      info->InitMask = pair.second;
src/Sema/Sema_Expr.cpp:701:      info->InitMask = pair.second.applyToLegacyInitMask(info->InitMask);
src/Sema/Sema_Expr.cpp:708:  state.InitMasks = captureVisibleInitMasks(CurrentScope);
src/Sema/Sema_Expr.cpp:722:  std::map<std::string, uint64_t> mergedMasks = states.front().InitMasks;
src/Sema/Sema_Expr.cpp:735:    for (const auto &pair : state.InitMasks) {
src/Sema/Sema_Expr.cpp:741:          state.InitMasks.count(pair.first) ? state.InitMasks.at(pair.first) : 0;
src/Sema/Sema_Expr.cpp:1005:  m_LastInitMask = ~0ULL; // Default to fully set
src/Sema/Sema_Expr.cpp:1023:    m_LastInitMask = ~0ULL;
src/Sema/Sema_Expr.cpp:1364:    m_LastInitMask = 0;
src/Sema/Sema_Expr.cpp:2016:      } else if (Info.InitMask == 0) {
src/Sema/Sema_Expr.cpp:2029:            if ((Info.InitMask & expected) != expected) {
src/Sema/Sema_Expr.cpp:2041:    m_LastInitMask = Info.InitMask;
src/Sema/Sema_Expr.cpp:2154:        m_LastInitMask = 0;
src/Sema/Sema_Expr.cpp:2504:      infoPtr->InitMask = state == PlaceState::Never ? 0 : ~0ULL;
src/Sema/Sema_Expr.cpp:2528:    auto masksBefore = captureVisibleInitMasks(CurrentScope);
src/Sema/Sema_Expr.cpp:2540:    auto masksThen = captureVisibleInitMasks(CurrentScope);
src/Sema/Sema_Expr.cpp:2575:      auto masksElse = captureVisibleInitMasks(CurrentScope);
src/Sema/Sema_Expr.cpp:2611:          info->InitMask = thenM & elseM;
src/Sema/Sema_Expr.cpp:2644:        info->InitMask = pair.second;
src/Sema/Sema_Expr.cpp:2656:            info->InitMask = ~0ULL;
src/Sema/Sema_Expr.cpp:2677:            info->InitMask = hasExactlyPlaceState(info->placeFact(),
src/Sema/Sema_Expr.cpp:2785:      masksBefore[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:2793:        CurrentScope->Symbols[pair.first].InitMask = pair.second;
src/Sema/Sema_Expr.cpp:2801:        info.InitMask = pair.second.applyToLegacyInitMask(info.InitMask);
src/Sema/Sema_Expr.cpp:2809:        masks[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:2906:            pair.second.InitMask = masksElse[pair.first];
src/Sema/Sema_Expr.cpp:2918:            pair.second.InitMask = masksThen[pair.first];
src/Sema/Sema_Expr.cpp:2931:          pair.second.InitMask = thenMask & elseMask;
src/Sema/Sema_Expr.cpp:2953:          pair.second.InitMask = thenMask & entryMask;
src/Sema/Sema_Expr.cpp:3039:      masksBefore[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:3077:        masksBody[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:3088:        pair.second.InitMask = entryMask & bodyMask;
src/Sema/Sema_Expr.cpp:3308:    auto masksBefore = captureVisibleInitMasks(CurrentScope);
src/Sema/Sema_Expr.cpp:3391:    auto masksBody = captureVisibleInitMasks(CurrentScope);
src/Sema/Sema_Expr.cpp:3413:      masksElse = captureVisibleInitMasks(CurrentScope);
src/Sema/Sema_Expr.cpp:3442:          info->InitMask = bodyMask & elseMask;
src/Sema/Sema_Expr.cpp:3472:          info->InitMask = entryMask & bodyMask;
src/Sema/Sema_Expr.cpp:3986:    m_LastInitMask = ~0ULL;
src/Sema/Sema_Expr.cpp:5181:      masksBefore[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:5194:        CurrentScope->Symbols[pair.first].InitMask = pair.second;
src/Sema/Sema_Expr.cpp:5202:        info.InitMask = pair.second.applyToLegacyInitMask(info.InitMask);
src/Sema/Sema_Expr.cpp:5233:              placeInfo->InitMask = isLive ? ~0ULL : 0;
src/Sema/Sema_Expr.cpp:5268:            mergedMasks[pair.first] = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:5276:            uint64_t armMask = pair.second.InitMask;
src/Sema/Sema_Expr.cpp:5317:          pair.second.InitMask = mergedMasks[pair.first];
src/Sema/Sema_Expr_Init.cpp:1234:    memberMasks[pair.first] = m_LastInitMask;
src/Sema/Sema_Expr_Init.cpp:1353:        memberMasks[defField.Name] = m_LastInitMask;
src/Sema/Sema_Expr_Init.cpp:1407:        m_LastInitMask = memberMasks[pair.first];
src/Sema/Sema_Expr_Init.cpp:1417:        if ((m_LastInitMask & expected) == expected) {
src/Sema/Sema_Expr_Init.cpp:1425:  m_LastInitMask = mask;
src/Sema/Sema_Expr_Init.cpp:1443:    m_LastInitMask = 0;
src/Sema/Sema_Expr_Init.cpp:1487:    m_LastInitMask = ~0ULL;
src/Sema/Sema_Expr_Init.cpp:1530:  m_LastInitMask = ~0ULL;
src/Sema/Sema_Stmt.cpp:2021:      Info.InitMask = 0;
src/Sema/Sema_Stmt.cpp:2024:      Info.InitMask = (m_LastInitMask == 0) ? ~0ULL : m_LastInitMask;
src/Sema/Sema_Expr_Member.cpp:152:          uint64_t maskToCheck = Info->InitMask;
src/Sema/Sema_Expr_Member.cpp:565:                     (Info->InitMask & (1ULL << constant->Value)) != 0);
src/Sema/Sema_Expr_Call.cpp:951:            it->second.InitMask =
src/Sema/Sema_Expr_Call.cpp:952:                exactPlace.second.applyToLegacyInitMask(it->second.InitMask);
src/Sema/Sema_Expr_Call.cpp:2692:        place->InitMask = 0;
src/Sema/Sema_Expr_Call.cpp:2696:        place->InitMask = ~0ULL;
src/Sema/Sema.cpp:874:    info.ExactPlace.setPlan(info.partialMovePlan(), info.InitMask);
src/Sema/Sema.cpp:878:  info.InitMask = info.ExactPlace.applyToLegacyInitMask(info.InitMask);
src/Sema/Sema.cpp:4600:      Info.InitMask = 0;
include/toka/Sema.h:50:  uint64_t InitMask =
include/toka/Sema.h:53:  // projections. `InitMask` remains a compatibility liveness view until its
include/toka/Sema.h:66:    ExactPlace.setPlan(plan, InitMask);
include/toka/Sema.h:400:  uint64_t m_LastInitMask =
include/toka/Sema.h:592:    std::map<std::string, uint64_t> InitMasks;
include/toka/PlaceState.h:147:  static constexpr ProjectionPlaceFacts fromLegacyInitMask(uint64_t tracked,
include/toka/PlaceState.h:175:  constexpr uint64_t applyToLegacyInitMask(uint64_t legacy) const {
include/toka/PlaceState.h:266:  constexpr void setPlan(PartialMovePlan plan, uint64_t legacyInitMask) {
include/toka/PlaceState.h:269:                        ? ProjectionPlaceFacts::fromLegacyInitMask(
include/toka/PlaceState.h:270:                              plan.eligibleMask(), legacyInitMask)
include/toka/PlaceState.h:287:  constexpr uint64_t applyToLegacyInitMask(uint64_t legacy) const {
include/toka/PlaceState.h:288:    return m_Projections.applyToLegacyInitMask(legacy);
include/toka/PlaceState.h:318:                        ? ProjectionPlaceFacts::fromLegacyInitMask(
```
