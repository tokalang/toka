# RC9 M1b-D.3 Ordinary Direct-Call Vertical Slice

**Design status:** Proposed for independent review.

**Implementation status:** Not implemented. M1b.1, ownership commit, CodeGen
consumption, Evidence v2, and caller-spelling activation remain unauthorized.

**Baseline:** `19a94dbf4ac36a21c068ba32fdf5aaa8ae6c483c`.

**Governing language decision:**
[`RC9 Signature-Driven Call Transfer ADR`](rc9_signature_driven_call_transfer_adr.md).

## Route correction

The BUILD_TESTING-only synthetic reference in
`c5e5a5c1d529abc970d5dc6faaf78949df8bbfbf` was rejected as contract-incomplete
and reverted by `19a94dbf4ac36a21c068ba32fdf5aaa8ae6c483c`.

The experiment is retained in Git history as evidence. Its generic action bag,
synthetic region facts, universal integer branch join, digest-based equality,
and entry-point fault injection are not implementation foundations.

D.3 keeps the previously accepted safety invariants—atomicity, complete
identity, liability conservation, region authority, immutable publication,
and Evidence isolation—but withdraws the standalone synthetic semantic engine
and generic journal route.

## Decision

The next implementation must be one real ordinary direct-call Shadow vertical
slice. It introduces no independently implemented transaction framework.
Actual Sema scope, type, PAL, place, dependency, and cleanup facts determine
the minimal isolation and delta APIs required by that slice.

The sole authoritative result is:

```text
ValidatedCall
    CallSiteId
    ResolvedCalleeId
    TransferEdges[]
    EvaluationDelta
    BoundaryDelta
    FinalizationDelta
    SemanticModelPatch
```

There is no generic semantic journal or action interpreter.

## Validated transfer authority

Each immutable `ValidatedTransferEdge` records at least:

```text
TransferEdgeId
ArgumentPlanId
selected formal identity/index
concrete TypeProperties proof
value category
transfer mode
source disposition
source PlaceId / exact-place admission
DestinationId
loan/dependency relation
drop/shared liability source and target
explicit/implicit spelling fact
```

The edge is the only transfer authority. Source disposition remains independent
from transfer mode, so Copy bare/explicit forms cannot be conflated.

Edges and deltas have private construction. A controlled
`ValidatedCallFactory` returns `Expected<ValidatedCall, CallValidationError>`
only after validating the whole direct call. Invalid parents, types, places,
regions, destinations, liability relations, or incomplete facts return an
error; no factory converts them into a nonempty valid identity/result.

Complete structural equality is semantic authority. Digests may accelerate
caches or diagnostics but never establish edge, call, patch, or revision
equality.

## Immutable domain deltas

Delta is not another spelling of journal. No delta exposes public setters,
append operations, generic payload variants, or arbitrary action composition.

### EvaluationDelta

`EvaluationDelta` contains only effects produced by evaluating the selected
actual expression in the real call-analysis child state:

- real value-producing expression effects;
- temporary facts created by that expression; and
- already validated/adopted nested call results.

It contains no outer-formal move, borrow, invalidation, or liability decision.

### BoundaryDelta and FinalizationDelta

`BoundaryDelta` and `FinalizationDelta` are derived exactly once from the full
validated edge set by private factories:

```text
deriveBoundaryDelta(TransferEdges, actual Sema snapshot)
deriveFinalizationDelta(TransferEdges, BoundaryDelta, actual RegionFacts)
```

They cannot be independently constructed or edited.

They are immutable aggregates of real state-domain patches, such as:

```text
PlaceStateDelta
PALDelta
DependencyDelta
CleanupLiabilityDelta
RegionObligationDelta
SemanticModelPatch
```

Every domain patch retains its originating `TransferEdgeId`; cross-domain
validation proves that source, destination, exact-place admission, loan,
cleanup, dependency, and liability facts all describe the same edge.

`RegionFacts` owned by the real semantic model are the sole region/outlives
authority. A caller cannot manufacture region kind/depth beside an opaque ID.

## Minimal call-analysis isolation

D.3 freezes only the interface required by the real direct-call slice:

```text
DirectCallAnalysisChild
    base state/revision identity
    working real Sema state domains used by the slice
    prepare actual and ValidatedCall
    discard()
    adopt(validated result)       future, inactive in first Shadow slice
```

The first implementation always discards the child after emitting internal
Shadow comparison facts. It does not commit ownership or publish production
state.

No standalone transaction target or abstract lifecycle kernel is implemented
or qualified separately. If `adopt`/immutable swap is later activated, its
shape must be justified by this real child and reviewed with a behavior-owning
vertical slice.

## First route and matrix

The slice covers only a resolved, non-generic, non-variadic ordinary direct
function call with one selected formal and no default/synthetic argument.

It must plan these actual/formal matrices using real Sema facts:

| Selected formal and actual | Transfer | Source disposition |
| --- | --- | --- |
| borrowed aggregate formal + place | `BorrowCapture` | `KeepLive` |
| `cede` + exact proven non-Copy place, bare/explicit | `MoveOwned` | `InvalidateWhole` or currently admitted exact projection |
| `cede` + proven Copy place, bare | `CopyValue` | `KeepLive` |
| `cede` + proven Copy place, explicit | `CopyValue` | `InvalidateWhole` or admitted exact projection |
| `cede` + admitted whole temporary, bare/explicit | `ConsumeTemporary` | `NoSourcePlace` |
| indeterminate or ineligible place/admission | reject | no delta/adoption |

The current RC8 caller spelling rule and `E04570` remain active. Bare owning
move planning is Shadow evidence only and cannot invalidate the caller.

## Placement in the real call path

The D.3 planner runs only after ordinary direct-call resolution selects the
concrete declaration/formal and before the legacy checker commits call-
boundary ownership state.

Contextual expression typing and any nested direct call occur inside the same
real analysis child. The planner must not invoke a second post-check walk or
mutate `CallExpr::Args`, `ResolvedFn`, generic caches, diagnostics, Evidence,
PAL, place state, or cleanup liability outside the child.

The existing M1a v3 recorder remains the legacy comparison surface. A new
internal D.3 receipt may be added only if it is versioned separately and cannot
be confused with public Evidence.

## Exclusions

The first slice excludes:

- static/method/callable/indirect/dynamic-trait/extern routes;
- generic deduction or instantiation;
- default, variadic, synthetic, and `init` arguments;
- outcome-governed calls;
- async, `.start`, thread handoff, or execution-boundary escape;
- branch/loop join;
- partial places beyond the currently shared exact-place admission subset;
- CodeGen consumption;
- public Evidence v2 or lint/LSP work; and
- any semantic behavior activation.

Branch join is deliberately absent. It will be designed only when a real
control-flow slice needs it, using each actual state domain's join operation;
there is no universal synthetic semilattice.

## Qualification gates

Before this slice can be called complete, one revision must prove:

1. all matrix rows above use real resolved formal, TypeProperties, PlaceId,
   PAL, dependency, cleanup, and region facts;
2. Boundary/Finalization deltas are factory-derived from edges and expose no
   public arbitrary construction;
3. invalid/incomplete input returns `Expected` error and leaves the complete
   parent-state digest unchanged;
4. zero/one/repeated Shadow probes produce identical normal diagnostics,
   Evidence v1, AST, identities, PAL/place/cleanup state, and compiler output;
5. implicit/explicit non-Copy forms have equal transfer/liability plans while
   Copy forms preserve their required source-disposition difference;
6. temporary cleanup and borrowed call-region loan facts are complete for the
   slice and are not reconstructed by CodeGen;
7. source and source-hidden direct-call plans are equivalent where the slice
   admits both inputs;
8. M1a Shadow, Evidence v1, public JSON CLI, CTest, conformance, pass, and fail
   gates remain green; and
9. no excluded route silently falls back to D.3 authority.

## Admission decision

This document admits no implementation until independently reviewed. After
review it may authorize only the ordinary direct-call Shadow slice above. It
does not authorize M1b.1, semantic adopt/commit, CodeGen consumption, Evidence
v2, branch join, or caller-spelling activation.
