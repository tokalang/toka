# P0.4 Execution Plan: Exact-Place Fact and Eligibility Unification

**Status:** Active internal migration. P0.4a, P0.4b, and P0.4c are
implemented for the frozen exact-place carrier. P0.4d remains before any
P0.4 completion claim. P0.4a introduced
`ExactPlaceFacts` and moved both `SymbolInfo` and the central `AnalysisState`
capture/merge path to it. P0.4b moves ordinary `if`/`else`, `guard`, `loop`,
`for`, `match`, and call-candidate rollback to the same value; `break` and
`continue` use the central carrier. This document adds no source syntax, does
not widen the bounded partial-`cede` matrix, and does not qualify the
async/place bridge.

**Authority:** [`place_state_core_rfc.md`](place_state_core_rfc.md) defines
the PlaceState contract. [`partial_cede_lifecycle_rfc.md`](partial_cede_lifecycle_rfc.md)
defines the admitted direct-field and fixed-array subset. This plan sequences
their implementation closure only.

## 1. Problem being closed

The current compiler has the right ingredients but still maintains them in
parallel on `SymbolInfo` and CFG snapshots:

```text
whole local          PlaceStateFact
admitted projection  ProjectionPlaceFacts + PartialMovePlan
legacy liveness       InitMask
legacy move marker    Moved + MoveLoc
```

`ProjectionPlaceFacts` already derives its tracked `InitMask` bits, and
`PartialMovePlan` already reaches CodeGen. The remaining risk is that an
operation or a control-flow merge consults one of these representations without
the others. P0.4 removes that split for the frozen whole-place/direct-projection
surface; it is not a proposal for general typestate or nested place tracking.

## 2. Frozen P0.4 model

Introduce one internal value object, `ExactPlaceFacts`, with:

```text
whole:        PlaceStateFact
eligibility:  PartialMovePlan
projections:  ProjectionPlaceFacts  // present only when eligibility is admitted
```

Its only supported coordinates are the root whole place and an eligible direct
field or fixed-array constant index. It provides queries, state transitions,
and CFG join for those coordinates. A projection query that is not admitted is
not a second, unknown fact: it is an unsupported operation and must retain the
current fail-closed diagnostic.

`PartialMovePlan` remains the one Sema elaboration copied through the AST to
CodeGen. CodeGen does not recompute shape, custom-drop, shared-member, or
array-bound eligibility. The runtime drop mask remains a lowering of the same
plan and concrete live projections; it is neither a source of authority nor a
replacement for the static fact.

During migration, `InitMask` is a one-way compatibility view derived from
`ExactPlaceFacts` at legacy boundaries. `Moved` and `MoveLoc` may remain as
diagnostic metadata temporarily, but the admitted exact-place decisions must
come from the new fact. P0.4 must not merge PAL, H/P authority, reference
`DirtyReferentMask`, or runtime cleanup ownership into this value object. Those
are independent sorts that continue to be snapshotted and checked alongside
the exact-place fact.

## 3. Migration slices

### P0.4a: Symbol and analysis-state consolidation

The initial implementation completed the model and storage half:
`ExactPlaceFacts` owns the whole fact, plan, and projection facts on
`SymbolInfo`, and `AnalysisState` uses that one value for its capture/merge
path. Its unit test covers projection transitions, joins, legacy-liveness
derivation, and fail-closed mismatched plans. The remaining ad-hoc CFG
snapshots belong to P0.4b; their copied `InitMask`/whole/projection
compatibility views are not evidence of a unified join authority.

Replace the parallel whole/projection fact fields on `SymbolInfo` and the
parallel `PlaceFacts`/`ProjectionFacts` maps in `AnalysisState` with one
`ExactPlaceFacts` value per local. Add explicit API operations for:

- whole and admitted-projection fact lookup;
- `Never -> Live`, `Live -> Moved`, and admitted `Moved -> Live` commits;
- installation of an elaborated `PartialMovePlan`; and
- conservative join.

The model unit test extends `tests/PlaceStateFact.cpp` with whole/projection
independence, `Never` versus `Moved`, branch joins, plan-kind mismatch, and
unsupported-coordinate cases. No user-visible diagnostic or lowering changes
in this slice.

### P0.4b: CFG ownership of the fact (implemented, not P0.4 closure)

The ordinary `if`/`else`, `guard`, conditional `loop`, `for`, and `match`
capture, restore, reachable-branch selection, and join paths now carry
`ExactPlaceFacts` as one value. They keep PAL, conditional-editor facts where
applicable, `InitMask`, and `Moved` compatibility snapshots in the same
transaction; the admitted whole/projection join no longer rebuilds from
separate maps. `break` and `continue` already use the central `AnalysisState`
path. Call-candidate rollback now restores the same full fact. Outcome arms
are the `match` arm transaction rather than a separate snapshot carrier, and
therefore use the migrated `match` path. The old separated
whole/projection snapshot helpers have been removed.

Each path continues to snapshot PAL and direct-flow ceilings in the same
transaction; no specialized merge may recreate a direct `InitMask`-only path
for an admitted coordinate.

This slice may retain compatibility-mask synchronization at legacy consumers,
but no admitted exact-place join may derive its answer by intersecting the
legacy mask first.

### P0.4c: Operation and lowering handoff (implemented, not P0.4 closure)

`ExactPlaceFacts` now exposes the whole-place transitions used by the frozen
surface and the exact-plan operation that restores all admitted projections to
Live. Direct `init` commits `Never -> Live`; synchronous `init`-formal calls
commit either `Never -> Live` or the private Outcome `{Never, Live}` state;
an Outcome arm commits that private state to its declared post-state. Whole
`cede` commits `Live -> Moved` through the scope transition, while admitted
direct-field and fixed-array transfers and their assignments use the matching
projection transition. A full repopulation uses the already-elaborated plan;
it does not rebuild eligibility from the aggregate type.

The compatibility `InitMask`/`Moved` updates remain in the same synchronous
Sema operation as those commits. This is the defined non-suspending boundary;
the async/place bridge is still excluded.

The CodeGen handoff audit confirms that `VariableDecl` and pattern binders
copy that same AST-carried `PartialMovePlan`, seed `DropMask` from its eligible
mask, and check that plan again before clearing or restoring a concrete drop
bit. CodeGen does not recompute shape, custom-drop, sharing, array-bound, or
projection eligibility. Existing direct-field/fixed-index cleanup and
repopulation runs cover the handoff; this slice does not redesign drop
lowering. Unsupported custom-drop, shared-member, dynamic, nested, nonlocal,
and over-64 forms remain rejected before CodeGen.

### P0.4d: Compatibility retirement and qualification

Retire semantic reads of `InitMask` and `Moved` for the admitted exact-place
matrix. Any residual use must be either a documented legacy/reference boundary
or diagnostic location metadata, never the deciding state authority. Record
the remaining non-admitted uses explicitly rather than silently treating them
as P0.4 coverage.

Re-run the bounded partial-`cede`, delayed-init, Outcome, source-less retained
body replay, and exact-once cleanup matrices, followed by the full release
qualification at the candidate revision.

## 4. Completion conditions

P0.4 is complete only when all are true:

1. `SymbolInfo` and `AnalysisState` each own one exact-place fact value for the
   frozen matrix, rather than independent whole/projection semantic fields.
2. Every admitted control-flow join uses that value's conservative join and
   preserves its accompanying PAL/direct-flow snapshots.
3. Every admitted partial projection has one Sema-elaborated eligibility plan
   that is carried to CodeGen; no duplicate eligibility admission is used.
4. `InitMask` and `Moved` do not decide an admitted exact-place transition or
   availability check.
5. Source-backed and retained-body source-less runs agree on the existing
   accepted and rejected matrix, and concrete live resources still drop exactly
   once on normal, early-return, and supported unwind paths.

The P0.4 completion claim still excludes state across `.await`, terminal
cancellation, bodyless object-attested replay, custom drop, and all projection
forms outside the frozen direct-field/fixed-array matrix.
