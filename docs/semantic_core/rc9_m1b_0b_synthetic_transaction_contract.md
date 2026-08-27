# RC9 M1b.0b Synthetic Transaction Contract

**Design status:** Proposed for independent M1b.0b review.

**Implementation status:** Not implemented and not admitted by this document.
No transaction/journal class, identity builder, Sema/PAL/Evidence/CodeGen/CLI
wiring, behavior flag, TKI change, or ABI change is authorized until this
contract is accepted.

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

## Typed journal

### Lanes

The synthetic journal has three typed lanes:

```text
Evaluation
    effects intrinsic to evaluating an expression or nested validated call

Boundary
    actions introduced by a selected formal, receiver, return, capture,
    init, or outcome boundary

Model
    immutable SemanticModel/elaboration/lowering patch publication
```

Each action carries a structural `JournalActionId`, lane, source semantic node,
typed payload, and required precondition witness. String opcodes, display-path
subjects, `void *`, and untagged integer identities are forbidden.

### Required evaluation actions

| Action | Required typed facts |
| --- | --- |
| `WritePlace` | exact `PlaceId`, write kind, prior-state witness |
| `BeginLoan` | `LoanId`, source/referent `PlaceId`, `RegionId`, capability |
| `EndLoan` | live `LoanId`, matching region/end reason |
| `CreateTemporary` | `TemporaryId`, `TypeId`, producing node |
| `ScheduleTemporaryCleanup` | `CleanupId`, owner temporary/place, liability |
| `CompleteTemporaryCleanup` | live `CleanupId`, completion reason |
| `ApplyNestedCall` | `ValidatedCallId`, ordered nested journal/model patch |

`EndLoan` is mandatory: a region ending cannot be represented by deleting a
borrow from a copied PAL snapshot. Temporary cleanup also has an explicit
terminal action; disappearance from a mask is not proof of completion.

### Required boundary actions

| Action | Required typed facts |
| --- | --- |
| `InvalidateWholePlace` | admitted whole `PlaceId`, destination, prior-state witness |
| `InvalidateProjection` | exact projected `PlaceId`, cleanup mask, admission witness |
| `InstallBoundaryBorrow` | `LoanId`, source/referent, destination, region, capability |
| `TransferDropLiability` | source/cleanup identity, destination, `TypeId` properties |
| `TransferTemporaryLiability` | `CleanupId`, `DestinationId`, target liability |
| `RetainSharedLiability` | source, destination, retain/release proof |
| `RecordDependencyHandoff` | destination, dependency roots, boundary kind |
| `FulfillInitObligation` | `InitObligationId`, destination, before/after place state |
| `ApplyOutcomeTransition` | `OutcomeTransitionId`, destination, selected case, before/after state |

`KeepSourceLive` remains a validated plan fact and never creates a journal
action. A no-change plan is not an excuse to emit a state mutation.

### Required model actions

Model actions stage complete immutable entries for the side tables defined
below through `StageModelPatch(SemanticModelPatchId, immutable patch)`.
Table entries and patch identity are strongly typed and carry no AST pointer
mutation.

### Journal validity

Before a transaction can transition to `Validated`, its synthetic validator
must prove:

1. every action ID is valid and unique within the transaction;
2. lane and payload kind agree;
3. every `EndLoan` matches exactly one live loan and no loan is ended twice;
4. each scheduled cleanup ends in exactly one completion or liability
   transfer, and no cleanup is silently lost;
5. place invalidations have exact-place/admission witnesses and compatible
   cleanup masks;
6. drop/shared liability is conserved;
7. init and outcome transitions consume the correct live obligation and legal
   prior state;
8. nested-call journals are already validated and preserve source order;
9. model patches have valid keys and no unequal duplicate entry; and
10. no boundary intent was applied to evaluation working state before
    whole-boundary validation.

The validated journal is immutable. Adoption/publication uses its prebuilt
successor state; it never interprets or executes string commands against live
state.

## Branch sibling merge

Branch merging is not sequential `adopt(child)` and does not extend the D.1
transaction lifecycle with another public terminal state.

An open parent creates a parent-owned `BranchFrameSet` identified by
`BranchSetId`. Each `BranchFrame` has a unique structural `BranchKey`, the same
parent identity/base epoch/base snapshot, and isolated manifest state,
journal, diagnostics, Evidence, and model patch. A frame exposes no
`adopt()`/`commit()` API.

After branch analysis, each frame is sealed with an explicit reachability
fact. The set prebuilds one
`BranchMergePatch` using the common base and the normative policies below,
then applies that patch to the still-open parent with one no-throw immutable
state swap and one epoch advance. All frames are consumed by their owning set.
Failure leaves the parent and every unconsumed frame unchanged.

Merge order is canonical `BranchKey` order and must produce the same result
when input enumeration is reversed. An unreachable branch contributes the
fact-lattice bottom. A missing `else` contributes an explicit unchanged-base
frame rather than silently omitting a path.

### Merge policies

| State family | Sibling merge rule |
| --- | --- |
| reachability | remove unreachable frames; all unreachable yields unreachable |
| init/place facts | existing definite-state lattice join; disagreement becomes Maybe/fail-closed fact |
| moved/used/mutated facts | moved/place lattice join; used/mutated union, never last-writer wins |
| exact projection masks | projection-wise join under the same admitted plan; incompatible plans reject |
| PAL loans/conflicts | common-base branch join; escaping loans require compatible identity/region |
| dependency/capability facts | dependency obligations union; available capabilities intersect across reachable frames |
| temporaries/cleanups | branch-local obligations must close; escaping conditional cleanup must have one compatible joined identity or reject |
| init/outcome obligations | join only through declared transition lattice; unresolved incompatible obligations reject |
| generic/model/side tables | deterministic key union; equal duplicates coalesce, unequal duplicates reject |
| diagnostics/warnings | stable source/diagnostic-key union with frozen dedup rules |
| internal Evidence/index | stable identity-key union; public Evidence rules remain separate |
| identities/source origins | no allocation or remap; equal key must mean equal full identity |
| pure caches | excluded only under the D.1 cold/warm/order-invariance proof |

M1b.0b tests two- and three-sibling joins only. Loop fixed-point/backedge policy
remains M1b.1 design work and cannot be inferred from sibling merge.

## Immutable elaboration and lowering side tables

The synthetic `SemanticModelPatch` must cover the following table shapes even
though M1b.0b stores only fixture payloads:

| Side table | Key | Logical payload |
| --- | --- | --- |
| `ExprFactsTable` | `SemanticNodeId` | candidate-relative type/value/place/dependency facts |
| `ResolvedCallTable` | `CallSiteId` | `ResolvedCalleeId`, formal identities, route/effect/boundary facts |
| `ImplicitConversionTable` | `ConversionId` | expression, source/destination `TypeId`, conversion kind |
| `DefaultArgumentTable` | formal-slot `DestinationId` | `SourceOriginId` and derived synthetic node identity |
| `SyntheticArgumentTable` | call site + synthetic role | derived node identity and logical position |
| `ReceiverLoweringTable` | call site | receiver facts and callable/method lowering recipe |
| `GenericInstanceTable` | generic instance identity | template declaration, substitution, owner coordinate |
| `TemporaryCleanupTable` | `CleanupId` | owner, type/liability, scheduled terminal action |
| `InitOutcomeTable` | obligation/transition identity | destination and legal state transition |
| `ValidatedCallTable` | `ValidatedCallId`/call site | immutable prepared/validated plan and journal reference |
| `LoweringRecipeTable` | `LoweringRecipeId` | logical argument/receiver/default/synthetic recipe |
| `SourceOriginTable` | derived semantic identity | `SourceOriginId` and parsed/template origin relation |

Default arguments, implicit conversions, generic specializations, callable
receivers, and synthetic arguments are side-table facts. Preparation may not
insert arguments, set `ResolvedFn`, write resolved types, or otherwise mutate
the source AST.

Patch union is deterministic. Repeating an equal entry is idempotent; the same
key with a different payload rejects before adoption. A lowering recipe is
logical and contains no LLVM value, slot address, ABI decision, or CodeGen
state.

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

## Synthetic reference implementation boundary

If this contract is accepted, M1b.0b implementation is limited to:

- controlled identity builders and the new strong value domains;
- D.1 lifecycle states/errors with structural transaction/fork identity;
- fixture-backed `TransactionalStateManifest` and complete digest;
- typed journal/action validation;
- parent-owned sibling `BranchFrameSet` merge;
- immutable fixture side-table patches;
- atomic published-state and rejected-output swaps; and
- deterministic fault injection before each successor-state swap.

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

- success and invalid-lane cases for every required action kind;
- unmatched/double `EndLoan` rejection;
- missing/double temporary cleanup terminal rejection;
- liability conservation failures;
- invalid exact-place/mask, init, and outcome transitions; and
- nested journal ordering and boundary-intent deferral.

### Branch merge

- two/three siblings, reversed enumeration, unreachable branch, and implicit
  base/else path;
- place/PAL/dependency lattice results;
- compatible/incompatible cleanup and init/outcome obligations;
- equal side-table coalescing and unequal collision rejection; and
- merge fault injection with unchanged parent/frame digests.

### Rejection and compatibility

- diagnostics/internal facts publish only as one complete rejected result;
- discarded semantic state remains unchanged;
- Evidence v1 is absent from the synthetic API and byte-identical in compiler
  parity gates;
- no production include/link/CLI/flag/global/static initialization; and
- full build, CTest, M1a Shadow, Evidence v1, JSON CLI, conformance, pass, and
  fail gates remain green in the qualification revision.

## Admission decision

This document alone admits no implementation. After independent review it may
authorize the `BUILD_TESTING`-only synthetic M1b.0b reference slice. Passing
that slice admits another review; it does not admit M1b.1 Sema consumption,
ownership commit, CodeGen consumption, Evidence v2, or the caller-spelling
behavior change.
