# RC9 M1b.0b Synthetic Transaction Contract

**Design status:** M1b.0b-D.2 accepted for the strictly BUILD_TESTING-only
synthetic reference implementation.

**Implementation status:** Synthetic reference implemented and locally
qualified; independent post-implementation acceptance pending. No
Sema/PAL/Evidence/CodeGen/CLI wiring, behavior flag, TKI change, ABI change, or
M1b.1 consumption is admitted.

**Baseline:** `2ac9b726f858da010151cc894c321ce709ae84d0`.

**Parent architecture:**
[`RC9 M1b Transactional Semantic Planning Design`](rc9_m1b_transactional_semantic_planning_design.md).

## Scope

M1b.0b is a synthetic reference implementation of the already frozen D.1
lifecycle. It exists to prove that nonempty state, typed journal actions,
branch joins, rejected output, and immutable publication can satisfy the
transaction contract before any live compiler state is connected.

M1b.0b does not prepare or validate a Toka call. Its state values and side
tables are deterministic fixtures implementing the same ownership classes as
the D.1 `TransactionalStateManifest`.

## Controlled structural identities

### New identity domains

M1b.0b must add strong, non-interchangeable value types for:

```text
TypeId
CallSiteId
SourceOriginId
ConversionId
ArgumentPlanId
TransferEdgeId
DestinationId
TemporaryId
CleanupId
LoanId
RegionId
InitObligationId
OutcomeTransitionId
ValidatedCallId
LoweringRecipeId
SemanticModelPatchId
SemanticRevisionId
BranchSetId
BranchKey
JournalActionId
TransactionId
StructuralForkKey
```

Each identity stores or resolves to its complete canonical structural key.
Hash values are lookup accelerators only and never collision-blind authority.
No domain is implicitly convertible to another even when their structural
components happen to match.

`TypeId` identifies a canonical semantic type including morphology, generic
substitution, callable ownership flow, and source-hidden nominal identity. It
is not a type spelling, mangled layout name, or `Type *` address.

### Destination identity

`DestinationId` is tagged and covers at least:

```text
FormalSlot(CallSiteId, formal index)
ReturnSlot(ResolvedCalleeId)
LocalInitSlot(RootSymbolId)
AggregateFieldSlot(SemanticNodeId, FieldId)
CaptureSlot(SemanticNodeId, capture ordinal)
TemporarySlot(TemporaryId)
```

Source places and destinations remain different domains. A destination cannot
be used as PAL/place invalidation authority without an explicit relation in a
validated journal action.

### Temporary, cleanup, loan, and region identity

- `TemporaryId` derives from the producing semantic node, evaluation role,
  and role-local structural ordinal.
- `CleanupId` derives from its temporary/place owner and cleanup role.
- `LoanId` derives from the originating semantic node/action, source place,
  referent place, and role-local ordinal.
- `RegionId` is tagged as lexical, call-evaluation, temporary-extension, or
  execution-boundary region and derives from its structural owner.
- init and outcome identities derive from the owning destination/declaration,
  transition role, and structural ordinal.

None consumes a global counter or changes when probes are reordered or
discarded.

### StructuralIdentityBuilder

Before any production target includes the M1b identity header,
`SemanticIdentity::fromCanonicalKey()` must cease to be a general public raw
construction entry point. Construction is owned by a pure
`StructuralIdentityBuilder` with typed methods for the domains above and the
M1b.0a identities.

The builder:

- accepts semantic components, not display paths or ad-hoc concatenated text;
- encodes components with collision-safe length/tag boundaries;
- has no counter, registry mutation, cache dependency, or source-order side
  effect beyond explicit structural ordinals;
- returns the same identity with caches cold, warm, or differently populated;
  and
- rejects incomplete, invalid, or mismatched-domain components.

Scaffold tests use an explicit test friend/accessor or typed fixture builder.
They do not keep a production raw-key escape hatch.

## Semantic authority before journal derivation

Journal actions are not independently authored transfer facts. The synthetic
planner first constructs and validates complete edges and obligations. Only
then may it derive a phase-ordered journal.

### ValidatedTransferEdge

Every call argument has an `ArgumentPlanId`; every receiver, return, capture,
init, or temporary handoff has an equivalent tagged `PlanOrigin`. Each origin
owns one or more structurally derived `TransferEdgeId` values. `PlanOrigin` is
tagged as argument, receiver, return, capture, init/outcome, or temporary and
contains only the corresponding strong identities. The authoritative edge is:

```text
ValidatedTransferEdge
    TransferEdgeId
    PlanOrigin
    TransferMode
    TypeId
    SourcePlace?                 exact structured PlaceId
    SourceDisposition
    ExactPlaceAdmission?         whole/projection witness and cleanup mask
    DestinationId
    LiabilitySource
    LiabilityTarget
    DependencyRoots[]
    BoundaryKind
    SharedDisposition
    BoundaryLoanPlan?
    TemporaryCleanupPlan?
```

`TransferMode` covers borrow capture, value/identity copy, owned move, shared
transfer/retain, temporary consumption, init handoff, and pending-outcome
handoff. `SharedDisposition` is explicitly `NotShared`, `MoveIdentity`, or
`RetainIdentity`; it is never inferred from a liability action.

`SourceDisposition` is an independent tagged fact:

```text
KeepLive
InvalidateWhole
InvalidateProjection
NoSourcePlace
```

It is not inferred from the presence or absence of journal actions. In
particular, `CopyValue + KeepLive` and `CopyValue + InvalidateWhole` are
distinct valid edges.

- `KeepLive` requires no invalidation action and forbids one.
- `InvalidateWhole` requires a source place, matching whole-place admission,
  and exactly one whole invalidation action for the edge.
- `InvalidateProjection` requires the exact projected source, matching partial
  admission/cleanup mask, and exactly one projection invalidation action whose
  path and mask equal the edge.
- `NoSourcePlace` requires an absent source/admission and forbids place
  invalidation; it is used for admitted temporaries/source-less transfers.

Source disposition, admission, derived action, cleanup mask, and tagged
liability source/target must all agree on the same edge.

`LiabilitySource` is a tagged sum:

```text
None
PlaceCleanup(PlaceId, CleanupId)
TemporaryCleanup(TemporaryId, CleanupId)
SharedHandle(PlaceId, CleanupId?)
```

`LiabilityTarget` is likewise tagged:

```text
SourceRetained(LiabilitySource)
DestinationAssumed(DestinationId, CleanupId?)
SharedRetained(DestinationId, CleanupId)
CompletedAtFinalization(CleanupId)
NoLiability
```

Untagged "source/cleanup identity" payloads are forbidden.

`BoundaryLoanPlan` contains `LoanId`, source/referent, initial call
`RegionId`, destination, capability, and exactly one Finalization disposition:

```text
EndAtCallFinalization
TransferAtFinalization(new RegionId, DestinationId)
```

The new region must structurally outlive the call region and satisfy dependency
escape/boundary rules. A transferred loan remains live under the same `LoanId`
and its new region owns the later terminal `EndLoan`; the original call cannot
also end it.

`TemporaryCleanupPlan` carries cleanup region from creation:

```text
TemporaryCleanupPlan
    TemporaryId
    CleanupId
    TypeId
    current RegionId
    liability
    RegionExitDisposition

RegionExitDisposition
    Complete
    Disarm(TransferEdgeId, DestinationId)
    TransferRegion(TransferEdgeId, new RegionId, DestinationId)
```

For each region exit exactly one disposition applies. `TransferRegion` keeps
the cleanup live in a strictly longer region; transfer chains are acyclic and
must eventually reach exactly one ultimate `Complete` or `Disarm`. A cleanup
cannot complete, disarm, and extend on competing paths without a validated
branch/outcome join.

All invalidation, cleanup disarm/terminal, liability transfer, dependency
handoff, destination, and shared disposition actions derived for one transfer
must reference the same `TransferEdgeId`. Edge validation rejects a source,
destination, mask, dependency, or liability component borrowed from a
different edge even when every individual identity is otherwise valid.

An edge is immutable after validation. Journal validation checks derivation
from the edge; it does not reconstruct or repair the edge from actions.

### Pending outcome obligation

An outcome-governed init call creates an obligation because the runtime case
is not selected when the call returns:

```text
PendingOutcomeObligation
    InitObligationId
    TransferEdgeId
    OutcomeTransitionId
    DestinationId
    Return/transition declaration identity
    CasePlan[]

CasePlan
    case identity
    destination post-state          initialized | remain-uninit
    liability/cleanup transition
    dependency transition
```

Its lifecycle is:

```text
Pending -> Resolved(case)
Pending -> Forwarded(new obligation identity)
Pending -> Cancelled(cancel cleanup plan)
```

`CreatePendingOutcomeObligation` is derived at the original call boundary.
`ResolveOutcomeCase` belongs to the later consuming match/direct-outcome
operation and selects exactly one prevalidated arm. Forwarding preserves the
complete case matrix under a new destination/obligation identity. Cancellation
must name the cleanup/liability result for every still-pending arm. An
obligation cannot disappear, resolve twice, be used as initialized while
pending, or collapse `remain-uninit` into an initialized state.

Per-arm cleanup liability is part of `CasePlan`, not a later CodeGen guess.

### Call phase plan

Each validated call has a strict phase plan:

```text
Evaluation -> Boundary -> Finalization
```

- `Evaluation` prepares actual-expression and nested-call effects in source
  order without applying the outer formal boundary.
- `Boundary` applies actions derived from validated transfer edges and may
  install call-region loans or create pending outcome obligations.
- `Finalization` ends call/full-expression regions, ends boundary loans,
  transfers explicitly extended loans, completes or extends untransferred
  temporary cleanup, and disarms cleanup whose liability moved to a
  destination.

An unresolved pending outcome obligation may intentionally survive the
original call's Finalization phase; its later match/forward/cancel operation
owns resolution. A boundary loan or cleanup may survive only through its typed
region-transfer disposition and longer-lived region witness.

## Derived typed journal

### Phases

The synthetic journal has three typed phases:

```text
Evaluation
    effects intrinsic to evaluating an expression or nested validated call

Boundary
    actions introduced by a selected formal, receiver, return, capture,
    init, or outcome boundary

Finalization
    post-boundary loan termination, cleanup completion/disarm, and region end
```

Each action carries a structural `JournalActionId`, phase, source semantic
node, typed payload, and required precondition witness. Transfer-derived
actions also carry their `TransferEdgeId`. String opcodes, display-path
subjects, `void *`, and untagged integer identities are forbidden.

### Required evaluation actions

| Action | Required typed facts |
| --- | --- |
| `WritePlace` | exact `PlaceId`, write kind, prior-state witness |
| `BeginLoan` | `LoanId`, source/referent `PlaceId`, `RegionId`, capability |
| `EndLoan` | live `LoanId`, matching region/end reason |
| `CreateTemporary` | `TemporaryId`, `TypeId`, producing node |
| `ScheduleTemporaryCleanup` | cleanup plan with owner, `TypeId`, current region, liability and region-exit disposition |
| `CompleteTemporaryCleanup` | live `CleanupId`, completion reason |
| `ApplyNestedCall` | `ValidatedCallId`, ordered nested journal/model patch |

`EndLoan` is mandatory: a region ending cannot be represented by deleting a
borrow from a copied PAL snapshot. Temporary cleanup also has an explicit
terminal action; disappearance from a mask is not proof of completion.

### Required boundary actions

| Action | Required typed facts |
| --- | --- |
| `InvalidateWholePlace` | edge, admitted source/destination, prior-state witness |
| `InvalidateProjection` | edge, exact source/destination, edge cleanup mask/admission witness |
| `InstallBoundaryBorrow` | edge, `LoanId`, source/referent, destination, call region, capability |
| `TransferDropLiability` | edge, tagged liability source/target, `TypeId` properties |
| `TransferTemporaryLiability` | edge, temporary `CleanupId`, destination liability target |
| `RetainSharedLiability` | edge, shared source/destination and retain/release proof |
| `RecordDependencyHandoff` | edge, destination, dependency roots, boundary kind |
| `FulfillImmediateInitObligation` | edge, obligation, destination, before/after place state |
| `CreatePendingOutcomeObligation` | edge and complete immutable pending obligation/case matrix |
| `ResolveOutcomeCase` | pending obligation, selected case plan, consuming operation identity |
| `ForwardPendingOutcome` | pending obligation and derived destination/obligation identity |
| `CancelPendingOutcome` | pending obligation and complete cancel cleanup/liability plan |

`KeepSourceLive` remains a validated plan fact and never creates a journal
action. A no-change plan is not an excuse to emit a state mutation.

### Required finalization actions

| Action | Required typed facts |
| --- | --- |
| `EndBoundaryLoan` | edge, live boundary `LoanId`, matching call `RegionId` |
| `TransferBoundaryLoanRegion` | edge, loan, old call region, longer new region, destination |
| `CompleteFullExpressionCleanup` | edge, live untransferred `CleanupId`, terminal reason |
| `DisarmTransferredCleanup` | edge, transferred `CleanupId`, matching liability target |
| `TransferCleanupRegion` | edge, cleanup, old region, strictly longer new region, destination |
| `EndCallRegion` | call `RegionId`, proof that all non-escaping loans/cleanups are terminal |

Finalization actions are derived together with their boundary actions. A
boundary loan cannot reuse evaluation `EndLoan` and must exactly once end or
transfer from the call region. At each cleanup-region exit, complete, disarm,
and region transfer are mutually exclusive.

### Model patch staging

The immutable `SemanticModelPatch` is orthogonal to call phases and is not a
fourth journal lane. It is staged with the validated call/transaction under a
`SemanticModelPatchId`; table entries and patch identity are strongly typed
and carry no AST pointer mutation.

### Journal validity

Before a transaction can transition to `Validated`, its synthetic validator
must prove:

1. every action ID is valid and unique within the transaction;
2. phase and payload kind agree and phases are ordered Evaluation, Boundary,
   Finalization;
3. every `EndLoan` matches exactly one live loan and no loan is ended twice;
4. each scheduled cleanup carries a region and has exactly one disposition at
   every region exit; transfer chains strictly outlive their predecessor and
   eventually reach one completion or disarm, with no cleanup silently lost;
5. place invalidations have exact-place/admission witnesses and compatible
   cleanup masks;
6. every transfer-derived action references one complete validated edge and
   agrees with its source, destination, mask, liability, dependency, and
   shared disposition;
7. drop/shared liability is conserved per edge;
8. immediate init and pending outcome transitions consume the correct live
   obligation and legal prior state, with every pending case accounted for;
9. all call-region boundary loans exactly once end or transfer during
   Finalization, and all cleanup region exits reach one legal disposition;
10. nested-call journals are already validated and preserve source order;
11. model patches have valid keys and no unequal duplicate entry; and
12. no boundary intent was applied to evaluation working state before
    whole-boundary validation.

The validated journal is immutable. Adoption/publication uses its prebuilt
successor state; it never interprets or executes string commands against live
state.

## Branch sibling merge

Branch merging is not sequential `adopt(child)` and does not extend the D.1
transaction lifecycle with another public terminal state.

An open parent creates a parent-owned `BranchFrameSet` identified by
`BranchSetId`. Each `BranchFrame` has a unique structural `BranchKey`, the same
captured parent identity/base epoch/base snapshot, and isolated manifest state,
journal, diagnostics, internal facts, and model patch. A frame exposes no
`adopt()`/`commit()` API.

The dedicated lifecycle is:

```text
BranchFrameSet: TopologyOpen -> Open -> Sealed -> Merged
                any preterminal state ---------> Discarded

BranchFrame:    Provisional -> Registered -> Sealed -> Consumed
                Provisional -> Removed          topology not frozen
                Registered/Sealed -> Discarded  whole-set discard only
```

Frames may be added or removed only while the set is `TopologyOpen` and the
frame is provisional. `freezeTopology()` captures the complete path inventory
and converts every remaining frame to `Registered`; it also captures parent
identity/base epoch/base snapshot. No registered frame can be removed.

Every registered source path must seal as exactly one of reachable,
unreachable, or explicit unchanged-base. A missing `else` is a registered
unchanged-base frame. If any registered/sealed frame is discarded or otherwise
cannot seal, its owning set must transition wholly to `Discarded`, discard all
frames, and forbid set seal/merge. Parent state, epoch, and digest remain
unchanged.

`Removed`, `Merged`, `Consumed`, and `Discarded` are terminal. Repeated
freeze/seal/merge/discard or mutation after a terminal state fails closed as an
internal lifecycle error.

After branch analysis, each registered frame is sealed with its explicit path
fact. The set prebuilds one
`BranchMergePatch` using the common base and the normative policies below,
then applies that patch to the still-open parent with one no-throw immutable
state swap and one epoch advance. All frames are consumed by their owning set.
Failure leaves the parent and every unconsumed frame unchanged.

Both before merge prebuild and immediately before the final swap, the set must
prove:

```text
parent identity == captured parent identity
parent current epoch == BranchFrameSet base epoch
```

A mismatch is `StaleParent`: parent/set/frame lifecycle, epoch, manifest/model
digest, and published snapshot remain unchanged. No rebase is permitted.

Merge order is canonical `BranchKey` order and must produce the same result
when input enumeration is reversed. An unreachable branch contributes the
fact-lattice bottom. The frozen topology, not the set of surviving frames,
defines the merge inputs.

### Join algebra

Every lattice-governed state family supplies a typed `BranchJoinSpec` with
`bottom`, equality, and `join`. Synthetic exhaustive/property tests must prove
for all fixture values:

```text
join(a, b) = join(b, a)
join(join(a, b), c) = join(a, join(b, c))
join(a, a) = a
join(bottom, a) = a
```

Sorting frames by `BranchKey` is only deterministic enumeration; it is not a
substitute for these commutative, associative, idempotent, and bottom-identity
laws. Conflict-checked map/model union is a partial join: the same laws must
hold for compatible inputs, while any incompatible grouping rejects with no
state change.

### Merge policies

| State family | Sibling merge rule |
| --- | --- |
| reachability | registered unreachable frames contribute bottom; all unreachable yields unreachable |
| init/place facts | existing definite-state lattice join; disagreement becomes Maybe/fail-closed fact |
| moved/used/mutated facts | moved/place lattice join; used/mutated union, never last-writer wins |
| exact projection masks | projection-wise join under the same admitted plan; incompatible plans reject |
| PAL loans/conflicts | common-base branch join; escaping loans require compatible identity/region |
| dependency/capability facts | dependency obligations union; available capabilities intersect across reachable frames |
| temporaries/cleanups | branch-local obligations must close; escaping conditional cleanup must have one compatible joined identity or reject |
| init/outcome obligations | join only through declared transition lattice; unresolved incompatible obligations reject |
| generic/model/side tables | deterministic key union; equal duplicates coalesce, unequal duplicates reject |
| diagnostics/warnings | stable source/diagnostic-key union with frozen dedup rules |
| internal facts/index | stable identity-key union; public Evidence rules remain separate |
| identities/source origins | no allocation or remap; equal key must mean equal full identity |
| pure caches | excluded only under the D.1 cold/warm/order-invariance proof |

M1b.0b tests two- and three-sibling joins only. Loop fixed-point/backedge policy
remains M1b.1 design work and cannot be inferred from sibling merge.

## Immutable elaboration and lowering side tables

The synthetic `SemanticModelPatch` must cover the following table shapes even
though M1b.0b stores only fixture payloads:

| Side table | Key | Logical payload |
| --- | --- | --- |
| `DeclarationFacts` | `DeclarationId`/resolved declaration identity | owner coordinates, signature/contract and boundary facts |
| `TypePropertiesByType` | `TypeId` | immutable Copy/ownership/dependency/drop/send/sync properties |
| `ExprFactsTable` | `SemanticNodeId` | candidate-relative type/value/place/dependency facts |
| `ResolvedCallTable` | `CallSiteId` | `ResolvedCalleeId`, formal identities, route/effect/boundary facts |
| `ImplicitConversionTable` | `ConversionId` | expression, source/destination `TypeId`, conversion kind |
| `DefaultArgumentTable` | formal-slot `DestinationId` | `SourceOriginId` and derived synthetic node identity |
| `SyntheticArgumentTable` | call site + synthetic role | derived node identity and logical position |
| `ReceiverLoweringTable` | call site | receiver facts and callable/method lowering recipe |
| `GenericInstanceTable` | generic instance identity | template declaration, substitution, owner coordinate |
| `TemporaryCleanupTable` | `CleanupId` | owner, type/liability, current region, transfer chain and terminal recipe |
| `InitOutcomeTable` | obligation/transition identity | destination, pending case matrix and legal state transition |
| `ValidatedCallTable` | `ValidatedCallId`/call site | immutable prepared/validated plan and journal reference |
| `LoweringRecipeTable` | `LoweringRecipeId` | logical argument/receiver/default/synthetic recipe |
| `SourceOriginTable` | derived semantic identity | `SourceOriginId` and parsed/template origin relation |

All tables plus the complete transactional manifest state belong to one
immutable revision:

```text
SemanticRevision
    SemanticRevisionId
    TransactionalStateManifest snapshot/digest
    SemanticModel              all side tables above
    Source-origin/cross-reference index

PublishedSemanticSnapshot
    shared immutable handle to exactly one SemanticRevision
```

Root publication prebuilds and validates the complete successor revision, then
performs one no-throw snapshot-handle swap. Observers pin one handle and may not
combine tables or manifest state obtained from different revisions.

Default arguments, implicit conversions, generic specializations, callable
receivers, and synthetic arguments are side-table facts. Preparation may not
insert arguments, set `ResolvedFn`, write resolved types, or otherwise mutate
the source AST.

Patch union is deterministic. Repeating an equal entry is idempotent; the same
key with a different payload rejects before adoption. A lowering recipe is
logical and contains no LLVM value, slot address, ABI decision, or CodeGen
state.

The same collision/cross-reference validator is mandatory for candidate child
to parent, nested child to candidate, branch merge, and root to published
revision:

- different complete keys coexist even when their hash values collide;
- same complete key plus equal payload coalesces idempotently;
- same complete key plus unequal payload rejects atomically;
- every referenced identity exists in the same successor revision or its
  immutable base, with the expected identity domain; and
- no cleanup, lowering recipe, call, type, declaration, or source-origin entry
  may publish with a dangling/cross-revision identity.

Any failure preserves operation lifecycle, parent/root epoch, complete model
and manifest digest, and the published snapshot handle.

Diagnostics from every analyzed branch follow the existing diagnostic
reachability policy and are merged independently from post-branch semantic
reachability. Removing an unreachable frame from the state lattice cannot
silently erase a diagnostic that the active language policy requires.

## Rejected analysis and frozen Evidence v1

M1b.0b may define an internal immutable `RejectedAnalysisResult` containing:

```text
DiagnosticBuffer
WarningDedupResult
InternalRejectionFact[]
Source/contract locations
```

It must not contain or publish public
`toka.cede-obligation-evidence` v1 records. In M1b.0b and later pre-activation
Shadow slices:

- the transaction implementation never calls the `SemanticEvidence`
  singleton or v1 recorder;
- selected diagnostics publish atomically after the semantic transaction is
  discarded;
- internal rejection facts are comparison/test data only;
- RC8 legacy analysis remains the sole producer of Evidence v1; and
- normal, Shadow, and repeated-probe v1 output must remain byte-identical.

No adapter may reinterpret an implicit signature-driven plan as a fulfilled v1
caller spelling obligation. Committed transactional evidence requires the
separately versioned Evidence v2 activation artifact.

## Executable fault-injection matrix

M1b.0b exposes only this finite enum:

```text
SyntheticFaultPoint
    None
    PatchUnion
    FullKeyCollisionValidation
    CrossReferenceValidation
    ImmutableSuccessorBuild
    ManifestDigest
    BranchLatticeJoin
    RejectedResultPrebuild
    AdoptSuccessorBuild
    RootSuccessorBuild
    PreSwap
```

Injection is an explicit one-operation test parameter owned by the synthetic
fixture. It is never a global/static flag, environment variable, CLI option,
or production behavior path.

| Fault point | Required state after injected failure |
| --- | --- |
| `PatchUnion` | operation owner stays pre-operation; parent/root epoch, model/manifest digest and published snapshot unchanged |
| `FullKeyCollisionValidation` | same lifecycle as entry; no entry coalesced/rejected partially; all frames/children unchanged |
| `CrossReferenceValidation` | same lifecycle as entry; no dangling entry staged; all revision handles/digests unchanged |
| `ImmutableSuccessorBuild` | no successor swap; caller/child/frame/root states and epochs unchanged |
| `ManifestDigest` | no lifecycle transition; old digest remains authoritative and complete |
| `BranchLatticeJoin` | parent remains `Open` at captured epoch; set/frames remain `Sealed`; all digests unchanged |
| `RejectedResultPrebuild` | selected transaction remains `Open`; rejected-output snapshot and semantic parent remain unchanged |
| `AdoptSuccessorBuild` | parent remains `Open`; child remains `Validated`; both epochs/digests unchanged |
| `RootSuccessorBuild` | root remains `Validated`; published revision ID/handle/digest unchanged |
| `PreSwap` | operation-specific pre-swap states above remain unchanged; no observer sees a new revision/output |

Each fault is exercised at candidate-child, nested-child, branch, and root
boundaries where applicable. Tests compare complete D.1 manifest, model,
identity/cache, lifecycle, epoch, journal, diagnostics/internal-fact, and
published-handle digests—not a selected subset.

The final immutable pointer/handle swap is not a fault point. It must be
compile-time `noexcept` and tested to expose only the complete old or complete
new snapshot. There is no injectable or fallible operation after the swap.

## Synthetic reference implementation boundary

If this contract is accepted, M1b.0b implementation is limited to:

- controlled identity builders and the new strong value domains;
- D.1 lifecycle states/errors with structural transaction/fork identity;
- fixture-backed `TransactionalStateManifest` and complete digest;
- immutable validated transfer edges, tagged liability sources/targets, and
  pending outcome obligations;
- derived Evaluation/Boundary/Finalization journal/action validation;
- parent-owned sibling `BranchFrameSet` merge;
- immutable fixture side-table patches and single-handle `SemanticRevision`;
- atomic published-revision and rejected-output swaps; and
- the finite `SyntheticFaultPoint` matrix before each applicable successor
  swap.

It remains a `BUILD_TESTING`-only target and does not link `toka_frontend`.
Production source files may not include its transaction/journal header. The
M1b.0a value header may be extended only with reviewed strong identities and
the controlled builder; the empty production `SemanticModel` gains no side
tables or mutation API in 0b.

## Qualification matrix

The M1b.0b review receipt must include all of the following.

### Identity and builder

- raw-key production construction is inaccessible;
- all new domains reject invalid components and implicit cross-domain use;
- probe/fork/branch order produces identical identities;
- argument-plan/transfer-edge/revision identities remain structurally stable;
- engaged/absent trait substitution and every callee tag remain valid/invalid
  as specified; and
- projection order changes structured place identity and hash/equality results.

### Lifecycle and publication

- nonempty discard/adopt and nested bottom-up adoption;
- wrong-parent/stale child and every double/terminal lifecycle rejection;
- child commit rejection and root-only single commit;
- injected successor-build failure preserves complete state/digest; and
- observers see only old or complete-new immutable state, never a prefix.

### Typed actions

- success and invalid-phase cases for every required action kind;
- strict Evaluation/Boundary/Finalization ordering;
- identical `CopyValue` mode edges proving `KeepLive` versus
  `InvalidateWhole/Projection` behavior and derived-action differences;
- KeepLive-with-invalidation, invalidation-without-admission/action, whole/
  projection path/mask mismatch, and NoSourcePlace-with-source rejection;
- mismatched edge source/destination/mask/liability/dependency/shared-mode
  actions reject even when each payload is individually valid;
- unmatched/double `EndLoan` rejection;
- boundary loan End/Transfer success matrix plus missing/double/mismatched
  region-transfer rejection;
- cleanup Complete/Disarm/TransferRegion success matrix, mutual-exclusion,
  non-outliving/cyclic transfer, and missing/double ultimate terminal rejection;
- liability conservation failures;
- invalid exact-place/mask and immediate init transitions;
- pending outcome remain-uninit, resolve, forward, cancel, double resolution,
  and per-arm cleanup/liability matrices; and
- nested journal ordering and boundary-intent deferral.

### Branch merge

- two/three siblings, reversed enumeration, unreachable branch, and implicit
  base/else path;
- BranchFrameSet/Frame lifecycle and double/terminal operation rejection;
- provisional removal before topology freeze, plus registered-frame discard
  forcing whole-set discard and permanent merge rejection with unchanged
  parent/epoch/digest;
- stale parent detection before prebuild and immediately before swap;
- commutative, associative, idempotent, and bottom-identity property tests for
  every lattice family and compatible partial-join family;
- place/PAL/dependency lattice results;
- compatible/incompatible cleanup and init/outcome obligations;
- equal side-table coalescing and unequal collision rejection; and
- merge fault injection with unchanged parent/frame digests.

### Semantic revision and side tables

- `DeclarationFacts`, `TypePropertiesByType`, and every elaboration/lowering
  table publish through one `SemanticRevision` snapshot handle;
- collision matrices run at child-parent, nested-child, branch, and root
  publication boundaries;
- different full keys with forced equal hashes coexist;
- same key/equal payload coalesces and same key/unequal payload rejects;
- cross-table reference closure rejects missing or wrong-domain identities;
- collision/reference failures preserve lifecycle, epoch, complete digest, and
  published revision handle; and
- observers cannot combine tables from different revisions.

### Fault matrix

- every applicable `SyntheticFaultPoint` is exercised with the exact expected
  lifecycle/epoch/digest state above;
- fault order/repetition is deterministic and has no global residue; and
- final publication swap is statically and dynamically proven `noexcept`.

### Rejection and compatibility

- diagnostics/internal facts publish only as one complete rejected result;
- discarded semantic state remains unchanged;
- Evidence v1 is absent from the synthetic API and byte-identical in compiler
  parity gates;
- no production include/link/CLI/flag/global/static initialization; and
- full build, CTest, M1a Shadow, Evidence v1, JSON CLI, conformance, pass, and
  fail gates remain green in the qualification revision.

## Admission decision

Independent D.2 review authorized only the `BUILD_TESTING`-only synthetic
M1b.0b reference slice. That implementation is complete and awaits its required
post-implementation acceptance. It does not admit M1b.1 Sema consumption,
ownership commit, CodeGen consumption, Evidence v2, or the caller-spelling
behavior change.

## Local implementation receipt

The implementation adds two unlinked `BUILD_TESTING` targets:

```text
toka_synthetic_transfer_contract
toka_synthetic_semantic_transaction
```

They cover controlled strong identities, tagged destinations, transfer-edge
and pending-outcome validation, every action phase/kind, nested action-ID
uniqueness, loan/cleanup region terminals, lifecycle/stale/double operations,
manifest/model digests, branch topology and semilattice laws, revision
collision/reference closure, rejected output, and every named fault family.

Local qualification at the implementation revision passed:

- full build;
- CTest 20/20;
- M1a Shadow v3: 41 receipts plus four normal cases;
- Cede Evidence v1 ABI gate;
- public JSON CLI: 17/17;
- Pass: 451/451;
- Fail: 473/473; and
- Conformance: 308/308.

Production source, `toka_frontend`, CLI/LSP, Sema/PAL, CodeGen, Evidence, and
hosted targets do not include or link the synthetic transaction/journal
support. The production `SemanticModel` remains empty and immutable.
