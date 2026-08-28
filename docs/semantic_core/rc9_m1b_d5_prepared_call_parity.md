# RC9 M1b-D.5a Pre-Legacy Prepared-Call Parity

**Design status:** Proposed for one bounded M1b.1a implementation review.

**Implementation status:** Not implemented. This document authorizes neither
live ownership commit nor caller-spelling activation.

**Qualified inputs:** D.3a post-legacy direct-call observation at `0235a35c`,
D.4a pure direct-nominal overload query at `5197e2e4`, and EXP-LIN-02 at
`60a7b935`.

## Decision

M1b.1a moves one already-qualified call plan across the legacy-check boundary:
for a narrow ordinary direct call whose actual is an already-resolved whole
function-local place, Sema captures all stable planning facts before legacy
argument checking and invokes a pure `PreparedCallFactory`.

The existing legacy argument check still runs exactly once and remains the only
producer of diagnostics, Evidence v1, PAL/place mutations, and CodeGen input.
After that check, the existing D.3a observation is finalized and its complete
validated call is compared structurally with the pre-legacy prepared call.

M1b.1a introduces no general transaction object, AST clone, Scope/PAL snapshot,
journal, adopt/discard lifecycle, semantic-model publication, CodeGen consumer,
Evidence v2 producer, or behavior switch.

## One planning authority

D.5a must not create a second transfer classifier. The pure translation unit
splits the existing D.3a factory into two internal stages:

```text
prepareCall(PreLegacyDirectCallFacts, ResolvedPlanningFacts)
    -> Expected<PreparedCallResult, PreparationError>

finalizeLegacyObservation(PreparedCallResult, LegacyCallOutcome)
    -> D3FactoryObservationRecord
```

`DirectCallObservationFactory::observe()` becomes an adapter over these same
stages for existing D.3a callers. Transfer mode, source disposition, boundary
access, dependency, cleanup liability, region witness, and model patch are
computed only by `prepareCall()`. In qualification mode, pre- and post-legacy
resolved type/proof facts each invoke that same pure core once while sharing
the immutable pre-legacy boundary snapshot. The post invocation is the D.3a
parity oracle, not a second classifier.
Finalization may accept, exclude, or reject the post prepared result based on
legacy outcome, but may not rebuild or modify its plan.

The immutable result is:

```text
PreparedCallResult
    Admission = Prepared | NotInSlice | Rejected
    closed reason?
    input facts
    PreparedCall?

PreparedCall
    CallSiteId
    ResolvedCalleeId
    TransferEdges[]
    BoundaryDelta
    FinalizationDelta
    SemanticModelPatch
    MinimalRegionWitness
```

Whole-place variable evaluation has no value-producing side effect in this
slice, so the prepared result contains no invented `EvaluationDelta`. The
post-legacy D.3a `EvaluationDelta` must be empty for parity admission. A later
slice that admits temporaries or contextual expressions must model their real
evaluation separately.

`PreparedCall` remains Shadow data. Its presence grants no permission to
invalidate a place, suppress cleanup, emit Evidence, or lower CodeGen.

## Closed admission

Every D.5a condition is required:

- final ordinary same-lexical direct function call;
- exactly one selected source-backed declaration and no overload traversal;
- exactly one explicit formal and one actual;
- no candidate, generic-deduction, closure-precompute, nested-observation, or
  other speculative traversal;
- actual is a bare or explicit-`cede` whole function-local place;
- source place is `Live`, non-alias, non-projected, and PAL-free;
- actual type is already resolved before the legacy argument loop;
- pre-legacy actual type and selected formal type have a direct compatible
  identity with no conversion or contextual typing;
- Copy and ownership proofs are already present in read-only qualified stores;
- every admitted `cede + ProvenNonCopy` row also has
  `OwnershipProof=Owned`; `ProvenNonCopy + Trivial` is outside D.5a;
- no proof-producing query or cache insertion is required;
- no shared/raw/reference/function/dyn identity, dependency-bearing view,
  return dependency, region escape, outcome, `init`, default, variadic,
  async, `.start`, thread handoff, or other execution boundary; and
- no diagnostic exists for this call before the legacy argument loop.

The admitted matrix is deliberately small:

| Selected formal and whole local actual | Prepared transfer | Prepared source |
| --- | --- | --- |
| non-`cede` plain aggregate, bare | `BorrowCapture` | `KeepLive` |
| `cede` proven non-Copy, proven Owned aggregate, bare | `MoveOwned` | `InvalidateWhole` |
| `cede` proven non-Copy, proven Owned aggregate, explicit | `MoveOwned` | `InvalidateWhole` |
| `cede` proven Copy, proven Trivial aggregate, bare | `CopyValue` | `KeepLive` |
| `cede` proven Copy, proven Trivial aggregate, explicit | `CopyValue` | `InvalidateWhole` |

The admitted bare non-Copy/Owned row still receives legacy `E04570`. D.5a
records that its prospective authority is otherwise equivalent to the explicit
row; it does not suppress the diagnostic or invalidate the source.

`ProvenNonCopy + Trivial` does not imply the legacy caller policy requires
explicit `cede`: a managed no-drop aggregate may still be exempt under RC8.
D.5a therefore returns `NotInSlice(CededNonCopyTrivial)` before preparation for
that combination. The insertion point must not call
`canImplicitlyPassToCede()` or another legacy helper that can resolve, cache,
or mutate facts.

## Stable pre-legacy facts

Integration may read only facts already available at the insertion point:

```text
PreLegacyDirectCallFacts
    structural call/callee/formal/source identities
    formal cede contract and source spelling
    source PlaceId, exact whole-place admission, PlaceState and init mask
    current PAL state and dependency set

ResolvedPlanningFacts
    actual and formal canonical type identities
    TypeCategory
    CopyProof = ProvenCopy | ProvenNonCopy
    OwnershipProof = Trivial | Owned
    exact whole-place authority and raw PAL state
    raw dependency facts
    SourceCleanupAuthority = None | ExistingSourcePlace(PlaceId, init mask)
    structural destination anchor
    call/full-expression region anchors

LegacyCallOutcome
    observed actual/formal canonical type identities and proofs
    observed PAL operation / boundary access
    source PlaceState and PAL state after legacy
    observed cleanup/drop-mask state after legacy
    diagnostic codes and final-check count
```

`BoundaryAccess`, liability transfer/retention, region witnesses, deltas, and
SemanticModelPatch are outputs of the shared pure core. They are never supplied
as asserted pre-planned facts. `SourceCleanupAuthority` contains only the
existing source PlaceId and init mask; the factory derives the cleanup subject
identity and every liability transition. Likewise region anchors identify real
lexical/full-expression origins but cannot assert a region kind or outlives
relation.

The queries are the already-qualified const D.3a queries. A cold/missing Copy,
ownership, type, cleanup, identity, or region fact returns a closed preparation
error. It must not call `proveSlice4CopyType()`, resolve an unknown type, create
a destructor, populate a cache, check an expression, or use the destination
formal to fill a missing actual fact.

Pre-legacy and post-legacy actual/formal canonical identities and read-only
proofs must be equal. Any elaboration, coercion, contextual type change, AST
replacement, argument insertion, or source-place identity change produces
`Rejected(PrePostFactMismatch)` with no parity-accepted plan. The post oracle
deliberately retains the immutable pre-legacy PlaceState/PAL snapshot: explicit
legacy `cede` is allowed to perform its existing source invalidation and is
checked as the observed outcome, not mistaken for preparation impurity.

## Integration sequence

The real call path is fixed:

```text
1. prove the closed D.5a route/admission gate
2. capture structural sentinel A0
3. capture pre-legacy and resolved planning facts read-only
4. run PreparedCallFactory once
5. capture A1 and require A0 == A1 field-by-field
6. run the unchanged legacy argument check exactly once
7. capture structural sentinel B0
8. recapture post-legacy resolved type/proof and LegacyCallOutcome facts, run
   the same pure core once with the immutable pre-legacy boundary facts,
   finalize D.3a, perform strict same-call parity, and build the receipt by value
9. capture B1 and require B0 == B1 field-by-field
10. append the completed receipt to command-local qualification state
```

There is no fallback from preparation failure to a partial prepared plan.
Because D.5a is Shadow-only, `NotInSlice` and `Rejected` preparation results do
not change normal diagnostics or control flow; the legacy path still runs once.
An internal infrastructure failure terminates only the dedicated qualification
mode and follows the already-qualified D.4a exact-one-diagnostic policy.

## Same-call parity and cross-fixture equivalence

For one AST call, the pre-legacy prepared plan and post-legacy oracle use the
same identities and spelling. They require strict full typed equality, never
hashes or display strings. Same-call parity compares:

- call, callee, formal, argument-plan, edge, source-place, destination, region,
  cleanup, and patch identities;
- transfer mode, source disposition, spelling, Copy/ownership proof;
- PAL/dependency/boundary facts and cleanup liability;
- every Boundary and Finalization delta entry; and
- the complete SemanticModelPatch.

The post-legacy record may additionally carry the legacy outcome and its empty
whole-place EvaluationDelta. Those wrapper facts are not part of the prepared
plan and cannot alter it.

Bare/explicit fixtures are different calls and must not use this strict
equality. Cross-fixture comparison first maps concrete identities to structural
roles:

```text
Call, Callee, Formal0, ArgumentPlan0, Edge0, Source0, Destination0,
SourceCleanup0, DestinationCleanup0, CallRegion, FullExpressionRegion
```

It then compares a `TransferAuthorityProjection` with a closed
allowed-difference relation:

- non-Copy/Owned bare versus explicit: after role normalization, only
  `spelling` may differ;
- Copy/Trivial bare versus explicit: `spelling`, source disposition
  (`KeepLive`/`InvalidateWhole`), boundary access
  (`SharedBorrow`/`Invalidation`), and exactly the source-related
  PlaceState/PAL delta and patch payloads derived from that disposition may
  differ;
- transfer mode, Copy/ownership proof, destination, dependency, cleanup
  liability, region recipe, and every non-source delta/patch fact must remain
  equal in both comparisons.

The allowed-difference matcher is typed and exhaustive. It cannot erase fields,
compare display strings, or accept an unclassified difference.

## Closed result and receipt

The gate and factory reasons are frozen:

```text
D5GateExclusionReason
    WrongRoute
    NonSameLexical
    OverloadOrCandidateProbe
    SpeculativeOrNonFinalTraversal
    NestedPreparation
    ExistingCallDiagnostic

D5PreparationExclusionReason
    ArityOrDefault
    GenericOrContextual
    InitOrOutcome
    AsyncOrExecutionBoundary
    ReturnDependencyOrRegionEscape
    ProjectionOrTemporary
    NonLocalPlace
    SharedRawReferenceOrCallable
    DependencyBearingActual
    TypeRequiresContextOrConversion
    CededNonCopyTrivial

D5PreparationError
    InvalidIdentity
    IncompatibleType
    IndeterminateCopyProof
    IndeterminateOwnership
    InvalidWholePlaceAdmission
    IncompleteLiability
    IncompleteRegion
    ConflictingPreparedPlan

D5ParityError
    PrePostFactMismatch
    PreparedPlanMismatch
    LegacyOutcomeMismatch
    LegacyCheckCountMismatch
    NonEmptyEvaluationDelta

D5InfrastructureError
    InvalidCallSiteIdentity
    InvalidCalleeIdentity
    InvalidFormalOrDestinationIdentity
    InvalidSourcePlaceIdentity
    ConflictingPatchPayload
    MalformedPreparedResult
```

Gate exclusion invokes the pure factory zero times. A preparation `NotInSlice`
or `Rejected` result has no plan. A parity rejection may retain the two Shadow
plans only for structural diff reporting, but exposes no parity-accepted plan
or semantic authority. Only the dedicated qualification mode may emit one
terminal internal diagnostic for an injected infrastructure failure.

The internal protocol is separately versioned:

```text
schema = toka.internal.m1b-d5a-prepared-call-parity
version = 1
considered_call_count
pre_factory_invocation_count
post_oracle_invocation_count
prepared_count
excluded_count_by_reason
rejected_count_by_reason
parity_failure_count_by_reason
infrastructure_error_count_by_reason
receipts[]
    call/formal/source locations and typed identities
    pre-legacy planning facts
    prepared admission/reason
    prepared plan?
    legacy diagnostic codes
    final legacy check count
    post-legacy D.3a admission
    same_call_structural_parity
    differing_plan_fields[]
    authority_projection
    cross_fixture_allowed_differences[]
    pre_factory_parent_unchanged
    post_factory_parent_unchanged
    pre_differing_parent_fields[]
    post_differing_parent_fields[]
```

Every reason has an exhaustive switch and a real fixture. Implementation may
not add a generic `Unsupported` escape hatch or silently remap a D.3a reason.

## Qualification

Implementation acceptance requires:

1. all five admitted rows use real source calls and invoke the prepare factory
   once before legacy and the same pure core once as a post-legacy oracle;
2. each same-call pre/post pair is strictly structurally equal; cross-fixture
   bare/explicit comparisons pass only the frozen role-normalized
   allowed-difference relation;
3. the admitted bare non-Copy/Owned call still emits exactly legacy `E04570`,
   while a real `cede + ProvenNonCopy + Trivial` fixture is
   `NotInSlice(CededNonCopyTrivial)` and preserves its legacy exemption;
4. pre/post call, formal, actual type/proof, source identity, dependency,
   cleanup, region, and plan facts are structurally equal for admitted calls;
   successful legacy calls must match the selected prepared source/boundary
   disposition; the bare non-Copy/Owned spelling-only failure must instead
   preserve the pre-call source state because its prospective plan is not
   committed;
5. A0/A1 proves pre preparation is pure; B0/B1 separately proves post fact
   capture, oracle preparation, parity comparison, and receipt construction
   mutate no AST, Scope, PlaceState, PAL, diagnostics, Evidence, proof cache,
   identity allocator, or semantic-model state; receipt append occurs only
   after B1;
6. legacy evaluation/final checking occurs exactly once and normal output,
   diagnostics, Evidence v1, D.3a receipt, and exit status are byte-identical
   with D.5a disabled;
7. cold/warm cache state and fixture scheduling do not change admission or plan;
8. every closed exclusion/error has a real gate; factory-invalid cases fail
   closed with no plan;
9. equal digest with unequal full identity/plan remains unequal;
10. overload, candidate, generic, closure, nested, temporary, projection,
    source-hidden, cross-module, and execution-boundary paths invoke the D.5a
    factory zero times; and
11. CTest, M1a Shadow, D.3a, D.4a, Evidence v1, public JSON CLI, pass/fail, and
    ownership cleanup gates remain green.

## Bounded review rule

D.5a design or implementation may be blocked only by a reproducible example
inside the closed admission above that shows:

1. required planning facts are unavailable or mutable before legacy checking;
2. preparation changes parent state;
3. prepared and qualified D.3a plans differ;
4. legacy checking no longer runs exactly once or public behavior changes;
5. a closed exclusion/error can enter with a plan; or
6. the shared pure core is not the sole transfer classifier.

Temporaries, projections, multiple arguments, overload expansion, generic or
closure isolation, other call routes, commit, CodeGen, Evidence v2, lint/LSP,
and caller-spelling activation are later slices and cannot reject D.5a.

## Non-authorization

D.5a does not authorize M1b.2 or later, live transaction/adopt/commit,
PlaceState/PAL mutation, cleanup suppression, CodeGen plan consumption,
Evidence v2, `E04570` removal, implicit-call-move lint, source/TKI contract
changes, or signature-driven caller behavior activation.
