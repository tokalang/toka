# RC9 M1b Transactional Semantic Planning Design

**Design status:** M1b-D.1 accepted.

**Implementation admission:** M1b.0a opaque identities and an empty immutable
`SemanticModel` are admitted. M1b.0b transaction lifecycle/journal APIs require
a separate implementation review against this D.1 contract. All Sema wiring
remains blocked until its state-manifest and synthetic lifecycle gates pass.
No semantic behavior, PAL state, CodeGen path, evidence contract, TKI format,
or ABI is changed by this design.

**Implementation status:** M1b.0a scaffold implemented and unit-qualified.
M1b.0b and all semantic consumers remain unimplemented.

**Design baseline:**
`20410f4cbe6615a3c4a662fd6d64def0e956de0a`.

**Governing language decision:**
[`RC9 Signature-Driven Call Transfer ADR`](rc9_signature_driven_call_transfer_adr.md).

## Decision

M1b will not promote the M1a `CallExpr::ShadowArgumentTransfers` /
`MethodCallExpr::ShadowArgumentTransfers` vectors into commit authority.
Those vectors remain audit-prototype carriers and are retired after the new
planner reaches source/source-less and route parity.

The production implementation will use:

1. immutable expression and type facts;
2. structured semantic identities for nodes, declarations, and places;
3. a discardable, nestable analysis transaction for every candidate or
   speculative traversal;
4. one whole-call prepare/validate result;
5. an immutable `SemanticModel` side table as the handoff to CodeGen and
   evidence; and
6. fail-closed CodeGen consumption for every migrated ownership-sensitive
   route.

The resulting pipeline is:

```text
resolve candidates
    -> prepare expression facts in candidate transactions
    -> select resolved formal/declaration identities
    -> plan the receiver and all arguments in one call transaction
    -> validate type, place, PAL, dependency, boundary, and liability facts
    -> adopt the validated child once into the parent transaction
    -> publish the final SemanticModel after Sema succeeds
    -> execute validated plans in CodeGen
```

`prepare` and `validate` are pure relative to their parent/global semantic
state. They may update a transaction-local working state so later arguments
observe earlier arguments in the language's evaluation order. A rejected or
discarded transaction has no externally observable semantic-state effect;
selected rejection output is published only through the separate immutable
result defined below.

## Why M1a is not the production carrier

M1a was intentionally built beside the RC8 checker. It proved route discovery
and exposed hidden coupling, but it is not safe commit input:

- plans are stored on mutable AST nodes;
- legacy `checkExpr()` has already changed ownership state before a plan is
  recorded;
- overload, generic, closure, and dynamic-dispatch paths can revisit or mutate
  the same AST;
- Copy, dependency, exact-place, and drop facts still come from separate
  classifiers; and
- plans are recorded per argument rather than admitted as one call.

Adding rollback code or more `m_Is...` probe flags would preserve those
failure modes. M1b therefore introduces a new semantic boundary rather than
incrementally granting authority to the prototype vectors.

## Semantic identities

### Semantic node identity

Each parsed or compiler-created semantic node has a session-stable
`SemanticNodeId`. A clone or monomorphized body receives a new node identity
and separately records its source-origin identity. Pointer addresses, display
paths, and source locations are not semantic keys.

The `SemanticModel` is scoped to one compilation revision, so IDs need not be
stable across independent compiler invocations. Deterministic evidence uses
source/declaration coordinates rather than serializing session-local IDs.

M1b uses structural identity construction rather than a probe-visible global
allocator:

- parsed nodes are assigned deterministically from compilation-unit identity,
  source-origin identity, expansion path, and structural ordinal before Sema
  candidate probing;
- a monomorphized clone derives its node IDs from template origin, canonical
  substitution identity, clone-local structural path, and ordinal; and
- a synthetic node derives its ID from its owning semantic node,
  `SyntheticRole`, and role-local ordinal.

Discarding or reordering probes therefore cannot consume an ID. Transaction-
local allocation followed by remapping is not part of M1b. A legacy global
node counter may coexist during migration, but it is not a `SemanticNodeId`
source and must not enter the new `SemanticModel`.

Identity equality is equality of the canonical structural key. A digest or
hash may accelerate lookup but cannot be the sole collision-blind authority;
containers must resolve collisions against the complete canonical key.

### Resolved declaration identity

Every selected callee is represented by a tagged `ResolvedCalleeId`:

```text
ResolvedCalleeId
    DirectDeclaration(DeclarationId)
    GenericInstance(TemplateDeclarationId, SubstitutionId)
    TraitSlot(TraitDeclarationId, MethodSlotId, SubstitutionId?)
    IndirectFunction(CallableContractId)
    IndirectDynFunction(CallableContractId)
    ExternDeclaration(DeclarationId)
    LangItem(LangItemId, DeclarationId)
```

Declaration owners include resolver-proven crate/module coordinates. Generic
instances retain their template-origin identity and canonical concrete
substitution. Indirect `fn`/`dyn fn` calls identify a resolved callable
contract rather than inventing a concrete declaration.

Execution boundaries are declaration facts, such as
`ExecutionBoundaryKind::ThreadHandoff`, attached through trusted resolver or
lang-item identity. Callee spelling is never boundary authority. Aliases reach
the same declaration fact; a user same-named declaration does not.

### Place identity

Ownership and PAL operations use one structured place representation:

```text
PlaceId
    RootSymbolId
    Projection[]

Projection
    Field(member identity)
    ConstantIndex(value)
    DynamicIndex(expression identity)
    Dereference
    Unknown
```

`PlaceId` preserves three independent coordinates:

- `source_place`: the caller binding or projection whose state may change;
- `referent_place`: the PAL root/projection reached through a borrowed view;
  and
- `dependency_roots`: lifetime owners that constrain escape or handoff.

A display string is derived from `PlaceId`; it is never parsed back into
authority. Dynamic or unknown projections may be represented for diagnostics,
but they fail closed for ownership invalidation unless an existing exact-place
rule explicitly admits them.

## Unified type properties

M1b introduces one recursive, memoizable query:

```text
TypeProperties classifyType(TypeId)
```

At minimum it returns:

```text
copy          ProvenCopy | ProvenNonCopy | Indeterminate
ownership     Plain | BorrowedView | UniqueOwner | SharedOwner
              | StructuralOwner | Indeterminate
dependency    None | Borrowed | RawUnsafe | Structural | Indeterminate
drop          NoLiability | StructuralLiability | CustomLiability
send          Proven | Rejected | Indeterminate
sync          Proven | Rejected | Indeterminate
```

The query recursively handles primitives, references/raw identities, smart
handles, arrays, shapes, callables, generic substitutions, opaque/interface
types, and `T | miss`. Generic or source-hidden information that is
insufficient for a proof returns `Indeterminate`; absence of a visible drop
hook is not a Copy proof.

Sema planning, exact-place admission, PAL boundary validation, evidence, and
CodeGen completeness checks consume these properties. They may not maintain
route-local type-name allowlists or rederive ownership from layout.

Memoization is allowed during a probe only when the cache is referentially
transparent, deterministically keyed, and cannot affect diagnostics,
declaration identity, candidate selection, or emitted output.

## Expression facts

Argument preparation produces immutable facts independently from any transfer
decision:

```text
ExprFacts
    TypeId
    ValueCategory           Place | Temporary | InitStorage | Indeterminate
    SourcePlace?            structured PlaceId
    ReferentPlace?          structured PlaceId
    DependencyRoots[]
    TypeProperties
    AccessCapabilities
    ExplicitCede            bool
    ActualInit              bool
    SourceLocation          diagnostics only
```

Value category is an AST/semantic fact, not `bool(makeAccessPath(expr))`.
Permission-only postfix syntax may preserve a place. Increment/decrement,
casts, address construction, arithmetic, calls, and other value-producing
operations are classified according to their actual semantic result.

`ExprFacts` never contains a transfer decision, but its `TypeId` may be
candidate-relative. Contextual integer/character literals, closures, generic
expressions, and other expected-type-dependent forms are prepared separately
inside each candidate transaction. Only non-contextual syntax/place seed facts
may be shared across candidates. No candidate may publish its contextual type
or AST annotation into the parent before selection and whole-call validation.

## Analysis transaction

Every nontrivial semantic traversal runs in an `AnalysisTransaction`:

```text
AnalysisTransaction
    BaseSnapshot
    WorkingPAL
    WorkingPlaceState
    SemanticJournal
    DiagnosticBuffer
    EvidenceBuffer
    SemanticModelPatch
```

The transaction supports:

- `fork(StructuralForkKey)` from the same immutable snapshot;
- `validate()` after preparation;
- `adopt(child)` into a still-open parent;
- `discard()` with zero parent/global mutation; and
- root-only `commit()` into the published semantic state.

### Lifecycle state machine

Transactions are move-only. Each live transaction has a structurally derived
`TransactionId`, optional `ParentTransactionId`, `BaseEpoch`, current `Epoch`,
and one state:

```text
Open -> Validated -> Adopted       child consumed by its parent
Open -> Validated -> Committed     root publication only
Open -------------> Discarded
Open -> Validated -> Discarded
```

The operations are frozen as follows:

- `fork(StructuralForkKey)` is valid only on an `Open` parent. It creates an
  `Open` child whose parent identity and base epoch equal the parent's current
  identity/epoch. Its transaction identity derives from parent identity plus
  the caller-supplied deterministic candidate/node/role key; it consumes no
  global counter.
- `validate()` is valid only from `Open`. Success moves to `Validated`;
  rejection returns a separate rejected result and the transaction must be
  discarded after extracting its buffered diagnostics/rejection Evidence.
- `adopt(child)` is valid only when the parent is `Open`, the child is
  `Validated`, the child names that exact parent, and the child's `BaseEpoch`
  equals the parent's current `Epoch`. It consumes the child, changes the child
  to `Adopted`, changes no published/global state, and advances the still-open
  parent's epoch exactly once.
- `commit()` is valid only for a parentless root in `Validated`. It changes the
  root to `Committed` and publishes exactly once. A child can never commit.
- `discard()` is valid only from `Open` or `Validated` and moves to
  `Discarded`. It publishes nothing.

Any mutation of a parent's transaction-local working state or adoption of a
different child advances its epoch. A child whose base epoch is stale cannot
be rebased or partially adopted; it fails closed and must be discarded.

`Adopted`, `Committed`, and `Discarded` are terminal. A second adopt, commit,
discard, validate, or journal write returns an internal
`TransactionError::LifecycleViolation`; debug builds additionally assert.
Such violations are compiler failures, never recoverable source diagnostics or
silent no-ops. Destruction of an unconsumed `Open`/`Validated` transaction
performs a fail-safe discard; moved-from handles expose no operations.

### Atomic adoption and publication

`adopt` and root `commit` may validate/prebuild a complete immutable successor
state before their lifecycle transition. Allocation, hashing, remapping, or
other fallible work happens during that prebuild. If it fails, the parent and
published transactional state remain byte-for-byte unchanged, subject only to
the manifest's separately proven pure-cache exception.

The final adoption/publication action is a no-throw immutable-state pointer
swap (or an equivalently no-throw mechanism). It cannot replay a journal into
live state entry by entry. Root publication swaps the complete
`PublishedSemanticState`; no diagnostic, Evidence, semantic-index, model, PAL,
or place-state consumer can observe a prefix.

The journal contains logical actions, not LLVM lowering:

```text
InvalidateWhole(place)
InvalidateProjection(place, cleanup mask)
KeepLive(place)
InstallBorrow(source, referent, capability)
TransferDropLiability(source, destination)
RetainSharedLiability(source, destination)
RecordDependency(destination, roots)
PublishResolvedCall(call id, validated plan)
```

All PAL, place, drop, dependency, diagnostics, evidence, and semantic-model
writes go through the transaction. Direct mutation of parent scopes, global
PAL state, AST resolution caches, evidence singletons, or diagnostic output
from a probe is forbidden.

The only probe-global writes allowed are pure deterministic interning or
memoization entries satisfying the cache rule above.

### TransactionalStateManifest

M1b.0b must implement a source-backed `TransactionalStateManifest`. Every
mutable semantic subsystem registers exactly one ownership class below and a
digest provider. Adding mutable Sema state without a manifest entry is a test
failure.

| State family | Required ownership during prepare/probe |
| --- | --- |
| lexical scopes, symbol definitions, resolved bindings | immutable base snapshot plus transaction-local scope/model patch |
| init masks, moved/used/mutated facts, exact-place facts, payload ceilings | transaction working state plus ordered journal delta |
| PAL borrows/conflicts and `PlaceState` | transaction working state plus ordered journal delta |
| source/referent/dependency roots and lifetime/member dependencies | immutable facts plus transaction-local model/dependency patch |
| capture summaries, effect state, reachability and handle-grammar reachability | explicit returned summary or transaction-local patch; never parent mutation |
| generic/trait instances, substitutions and candidate declarations | candidate-local model patch; publish only through final adoption |
| resolved callees/types, implicit conversions, default arguments and callable receivers | `SemanticModelPatch`/lowering recipe; source AST mutation forbidden |
| synthetic/default arguments and compiler-created nodes | structurally identified transaction-local lowering recipe/model patch |
| diagnostics, notes, warnings and warning-dedup state | immutable dedup snapshot plus transaction-local diagnostic buffer |
| Evidence, todo/conditional facts and semantic index | transaction-local buffers/model patch |
| semantic IDs and source origins | structural deterministic construction; no probe-visible allocator |
| type/declaration interning caches | pure deterministic cache only; otherwise transaction-local patch |
| generic/lookup/layout convenience caches | classified individually as pure or transaction-local; unclassified cache writes forbidden |
| CodeGen state, emitted IR and ABI lowering state | unavailable during Sema transactions |

The manifest digest covers the parent and published forms of every row,
including empty/nonempty buffers, ID/cache state, and warning/evidence dedup.
`discard()` and failed/stale lifecycle operations must preserve that complete
digest. Pure-cache entries may be excluded from byte equality only after a
separate test proves output, identity, and candidate-order invariance with the
cache cold, warm, and differently populated.

### Diagnostics on rejection

Candidate-local diagnostics remain buffered. Rejected overload candidates are
discarded. After resolution either the selected candidate's diagnostics or a
deterministically ranked no-viable-candidate diagnostic is published.

A rejected selected call may publish an immutable `RejectedAnalysisResult`
containing its selected diagnostics and rejection-only Evidence after the
semantic transaction is discarded. That output publication is separate from
`adopt`/`commit` and is atomic as a complete buffer. It commits no ownership,
PAL, place, dependency, drop-liability, or authoritative semantic-model
change. Other error-recovery facts, if needed, live in the same explicitly
non-authoritative result.

### Nested and speculative analysis

- A nested call validates into the current transaction, not global state.
- Closure capture precompute returns an explicit `CaptureSummary`; its
  transaction is discarded and cannot retain `ResolvedFn`, synthetic
  receivers, RootIDs, diagnostics, or evidence.
- Overload and trait candidates each use sibling transactions from the same
  snapshot.
- Generic instance declaration/model entries remain local to the selected
  candidate transaction and reach the parent only when the whole call is
  validated and that transaction is adopted. Pure type interning may be
  shared.
- Branch and loop analysis may fork and merge transaction-local state, but a
  speculative branch never writes its parent directly.

Synthetic callable receivers become a lowering recipe in the validated call,
not an insertion into the source argument vector during preparation.

### Selected-candidate handoff

Candidate selection returns one move-only `SelectedCandidate`:

```text
SelectedCandidate
    CandidateTransaction          original child, still Open
    ResolvedCalleeId
    ResolvedFormalFacts[]
    CandidateRelativeExprFacts[]
    NestedValidatedCalls[]
    GenericAndModelPatch
```

The selected child continues directly into whole-call planning and validation.
Its contextual facts, nested-call effects, generic patch, identities, and
diagnostic buffer are moved neither into a new child nor into the parent at
selection time. Reanalysis and replay are forbidden. Rejected siblings are
discarded. After whole-call success the same child transitions to `Validated`
and is adopted exactly once; on failure it is discarded after diagnostics are
extracted.

Candidate viability/type ranking runs while sibling transactions remain
`Open`; it does not invoke the lifecycle `validate()` transition. Only the
selected child performs whole-call validation and transitions once from
`Open` to `Validated`.

### Evaluation effects versus boundary actions

The candidate transaction maintains two logical lanes:

```text
EvaluationJournal
    effects intrinsic to evaluating each actual in source order
    (nested validated calls, value-producing mutation, temporary/lifetime work)

BoundaryIntents
    actions introduced by the selected formal/receiver boundary
    (move/copy/borrow, source invalidation, dependency handoff, drop liability)
```

Actual expressions are prepared in source order, and their evaluation effects
advance only the candidate transaction's working state. Outer-call formal
boundary intents do not invalidate or borrow their source place while later
actuals are being prepared. Whole-call validation considers all boundary
intents, expression accesses, receiver aliases, and the final evaluation
working state together; conflicts reject the call independent of partial
parent mutation.

A nested call is itself validated and adopted into the candidate transaction
as an evaluation effect, so its already-complete source transition is visible
to later outer arguments. After whole-call validation the ordered journal
contains source-order evaluation effects followed by the admitted outer call
boundary transition. This separation is mandatory; an implementation may not
model an outer formal move by eagerly mutating global or candidate working
place state during argument preparation.

## Whole-call planning

Once resolution selects a declaration and formal list, M1b constructs one
prepared call:

```text
PreparedCall
    CallSiteId
    ResolvedCalleeId
    Route
    ReceiverPlan?
    ArgumentPlan[]          stable source/formal indices
    ExecutionBoundaryKind
    EffectFacts
    NestedValidatedCalls[]
```

Each `ArgumentPlan` combines its `ExprFacts` with the selected formal and
records transfer, source disposition, exact-place admission, dependency,
drop liability, spelling, and init facts.

Preparation follows source evaluation order inside the call transaction.
Validation then considers the entire call together:

- argument/formal compatibility and `init` pairing;
- exact-place eligibility and cleanup masks;
- use-after-move and PAL conflicts;
- overlapping source/referent paths across arguments and receiver;
- capability ceilings and aliases;
- dependency escape, `@Send`/`@Sync`, `.start`, and thread handoff;
- shared retain/release and unique drop liability; and
- callee/effect obligations independent of caller spelling.

The result is either:

```text
ValidatedCall { prepared call, ordered semantic journal }
```

or a rejection with no adopted journal. There is no partially valid argument
set and no rollback-based ownership algorithm.

Atomicity here is a compiler semantic-state guarantee. It does not redefine
runtime argument evaluation or promise runtime rollback.

## SemanticModel handoff

Successful Sema publishes immutable side tables keyed by semantic identity:

```text
SemanticModel
    ExprFactsByNode
    ResolvedCallsByCallSite
    ValidatedTransfersByCallSite
    DeclarationFacts
    TypePropertiesByType
    SourceOrigins
```

AST nodes remain source/lowering structure. They are not the authority for a
resolved formal, transfer decision, PAL transition, or drop liability.
Cloning an AST cannot copy a validated plan accidentally because plans live in
the model under the clone's distinct semantic node identity.

Evidence derives from the same model. It must not rerun source-text or type
classification.

## CodeGen contract

For each migrated call route, CodeGen must retrieve its `ValidatedCall` from
the `SemanticModel` and execute the recorded logical transfer and lowering
recipe. It may choose ABI details, slots, loads, stores, masks, or calling
conventions, but it may not infer ownership from:

- `CedeExpr` presence;
- type names;
- `hasDrop()` alone;
- source display paths;
- receiver syntax; or
- AST argument insertion order.

Migration uses an explicit route-admission table. A migrated ownership-
sensitive route without a validated plan fails closed. An unmigrated route may
temporarily use the named legacy path, but fallback cannot be silent or depend
on whether a plan happens to be present. Activation requires the legacy table
to be empty for the ADR's covered routes.

## Shadow and evidence migration

M1a schema v3 remains frozen as the prototype comparison surface until the new
engine exists. The first transactional Shadow schema will be separately
versioned and emitted from a discarded transaction plus its immutable plan,
not from legacy post-check AST fields.

Transactional Shadow must prove:

- running zero, one, or repeated probes leaves the same parent-state digest;
- probe order cannot change Node/Place/declaration identity;
- normal and Shadow exit status and diagnostics are identical;
- source and source-hidden inputs produce equivalent plans; and
- M1a v3 and the new planner agree wherever M1a has a qualified fact.

Public cede obligation evidence v1 remains unchanged. Evidence v2 is produced
only from committed `ValidatedCall` records after all activation gates pass.

## Implementation sequence

### M1b.0a — opaque identities and empty model

- Add strongly typed, opaque semantic/declaration/substitution/contract/place
  identity value types and the tagged `ResolvedCalleeId`.
- Add an empty immutable `SemanticModel` shell with no allocator, builder,
  patch, transaction, planner consumer, CLI, Sema, Evidence, or CodeGen wiring.
- Provide equality/order/hash/value-type tests and prove distinct identity
  domains cannot be implicitly substituted.
- Keep structural ID construction policy in this design; 0a exposes no global
  counter or probe-visible allocation API.

M1b.0a is the only implementation slice admitted by this D.1 commit.

The `toka_semantic_model_scaffold` CTest target locks strong domain separation,
full-key equality/order/hash behavior, all `ResolvedCalleeId` tags, structured
place projection values, and the zero-field/zero-entry model. The scaffold has
no transaction type or production Sema/Evidence/CodeGen include.

### M1b.0b — transaction lifecycle and synthetic state

- Implement the frozen move-only lifecycle and root-only atomic publication
  against synthetic state, not live Sema.
- Implement a source-backed `TransactionalStateManifest` registry/digest and
  immutable successor-state swap.
- Add no call planner, ownership commit, behavior flag, CLI, Sema, Evidence,
  or CodeGen wiring.
- Require a separate review before implementation and another qualification
  review before M1b.1 may consume the API.

### M1b.1 — transactional probe infrastructure

- Route diagnostics, evidence, PAL/place deltas, and semantic-model patches
  through a transaction for call preparation.
- Move closure precompute and candidate probing to discardable children.
- Do not activate call ownership changes.

### M1b.2 — unified type and exact-place queries

- Replace call-planner Copy/ownership/dependency/drop classification with
  `TypeProperties`.
- Share exact-place admission and cleanup-mask facts with existing qualified
  partial-transfer rules.
- Produce a new transactional Shadow surface with no `PendingValidation` or
  `Unclassified` fact on its admitted direct-call subset.

### M1b.3 — ordinary direct-call prepare/validate

- Plan receiver-free ordinary direct calls as one transaction.
- Prove multi-argument overlap, later-argument failure, temporary, Copy,
  owning, borrowed, raw, init, generic, and source-hidden matrices.
- Keep RC8 caller spelling and CodeGen behavior active.

### M1b.4 — route convergence

- Migrate static, method, callable, indirect `fn`, indirect `dyn fn`, dynamic
  trait, extern, async, `.start`, and thread routes one admitted slice at a
  time.
- Eliminate synthetic-source-argument mutation from callable lowering.

### M1b.5 — commit and CodeGen consumption

- Enable semantic commit only after M1b.0a, M1b.0b, and M1b.1 through M1b.4
  qualification.
- Make CodeGen consume validated plans and fail closed on missing liability
  plans.
- Retire M1a AST vectors after parity receipts.

Evidence v2, the implicit-call-move lint, and the ADR behavior flip remain
later activation work. They are not bundled into the first commit slice.

## M1b.0b synthetic qualification gates

Before any live Sema subsystem can consume the transaction API, synthetic
tests must prove:

1. nonempty child discard leaves parent, root publication, model, IDs, caches,
   diagnostics, Evidence, and manifest digest unchanged;
2. nonempty child adopt transfers its complete patch exactly once;
3. nested validated children adopt bottom-up without exposing an intermediate
   published state;
4. stale child adoption, wrong-parent adoption, child commit, double adopt,
   double commit, double discard, and writes after a terminal state fail
   closed;
5. a diagnostics/Evidence-only rejected result can publish selected
   diagnostics separately while its semantic transaction is discarded;
6. fork/probe/discard order produces identical structural identities and
   final model digest;
7. injected failure during successor-state construction changes no lifecycle
   state or digest; and
8. the final immutable-state swap is exercised as a no-throw publication with
   no observable journal prefix.

An empty transaction test is retained as a smoke test but is not qualification
for M1b.0b.

## Admission gates before any ownership commit

M1b.5 implementation may not begin until one revision proves all of these:

1. Every preparation and candidate path runs inside a transaction.
2. Discard restores an identical digest of PAL, place state, scope ownership,
   dependency facts, diagnostics, evidence, and semantic-model contents.
3. Closure precompute and repeated probes produce no AST or identity mutation.
4. The admitted direct-call subset has complete type, exact-place, dependency,
   boundary, and drop-liability facts; no pending fact grants authority.
5. A conflict in argument N commits none of arguments 1 through N-1.
6. Receiver/argument aliases are validated in one working-state order.
7. Generic candidate rejection and source-hidden replay leave no published
   instance/model residue and produce equivalent selected plans.
8. M1a v3, normal-output parity, cede evidence v1, public JSON, CTest,
   conformance, pass, and fail gates remain green.

## Required fault-injection tests

The implementation must include deterministic failures after each preparation
phase and assert parent-state digests:

- after candidate type resolution;
- after argument 1 and before argument 2;
- after installing a borrow;
- after preparing an invalidation;
- after computing a cleanup mask;
- after dependency/boundary validation;
- after nested-call validation; and
- immediately before commit.

These tests are architectural gates, not debug-only conveniences. They prevent
a future route from reintroducing partial state mutation.

## Rejected alternatives

### Promote `ShadowArgumentTransfers`

Rejected because mutable AST storage and post-check recording cannot provide
candidate isolation, clone safety, or whole-call atomicity.

### Snapshot and restore selected global fields

Rejected because the current state spans scopes, PAL, AST caches, generic
instances, dependencies, diagnostics, evidence, and lowering mutations. A
field list is incomplete by construction and regresses as new state is added.

### Add a global probe boolean

Rejected because every new side effect must remember the flag. Transactions
make the safe behavior structural: unadopted work cannot reach its parent.

### Commit each argument and roll back on failure

Rejected because rollback would need to invert aliases, cleanup masks,
reference counts, dependency facts, diagnostics, AST mutation, and nested
calls. Preparing in a child transaction is simpler and fail-closed.

### Let CodeGen repair or infer missing plans

Rejected because it recreates multiple ownership authorities and cannot
provide source-level PAL or exactly-once drop proof.

## Non-goals

- No new Toka syntax, hat, or runtime ownership operation.
- No widening of current exact partial-move eligibility.
- No physical ABI or LLVM lowering freeze.
- No TKI version change in M1b.0a, M1b.0b, or M1b.1.
- No change to `E04570`, callee `E0474`, explicit return/capture/local
  destructive reads, or consuming callable receiver syntax before activation.
- No claim that transaction infrastructure alone activates the accepted ADR.
