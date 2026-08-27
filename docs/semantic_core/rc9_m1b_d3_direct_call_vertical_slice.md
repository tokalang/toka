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

Before D.3 identity receives any semantic authority, the public raw
`SemanticIdentity::fromCanonicalKey()` construction path must be replaced by a
controlled typed factory. Every typed constructor validates all parent
identities and nonempty canonical witnesses and returns
`Expected<Identity, IdentityError>`; invalid input cannot be laundered into a
nonempty valid key.

The first slice has no general region graph. A private factory may create only
the call region and full-expression region witnesses required by one admitted
observation. Their identity is derived from the real call/formal/actual facts;
kind, depth, and arbitrary `outlives` declarations are not public inputs. Any
borrow escape, result dependency, region transfer, or lifetime extension is
`NotInSlice`.

## Immutable domain deltas

Delta is not another spelling of journal. No delta exposes public setters,
append operations, generic payload variants, or arbitrary action composition.

### EvaluationDelta

`EvaluationDelta` describes only facts/effects produced by the single legacy
evaluation of the selected actual expression and recovered from its resolved
cache plus the pre-legacy observation:

- real value-producing expression effects;
- temporary facts created by that expression.

It contains no outer-formal move, borrow, invalidation, or liability decision.

### BoundaryDelta and FinalizationDelta

`BoundaryDelta` and `FinalizationDelta` are derived exactly once from the full
validated edge set by private factories:

```text
deriveBoundaryDelta(TransferEdges, PreLegacyDirectCallFacts)
deriveFinalizationDelta(TransferEdges, BoundaryDelta, MinimalCallRegionWitness)
```

They cannot be independently constructed or edited.

They are immutable aggregates of real state-domain patches, such as:

```text
PlaceStateDelta
PALDelta
DependencyDelta
CleanupLiabilityDelta
RegionObligationDelta
```

Every domain patch retains its originating `TransferEdgeId`; cross-domain
validation proves that source, destination, exact-place admission, loan,
cleanup, dependency, and liability facts all describe the same edge.

`SemanticModelPatch` exists only once as the top-level `ValidatedCall` patch.
Domain deltas may reference its strongly typed entries but cannot own a second
patch.

The first slice creates no general `RegionFacts` graph. Its private minimal
region-witness factory is the sole authority for the non-escaping call and
full-expression regions in scope. A caller cannot manufacture region
kind/depth or an outlives claim beside an opaque ID.

## Pure direct-call observation

The first slice has no child transaction, adopt, discard, rollback, snapshot
swap, or independently implemented lifecycle API. It observes the existing
legacy call path without rechecking the expression.

The sequence is fixed:

```text
1. capture PreLegacyDirectCallFacts read-only before argument checking
2. run the existing legacy argument check exactly once
3. capture LegacyCallOutcome and resolved AST/type caches
4. capture AfterLegacyObservationBaseline
5. run pure DirectCallObservationFactory and compare state afterward
```

`PreLegacyDirectCallFacts` contains the selected declaration/formal, source
syntax/value-category seed, exact source place if already available, and
read-only PAL/place/dependency/capability facts needed by the admitted matrix.

The legacy checker remains solely responsible for contextual typing, AST
resolution, existing diagnostics, and current RC8 ownership effects. The pure
factory consumes pre-legacy facts plus cached post-check results; it never calls
`checkExpr`, performs a second AST walk, or attempts to replay the legacy
effects.

`DirectCallObservationFactory` returns the three-state slice result and the
mandatory internal receipt. It has no reference granting mutation of Sema,
scope, PAL, AST, diagnostics, Evidence, cleanup, or compiler caches.

## Central slice admission

One pure, centralized predicate returns:

```text
SliceAdmissionResult
    Admitted(ValidatedCall)
    NotInSlice(D3ExclusionReason)
    Rejected(CallValidationError)
```

No route-local caller may bypass or widen this predicate.

`Admitted` requires all of the following:

- source-backed, non-overloaded ordinary direct `FunctionDecl`;
- non-generic, non-variadic, synchronous function with no default argument,
  init/outcome contract, execution-boundary behavior, or return dependency;
- exactly one formal and one already legacy-evaluated actual;
- no nested D.3 observation within that actual;
- either a whole local place or a whole temporary (no projection);
- concrete owned non-Copy, proven Copy, or borrowed aggregate classification;
  and
- complete whole-place admission/liability facts required by the matrix.

`D3ExclusionReason` is exhaustive and includes at least:

```text
WrongRoute
Overloaded
Generic
VariadicOrDefault
MultipleArguments
InitOrOutcome
AsyncOrExecutionBoundary
ReturnDependencyOrRegionEscape
NestedObservation
Projection
SourceHidden
SharedIdentity
RawOrReferenceIdentity
FunctionOrDynIdentity
UnsupportedTypeCategory
```

Excluded calls emit `NotInSlice(reason)` and no prospective edge/delta. They
continue only through the legacy path.

`Rejected` is reserved for calls that otherwise meet the slice shape but
violate its semantic contract, including borrowed formal plus explicit `cede`
(`E04640`), indeterminate Copy/ownership proof, or invalid/missing whole-place
admission. Rejection is a prospective D.3 result only; existing legacy
diagnostics/outcome remain authoritative in this Shadow slice.

## First matrix

It must plan these actual/formal matrices using real Sema facts:

| Selected formal and actual | Transfer | Source disposition |
| --- | --- | --- |
| borrowed aggregate formal + place | `BorrowCapture` | `KeepLive` |
| borrowed formal + explicit `cede` | reject (`E04640`) | no delta |
| `cede` + exact proven non-Copy whole place, bare/explicit | `MoveOwned` | `InvalidateWhole` |
| `cede` + proven Copy place, bare | `CopyValue` | `KeepLive` |
| `cede` + proven Copy whole place, explicit | `CopyValue` | `InvalidateWhole` |
| `cede` + admitted whole temporary, bare/explicit | `ConsumeTemporary` | `NoSourcePlace` |
| indeterminate or ineligible whole-place admission | reject | no delta |

The current RC8 caller spelling rule and `E04570` remain active. Bare owning
move planning is Shadow evidence only and cannot invalidate the caller.

## Placement in the real call path

Pre-legacy capture runs after ordinary direct-call resolution selects the
concrete declaration/formal but before the existing argument checker mutates
call-boundary ownership state. The legacy checker then runs exactly once in its
current position.

The pure D.3 factory runs only after legacy checking has populated the actual's
resolved type/cache and legacy outcome. Immediately before and after that pure
factory, the implementation compares the complete observation-state inventory
defined below.

Nested actual calls, source-hidden declarations, and other excluded forms are
classified `NotInSlice` without recursively invoking D.3.

## Mandatory D.3 observation receipt

The first implementation must ship a separately versioned internal protocol:

```text
toka.internal.m1b-d3-direct-call-observation / version 1
```

The implementation command is frozen as:

```text
tokac --m1b-d3-direct-call-observation=json --check-only source.tk
```

It is mandatory qualification output, not an optional extension of M1a v3 and
not public Evidence. Its dedicated internal audit mode is check-only, mutually
exclusive with every other JSON/evaluation output mode, and emits one
deterministically ordered record for every considered call site.

The factory returns each immutable receipt record by value to an audit-driver
local buffer. It does not write a Sema field, global/static recorder,
`SemanticEvidence`, AST node, or semantic model. The returned record is the only
intentional output excluded from the parent-state equality comparison.

Each record contains at least:

```text
call site/source location
resolved route/callee/formal identity
admission = Admitted | NotInSlice | Rejected
admission/exclusion/rejection reason
legacy outcome             success/rejected + diagnostic codes + spelling
prospective D.3 outcome    transfer/rejection, never committed
complete ValidatedTransferEdge fields when admitted
EvaluationDelta records
BoundaryDelta records
FinalizationDelta records
top-level SemanticModelPatch entries
minimal call/full-expression region witness
observation-state comparison result
```

Every delta record carries:

```text
originating TransferEdgeId
state domain
typed subject identity
expected-before fact
result-after fact
provenance/admission reason
```

`NotInSlice` records contain the exact `D3ExclusionReason` and no edge/delta.
`Rejected` records contain `CallValidationError`, expected contract location,
and no adopted state. The receipt always records both legacy and prospective
outcomes so disagreement is visible rather than silently normalized.

The minimal region witness is factory-produced from real call facts and records
only call/full-expression region identity, origin, loan/cleanup subject, and
required terminal. It exposes no public region kind/depth/outlives constructor.

## Observation-state equality

The purity gate compares state captured immediately after legacy checking but
before the D.3 factory with state immediately after receipt construction. It is
not a before/after comparison of the legacy checker itself.

The comparison is structural and covers this explicit inventory:

- call/actual AST node identity, argument vector identity/order/count,
  `ResolvedFn`/extern/shape selection, resolved types, callable receiver,
  aggregate transfers, init flags, and all Shadow vectors;
- every visible scope/symbol identity, type, permission/capability, init mask,
  exact-place fact, moved/use/mutated flags, move origin, borrowed path, and
  lifetime/member dependency set;
- complete PAL state, conflicts, loans/borrows, payload ceilings, and
  path-restriction facts;
- place/cleanup/drop-liability and temporary/lifetime facts touched by call
  checking;
- current function/effect/expected-type/cede/permission/context flags and
  last dependency/field-dependency results;
- diagnostics, notes, warnings/dedup state, public Evidence v1 records,
  conditional/todo/capability/Shadow evidence buffers, and error status;
- function/shape/generic/trait/lookup/reachability caches and instantiated
  declaration collections; and
- committed semantic-model/index contents and source-origin identities.

Each family reports structural equality and any differing field. A combined
digest may be emitted as a convenience, but digest equality never substitutes
for this inventory or grants semantic authority.

## Exclusions

The first slice excludes:

- static/method/callable/indirect/dynamic-trait/extern routes;
- overloaded or source-hidden ordinary calls;
- generic deduction or instantiation;
- default, variadic, synthetic, and `init` arguments;
- outcome-governed calls;
- nested D.3 observation;
- shared/raw/reference/function/dyn identity categories;
- every projected source place;
- async, `.start`, thread handoff, or execution-boundary escape;
- borrowed/dependency-bearing result escape, region transfer, or temporary
  lifetime extension;
- branch/loop join;
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
3. invalid/incomplete identity/fact input returns `Expected` error and cannot
   produce a valid edge, delta, region witness, or receipt authority;
4. the mandatory version-1 D.3 protocol records every
   Admitted/NotInSlice/Rejected path with legacy/prospective outcomes, complete
   edge/delta provenance, expected-before/result-after, and exclusion reason;
5. zero/one/repeated observations pass every field of the explicit
   after-legacy state inventory and preserve normal diagnostics, Evidence v1,
   AST, identities, PAL/place/cleanup state, caches, and compiler output;
6. implicit/explicit non-Copy forms have equal transfer/liability plans while
   Copy forms preserve their required source-disposition difference;
7. temporary cleanup and borrowed call-region loan witnesses are complete for
   the slice, terminate locally, and are not reconstructed by CodeGen;
8. source-hidden, projected, nested, shared/raw/reference/function/dyn, and all
   other excluded forms deterministically return their exact NotInSlice reason;
9. M1a Shadow, Evidence v1, public JSON CLI, CTest, conformance, pass, and fail
   gates remain green; and
10. no excluded route silently falls back to D.3 authority.

## Admission decision

This document admits no implementation until independently reviewed. After
review it may authorize only the ordinary direct-call Shadow slice above. It
does not authorize M1b.1, semantic adopt/commit, CodeGen consumption, Evidence
v2, branch join, or caller-spelling activation.
