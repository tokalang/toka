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
`ValidatedCallFactory::create()` is used only by the admitted branch and returns
`Expected<ValidatedCall, CallValidationError>` after validating the whole
direct call. Invalid parents, types, places, regions, destinations, liability
relations, or incomplete facts return an error; no factory converts them into
a nonempty valid identity/result. The outer `DirectCallObservationFactory`
owns the total three-state admission decision.

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

Copy classification is captured only through:

```text
lookupCopyProof(Type) const
    -> ProvenCopy | ProvenNonCopy | Indeterminate
```

The lookup reads an already established proof for the concrete type and never
inserts or updates `Slice4CopyProofs`, `Slice4CopyProofInProgress`,
`CopyProofMap`, or another cache. Cache miss is `Indeterminate` and therefore a
fail-closed D.3 rejection. The observation path must not call
`proveSlice4CopyType()` or any proof-producing API.

## Immutable domain deltas

Delta is not another spelling of journal. No delta exposes public setters,
append operations, generic payload variants, or arbitrary action composition.

Every immutable delta contains private factory-created entries of this exact
shape:

```text
DeltaEntry
    TransferEdgeId
    StateDomain
    typed subject identity
    expected-before fact
    result-after fact
    provenance / admission reason
```

These fields belong to the delta entry itself. The receipt serializes them
directly and may not reconstruct provenance or before/after state from other
objects.

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

### Pure factory translation unit

The observation DTOs, `DirectCallObservationFactory`, and admitted-path
`ValidatedCallFactory` live in an independent pure translation unit. That
unit:

- accepts immutable value DTOs only;
- does not include or hold `Sema`, AST declarations/expressions,
  `DiagnosticEngine`, `SemanticEvidence`, PAL checker objects, or mutable
  compiler caches;
- contains no global/static recorder, memoization, environment/CLI access, or
  callback into semantic analysis; and
- returns one immutable `FactoryObservationRecord`, containing the total
  `SliceAdmissionResult` and its receipt fields, by value.

Integration code alone translates real compiler facts into DTOs. Build gates
lock the pure TU's include/dependency boundary.

## Pure direct-call observation

The first slice has no child transaction, adopt, discard, rollback, snapshot
swap, or independently implemented lifecycle API. It observes the existing
legacy call path without rechecking the expression.

The integration point is restricted to the final ordinary semantic traversal
of a same-lexical direct function call. The selected declaration must come from
lexical lookup without `functionAcceptsCall()`, overload/candidate acceptance,
generic deduction, closure capture precompute, or any other speculative probe.
If the implementation cannot prove these conditions before the first argument
check for that AST occurrence, D.3 is not invoked.

The sequence is fixed:

```text
1. capture PreCaptureSentinel A0
2. capture PreLegacyDirectCallFacts read-only
3. capture A1 and require A0 == A1 field-by-field
4. run the existing legacy argument check exactly once
5. capture PostObservationSentinel B0
6. capture LegacyCallOutcome/resolved caches with const-only Copy lookup
7. run pure DirectCallObservationFactory and obtain record by value
8. capture B1 and require B0 == B1 field-by-field
9. audit driver adds comparison envelope and buffers it locally
```

`PreLegacyDirectCallFacts` contains the selected declaration/formal, source
syntax/value-category seed, exact source place if already available, and
read-only PAL/place/dependency/capability facts needed by the admitted matrix.

The legacy checker remains solely responsible for contextual typing, AST
resolution, existing diagnostics, and current RC8 ownership effects. The pure
factory consumes pre-legacy facts plus cached post-check results; it never calls
`checkExpr`, performs a second AST walk, or attempts to replay the legacy
effects.

`DirectCallObservationFactory` returns a `FactoryObservationRecord` containing
the three-state slice result and mandatory internal receipt fields. It has no
reference granting mutation of Sema, scope, PAL, AST, diagnostics, Evidence,
cleanup, or compiler caches.

## Central slice admission

Before the factory, one integration gate defines the considered set:

```text
D3ConsideredCallGateResult
    Considered
    Excluded(D3GateExclusionReason)

D3GateExclusionReason
    WrongRoute
    NonSameLexical
    CandidateProbeOrSpeculativeContext
    NonFinalSemanticTraversal
    NestedObservationContext
```

Calls rejected by this integration gate produce no D.3 record. Qualification
asserts `factory_invocation_count == 0` for each gate reason.

The gate result and its exclusion reason are closed enums. In particular, an
outer considered call whose actual syntax contains a nested call may later
produce `NotInSlice(NestedObservation)`, while the inner traversal is rejected
by `NestedObservationContext` and never invokes the factory.

One pure, centralized predicate returns:

```text
SliceAdmissionResult
    Admitted(ValidatedCall)
    NotInSlice(D3ExclusionReason)
    Rejected(CallValidationError)
```

No route-local caller may bypass or widen this predicate. The sum is total for
every immutable DTO accepted by the gate; there is no optional, fallthrough,
or unclassified result.

`Admitted` requires all of the following:

- a call already accepted by `D3ConsideredCallGate`;
- non-generic, non-variadic, synchronous function with no default argument,
  init/outcome contract, execution-boundary behavior, or return dependency;
- exactly one formal and one already legacy-evaluated actual;
- no nested D.3 observation within that actual;
- exactly one of these shapes:
  - `cede` formal plus supported whole local place or whole temporary; or
  - non-`cede` aggregate formal plus bare whole local place;
- concrete owned non-Copy, proven Copy, or borrowed-aggregate classification;
  and
- complete whole-place admission/liability facts required by the matrix.

`D3ExclusionReason` is a closed enum:

```text
Generic
VariadicOrDefault
MultipleArguments
InitOrOutcome
AsyncOrExecutionBoundary
ReturnDependencyOrRegionEscape
NestedObservation
Projection
SharedIdentity
RawOrReferenceIdentity
FunctionOrDynIdentity
NonCedeScalar
NonCedeAggregateTemporary
UnsupportedTypeCategory
```

Excluded calls emit `NotInSlice(reason)` and no prospective edge/delta. They
continue only through the legacy path.

`CallValidationError` is also a closed enum:

```text
LegacyTypeMismatch
BorrowedFormalExplicitCede       E04640
IndeterminateCopyProof
IndeterminateOwnership
InvalidWholePlaceAdmission
IncompleteObservationFacts
```

Admission precedence is fixed:

1. incomplete post-check facts or legacy type mismatch return `Rejected`;
2. closed exclusion categories return `NotInSlice`;
3. non-`cede` aggregate + explicit `cede` returns
   `Rejected(BorrowedFormalExplicitCede)`;
4. supported non-`cede` aggregate place or `cede` place/temporary proceeds to
   proof/admission validation; and
5. indeterminate proof or invalid whole-place admission returns `Rejected`.

Every enumerator in the gate, exclusion, and error enums requires a fixture and
an exhaustive switch without `default`. Rejection is prospective only;
existing legacy diagnostics/outcome remain authoritative in this Shadow slice.

## First matrix

It must plan these actual/formal matrices using real Sema facts:

| Selected formal and actual | Transfer | Source disposition |
| --- | --- | --- |
| borrowed aggregate formal + place | `BorrowCapture` | `KeepLive` |
| borrowed formal + explicit `cede` | reject (`E04640`) | no delta |
| non-`cede` scalar place | `NotInSlice(NonCedeScalar)` | no delta |
| non-`cede` aggregate temporary | `NotInSlice(NonCedeAggregateTemporary)` | no delta |
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
or resolves any actual expression. This is the exact same-lexical common-path
program point immediately before its first argument-check call. A traversal
that has already checked, contextually typed, or probed any actual is rejected
by `D3ConsideredCallGate`. The legacy checker then runs exactly once in its
current position.

The pure D.3 factory runs only after legacy checking has populated the actual's
resolved type/cache and legacy outcome. Immediately before and after that pure
post-check extraction and factory call, the implementation compares the narrow
`PostObservationSentinel` defined below.

An outer considered call with nested actual syntax is
`NotInSlice(NestedObservation)` without recursively invoking D.3. Source-hidden
declarations and routes outside the considered set produce no D.3 record.

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
exclusive with every other JSON/evaluation output mode.

The considered set is exactly the final same-lexical ordinary-direct common-
path calls accepted by `D3ConsideredCallGate`. Static/method/callable/indirect/
dynamic-trait/extern, cross-module/source-hidden, candidate probe, closure
precompute, and other speculative traversals produce no D.3 record and must
prove `factory_invocation_count == 0` in their fixtures.

The factory returns each immutable `FactoryObservationRecord` by value. It does
not write a Sema field, global/static recorder, `SemanticEvidence`, AST node,
or semantic model. After the factory returns, the audit driver performs
sentinel comparison and creates the outer envelope:

```text
ObservationEnvelope
    factory record
    comparison
        pre_fact_capture_unchanged
        post_cache_and_factory_unchanged
        differing sentinel fields[]
```

The driver stores envelopes only in a command-local output buffer. Factory
records do not contain or predict comparison results.

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

Only `Admitted` records carry edge, delta, model-patch, or minimal-region
authority. Those collections are empty for `NotInSlice` and `Rejected`.

The top-level JSON document records schema/version, considered-call count,
factory-invocation count, and deterministically ordered envelopes. It is the
only stdout output; LLVM IR or a second JSON document is forbidden. The command
requires `considered_call_count == factory_invocation_count == envelopes.count`.
For a compilation containing only gate-excluded calls, all three are zero. The
exit code and stderr diagnostics match the same legacy `--check-only`
compilation without D.3 observation, including legacy failures. Output-mode
conflicts reject before installing the output guard, exit nonzero with one
usage diagnostic on stderr, and emit nothing on stdout. Sentinel comparison
failure is reported in the JSON envelope and fails the qualification consumer;
it does not replace or alter the legacy compilation exit status.

The minimal region witness is factory-produced from real call facts and records
only call/full-expression region identity, origin, loan/cleanup subject, and
required terminal. It exposes no public region kind/depth/outlives constructor.

## Observation-state equality

Purity is checked by two narrow, slice-specific sentinels. They are local audit
DTOs, not semantic-model records, a general Sema snapshot API, or a rollback
mechanism.

`PreCaptureSentinel` is captured as A0 and A1 around only
`PreLegacyDirectCallFacts` extraction. It contains:

- structural fingerprints of the relevant call and actual AST nodes, including
  the argument vector and resolved fields already present at A0;
- the selected formal plus source symbol, whole-place, permission, init/move,
  cleanup/liability, and dependency facts read by the extraction;
- the PAL facts for that source root/whole place;
- diagnostic/error, Evidence v1, and Shadow-buffer structural counters and
  entries visible to this call; and
- entry-count plus referenced-type entries for the Copy-proof and
  dependency/capability caches consulted by the extraction.

`PostObservationSentinel` is captured as B0 and B1 around only cached
post-check fact extraction, const-only Copy lookup, and the pure factory. It
contains the same relevant source/PAL/diagnostic/Evidence/cache fields plus the
call/actual resolved type and selected-declaration fields produced by the one
legacy check. It deliberately excludes the audit driver's command-local output
buffer, which is written only after B1 comparison.

Both comparisons are structural and field-by-field. A failure names every
differing sentinel field in the outer envelope. A digest may be printed only
as a debugging convenience and never substitutes for field comparison. The
sentinel capture helpers themselves are const/read-only and cannot populate a
cache.

This internal check is intentionally narrow. Full-process preservation is
qualified separately with zero-, one-, and repeated-observation executions:
public CLI behavior with the D.3 flag absent remains byte-for-byte unchanged,
and internal audit executions preserve the underlying legacy diagnostics,
Evidence v1, and exit behavior apart from their dedicated receipt stdout.

## Exclusions

The following routes are outside `D3ConsideredCallGate` and produce no record:

- static/method/callable/indirect/dynamic-trait/extern routes;
- cross-module/source-hidden ordinary calls;
- overload/candidate probes, generic-deduction probes, closure precompute, and
  other speculative or non-final traversals.

Within a considered call, the first slice returns a closed `NotInSlice` reason
for:

- a generic declaration/instance that reaches the gate without a probe;
- default, variadic, synthetic, and `init` arguments;
- multiple arguments;
- outcome-governed calls;
- nested D.3 observation;
- shared/raw/reference/function/dyn identity categories;
- non-`cede` scalar places and non-`cede` aggregate temporaries;
- unsupported type categories;
- every projected source place;
- async, `.start`, thread handoff, or execution-boundary escape;
- borrowed/dependency-bearing result escape, region transfer, or temporary
  lifetime extension.

The entire D.3 slice also excludes, without defining an admission result for:

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
4. every closed gate/exclusion/error enumerator has a fixture and an exhaustive
   switch with no `default`; gate exclusions prove factory invocation count
   zero, while the mandatory version-1 protocol records every considered
   Admitted/NotInSlice/Rejected path with legacy/prospective outcomes, complete
   edge/delta provenance, expected-before/result-after, and exact reason;
5. A0/A1 and B0/B1 pass every structural field of their narrow sentinels, and
   zero/one/repeated-observation CLI parity preserves normal diagnostics,
   Evidence v1, output, and exit behavior;
6. implicit/explicit non-Copy forms have equal transfer/liability plans while
   Copy forms preserve their required source-disposition difference;
7. temporary cleanup and borrowed call-region loan witnesses are complete for
   the slice, terminate locally, and are not reconstructed by CodeGen;
8. projected, nested, shared/raw/reference/function/dyn, and all other
   considered-but-excluded forms return their exact `NotInSlice` reason;
9. M1a Shadow, Evidence v1, public JSON CLI, CTest, conformance, pass, and fail
   gates remain green; and
10. source-hidden/cross-module, wrong-route, candidate/speculative, non-final,
    and nested-inner traversals produce no record and invoke the factory zero
    times;
11. Copy proof access is const-only and a miss rejects fail-closed without
    changing any proof cache; and
12. the pure factory translation unit passes dependency/include gates and has
    no Sema, AST, diagnostic, Evidence, PAL-object, mutable-cache, callback, or
    global/static state access.

## Admission decision

This document admits no implementation until independently reviewed. After
review it may authorize only the ordinary direct-call Shadow slice above. It
does not authorize M1b.1, semantic adopt/commit, CodeGen consumption, Evidence
v2, branch join, or caller-spelling activation.
