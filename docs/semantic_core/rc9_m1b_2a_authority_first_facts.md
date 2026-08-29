# RC9 M1b.2a Authority-First Semantic Facts

**Design status:** Accepted for bounded implementation.

**Implementation status:** M1b.2a implemented / bounded post-implementation
accepted. The implementation is authority-only and activates no transfer or
ownership behavior.

**Predecessor result:** D.5a implementation at `4103afd8` plus corrective
`870d49b4` was rejected and reverted. M1b.2a exists to establish the semantic
authorities that D.5a incorrectly synthesized.

## Decision

M1b.2a adds only immutable, typed, Sema-owned raw facts. It creates no transfer
edge, source disposition, boundary/finalization delta, transaction, model patch,
or CodeGen instruction.

The first authority set is:

```text
AuthorityFacts
    AuthorityFactKey
    PlaceFact?
    SourceCleanupFact
    RawLegacyCedePolicyInput?
```

No field may be a display string, inferred transfer result, or copy of an LLVM
drop mask.

## Enclosing `FullExpressionId`

Sema owns a typed full-expression context established before recursively
checking its child expression. The first slice admits only source-backed:

- local variable initializers;
- return expressions; and
- expression statements.

The context root is the enclosing AST expression owned by that statement, not
an inner call. Every nested or sibling call within one root observes the same
`FullExpressionId`; different roots observe different identities.

Identity is constructed from the already-existing source AST root and its
structural source/declaration coordinate through a controlled builder. Probe
order, mutable counters, call identity, display text, AST clones, and CodeGen
temporary registration cannot establish it.

Calls reached during candidate probing, generic deduction, closure capture
precompute, or another non-final traversal may read an existing context but
cannot allocate or publish a replacement identity.

`FullExpressionId` identifies the shared enclosing root, not an observation
within it. M1b.2a therefore also assigns a typed observation identity to every
final-traversal AST observation point:

```text
AuthorityObservationId = controlled identity of
    (FullExpressionId, observing source AST node identity)

SnapshotPhase
    PreEvaluation

AuthorityFactKey
    (FullExpressionId, AuthorityObservationId, SnapshotPhase)
```

`PreEvaluation` is the only phase in this slice. Two sibling or nested calls
share a `FullExpressionId` but have distinct `AuthorityObservationId` values;
revisiting the same source AST node in a non-final traversal cannot allocate a
second identity or publish a record.

Each observation snapshots facts before that observation evaluates. Therefore
`consume(cede resource) + inspect(resource)` may have two records for the same
place: the first can observe `Live`, while the later observation can observe
the legacy-updated state. Cleanup facts are keyed by
`(AuthorityObservationId, PlaceId)`, never by `PlaceId` alone.

## Exact whole-place `PlaceId`

The first slice admits only a whole function-local binding that has:

- a real `SymbolInfo::SymbolID`;
- a source declaration AST and location;
- an owning function/declaration identity; and
- an existing `ExactPlaceFacts` whole-place fact.

A controlled builder produces:

```text
PlaceFact
    PlaceId(RootSymbolId, no projections)
    SymbolLookupWitness(SymbolID, revision-local only)
    typed declaration identity
    typed owner identity
    PlaceState
    InitMask
```

`PlaceId` equality and hash authority use only the typed owner and declaration
identity (plus typed projections in later slices). Numeric `SymbolID` is not a
stable identity component; it is only a lookup witness inside the current
revision.

Before publication, the builder must reverse-resolve that witness and prove all
of the following still describe one binding:

- the same live `SymbolInfo` object in the expected lexical scope;
- the same source declaration AST;
- the same typed owning function/declaration;
- the same `ExactPlaceFacts` object and whole-place state; and
- the declaration/owner identities embedded in the candidate `PlaceId`.

Any mismatch is fail-closed and publishes no fact. Closure captures, generated
bindings, generic/AST clones, and capture-precompute copies are explicitly
`NotInSlice` in M1b.2a; copying an outer AST pointer or ExactPlace state into a
new SymbolInfo cannot grant the outer binding's `PlaceId`.

The builder cannot accept a raw canonical key, source spelling, temporary
string, caller-supplied owner, or destination type. Globals, place aliases,
dynamic/nested projections, temporaries, missing declarations, and source-hidden
bindings return a closed `NotInSlice` or `Indeterminate` result; they never
receive a fabricated local owner.

M1b.2a does not widen partial-move eligibility.

## Sema-owned source cleanup fact

CodeGen's LLVM `DropMask` is not a Sema authority and is not read or copied.
Before building any place fact, Sema freezes one immutable store keyed by typed
concrete `TypeId`:

```text
CleanupClassStore[TypeId] =
    OwnedWholeCleanup
    NoCleanup
    Indeterminate(CleanupClassIndeterminateReason)

CleanupClassIndeterminateReason
    MissingConcreteTypeGraph
    ColdAnalysis
    GenericOrSourceHidden
    RecursiveCycle
    ConflictingDropFacts
    UnsupportedCarrier
```

This store is the sole cleanup-class authority. `valueOwnership()`, `hasDrop()`,
legacy cede exemptions, and call-site recursion are not alternative authorities
and cannot override it. The store is complete before admitted expression
checking begins; a miss returns `Indeterminate` and never triggers analysis.

The precedence is frozen:

1. borrowed views (`str`, `bytes`, `cstr`, view iterators, references/slices)
   are `NoCleanup`;
2. compiler-recognized owned buffers (`string`, `Bytes`) are
   `OwnedWholeCleanup`, even though they are intentionally absent from
   `hasDrop()`;
3. a source-declared explicit `@Encap drop` is `OwnedWholeCleanup`, including
   `SlabID`, regardless of legacy `valueOwnership=Trivial` or cede exemption;
4. a fully analyzed structural type containing an owned-cleanup field is
   `OwnedWholeCleanup`, including `TimerHeap` through its `Vec` field;
5. a fully analyzed concrete type proven to contain no owned cleanup is
   `NoCleanup`; and
6. generic/incomplete/source-hidden/cyclic/conflicting or cold analysis is
   `Indeterminate`.

Raw/reference/shared/unique identities and carrier types outside the first
shape-only authority slice return a closed unsupported/indeterminate result;
M1b.2a does not silently classify them through legacy fallbacks.

The store builder receives resolved declaration/type graph facts from the
normal earlier type-analysis phase. It cannot call `resolveType()`, `hasDrop()`,
`valueOwnership()`, or recursively analyze a type while servicing an
observation.

M1b.2a then introduces a source-level fact keyed by the real observation and
place:

```text
SourceCleanupFact[(AuthorityObservationId, PlaceId)] =
    NoCleanup
    ArmedWholePlace(CleanupId, PlaceId, concrete TypeId, InitMask)
    Indeterminate(reason)
```

`CleanupId` is a typed identity created by a controlled builder from the real
place and concrete type identity. It is never `place-string + ":cleanup"`.

`ArmedWholePlace` requires all of:

- whole place is `Live`;
- concrete type identity is complete;
- `CleanupClassStore[TypeId] == OwnedWholeCleanup`;
- the current exact-place/InitMask fact proves initialized cleanup coverage;
  and
- no unsupported partial/custom-drop ambiguity exists.

`CleanupClassStore[TypeId] == NoCleanup` yields `NoCleanup`. A missing or
indeterminate class, incomplete identity, zero/ambiguous mask, unsupported
custom partial state, or any disagreement yields `Indeterminate`. No query may
populate a type/drop cache to avoid `Indeterminate`.

This fact records source liability only. It does not transfer, disarm, retain,
or lower that liability.

## Private raw legacy-policy input

M1b.2a records only raw input:

```text
RawLegacyCedePolicyInput
    concrete TypeId
    canonical soul identity
    type category
    frozen drop fact = HasDrop | NoDrop | Indeterminate
```

It exposes no writable `LegacyCedeRequirement`. A single pure classifier owns
the closed RC8 named-policy table and derives:

```text
ExplicitRequired | ImplicitExempt | Indeterminate
```

The existing legacy helper and any later Shadow consumer must call that same
classifier. Integration cannot inject a requirement, named exception, or
fallback result. The classifier cannot resolve a type, call `hasDrop()`, inspect
CodeGen, or populate a cache.

The derived requirement remains an RC8 compatibility fact only; it grants no
future transfer authority.

## Immutable publication boundary

Facts are published together in one command-local, immutable
`AuthorityFactsRevision`. Publication is observational and unavailable when the
internal qualification mode is disabled.

```text
A0 = capture complete semantic parent sentinel
build + validate all facts by value
A1 = capture sentinel; require A0 == A1 field-by-field

B0 = capture sentinel
publish one immutable revision/receipt
B1 = capture sentinel; require B0 == B1 field-by-field
```

A0/A1 includes type/cleanup store sizes and contents, identity allocators,
Scope/SymbolInfo/ExactPlace/PAL state, diagnostics, Evidence, AST elaboration,
generic/shape caches, and current full-expression context. It therefore detects
fact builders that populate a cache or rewrite an AST.

B0/B1 excludes only the command-local immutable revision slot intentionally
receiving the new revision; every semantic parent field remains identical.
Revision construction must complete before B0. Publication is one no-throw
immutable-state swap and cannot expose a prefix.

The revision contains no mutable AST pointer, Sema/PAL object, CodeGen value,
callback, journal action, or public Evidence record. Full structural equality is
authority; digests are diagnostic accelerators only.

## Closed results and revision protocol

The build result is a closed sum:

```text
AuthorityBuildResult
    Admitted(AuthorityFactRecord)
    NotInSlice(AuthorityExclusionReason)
    Indeterminate(AuthorityIndeterminateReason)
    Error(AuthorityBuildError)

AuthorityExclusionReason
    UnsupportedFullExpressionRoot
    NonFinalOrSpeculativeTraversal
    GlobalOrSourceHiddenBinding
    PlaceAliasOrProjection
    TemporaryOrMissingBinding
    CapturedOrGeneratedBinding
    GenericOrASTClone
    UnsupportedTypeCategory
    UnsupportedPartialState

AuthorityIndeterminateReason
    MissingConcreteTypeId
    MissingCleanupClass
    CleanupClassColdOrIncomplete
    CleanupClassConflict
    MissingLegacyDropFact
    ZeroOrAmbiguousInitMask
    PlaceNotLive
    PlaceNotLive
    IncompleteOwnerOrDeclarationIdentity

AuthorityBuildError
    InvalidFullExpressionIdentity
    InvalidObservationIdentity
    InvalidPlaceIdentity
    StaleSymbolLookupWitness
    OwnerDeclarationMismatch
    ExactPlaceMismatch
    DuplicateFactKey
    ConflictingPayload
    DanglingCrossReference
    MalformedRevision
```

The internal qualification schema is frozen before implementation:

```text
schema = toka.internal.m1b-2a-authority-facts
version = 1
revision_id
record_count
excluded_count_by_reason
indeterminate_count_by_reason
error_count_by_reason
records[]
    key = (FullExpressionId, AuthorityObservationId, PreEvaluation)
    full-expression root identity
    observation AST identity
    PlaceFact?
    SourceCleanupFact
    CleanupClassStore result for the record's TypeId
    RawLegacyCedePolicyInput?
```

There is at most one record per complete `AuthorityFactKey` and at most one
whole-place fact/cleanup fact in each first-slice record. Same key plus identical
payload coalesces idempotently; same key plus different payload rejects the
entire revision. Equal hashes with unequal complete keys coexist.

Every `CleanupId` references the `PlaceId` and `TypeId` carried by the same
record; every cleanup-map key references that record's observation and place.
Missing or foreign references reject the revision. Child/nested/repeated
builders cannot publish independently; the command publishes one validated
revision only.

## First qualification matrix

Before implementation acceptance, real fixtures must prove:

1. two sibling calls in one return expression share one `FullExpressionId`,
   have distinct observation IDs, and preserve source-order pre-evaluation
   snapshots;
2. `consume(cede resource) + inspect(resource)` records the same `PlaceId` at
   two observations while allowing the later observation to see the legacy
   state change;
3. nested calls share their enclosing root identity but have distinct typed
   observation IDs;
4. separate statements/initializers have distinct full-expression identities;
5. repeated/final traversal and scheduling order do not change identities;
6. one local binding has the same stable `PlaceId` across different callees and
   across runs with different revision-local numeric SymbolIDs;
7. different bindings, owners, and source files have different full identities;
8. reverse lookup proves SymbolInfo/declaration/owner/ExactPlace agreement;
   stale witnesses and owner/declaration substitutions reject atomically;
9. global, alias, projection, temporary, source-hidden, missing-declaration,
   captured/generated, generic clone, and AST clone cases fail closed without a
   `PlaceId`;
10. explicit-drop `SlabID`, built-in `string`/`Bytes`, and structural
    `TimerHeap` classify `OwnedWholeCleanup`; borrowed views and a proven plain
    POD classify `NoCleanup`;
11. a proven owned/drop local yields `ArmedWholePlace` with its real InitMask;
12. cold/incomplete/cyclic/conflicting/custom-partial cleanup input yields the
    exact `Indeterminate` reason;
13. `SlabID`, forced-explicit names, ordinary drop, ordinary no-drop, and
    indeterminate raw policy inputs reproduce the existing RC8 helper result;
14. callers cannot construct or overwrite the derived requirement;
15. duplicate/collision/foreign-reference fixtures enforce the frozen revision
    rules, including equal-hash unequal-key coexistence;
16. A0/A1 covers every builder/failure path and B0/B1 covers publication;
17. finite fault points after full-expression identity, observation identity,
    place reverse lookup, cleanup-class lookup, cleanup-fact construction,
    revision validation, and immediately before swap leave parent state and the
    prior revision byte-identical; and
18. normal diagnostics, PAL/place state, D.3a, D.4a, Evidence v1, public JSON,
    CodeGen output, and exit status remain unchanged.

Every exclusion/error is closed and has a real source or pure-value fixture.
Qualification receipts serialize complete typed facts; they do not reconstruct
authority from strings.

## Non-authorization

M1b.2a does not authorize:

- `PreparedCall`, transfer edges, source disposition, boundary/finalization
  delta, branch join, transaction/adopt/commit, or SemanticModel patch;
- ownership or PAL mutation, cleanup transfer/disarm, or partial-move expansion;
- CodeGen consumption or access to LLVM drop masks/temporary registries;
- Evidence v2, lint/LSP behavior, TKI changes, `E04570` removal, or
  signature-driven caller-spelling activation; or
- D.5a reimplementation before M1b.2a independently passes design and
  post-implementation acceptance.
