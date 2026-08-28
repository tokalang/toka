# RC9 M1b.2a Authority-First Semantic Facts

**Design status:** Proposed / independent design review required.

**Implementation status:** Not implemented.

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
    FullExpressionId
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
    SymbolID
    declaration identity
    owner identity
    PlaceState
    InitMask
```

The builder cannot accept a raw canonical key, source spelling, temporary
string, caller-supplied owner, or destination type. Globals, place aliases,
dynamic/nested projections, temporaries, missing declarations, and source-hidden
bindings return a closed `NotInSlice` or `Indeterminate` result; they never
receive a fabricated local owner.

M1b.2a does not widen partial-move eligibility.

## Sema-owned source cleanup fact

CodeGen's LLVM `DropMask` is not a Sema authority and is not read or copied.
M1b.2a introduces a source-level fact keyed by the real `PlaceId`:

```text
SourceCleanupFact
    NoCleanup
    ArmedWholePlace(CleanupId, PlaceId, concrete TypeId, InitMask)
    Indeterminate(reason)
```

`CleanupId` is a typed identity created by a controlled builder from the real
place and concrete type identity. It is never `place-string + ":cleanup"`.

`ArmedWholePlace` requires all of:

- whole place is `Live`;
- concrete type identity is complete;
- existing read-only type analysis proves a source-level drop/owned cleanup
  obligation;
- the current exact-place/InitMask fact proves initialized cleanup coverage;
  and
- no unsupported partial/custom-drop ambiguity exists.

Proven trivial/no-drop types yield `NoCleanup`. Missing/cold type analysis,
incomplete identity, zero/ambiguous mask, unsupported custom partial state, or
any disagreement yields `Indeterminate`. No query may populate a type/drop
cache to avoid `Indeterminate`.

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
prepare facts by value
validate complete typed identities and cross-references
capture parent sentinel
publish one immutable revision/receipt
capture parent sentinel
```

The revision contains no mutable AST pointer, Sema/PAL object, CodeGen value,
callback, journal action, or public Evidence record. Full structural equality is
authority; digests are diagnostic accelerators only.

## First qualification matrix

Before implementation acceptance, real fixtures must prove:

1. two sibling calls in one return expression share one `FullExpressionId`;
2. nested calls share their enclosing root identity;
3. separate statements/initializers have distinct full-expression identities;
4. repeated/final traversal and scheduling order do not change identities;
5. one local binding has the same `PlaceId` across different callees;
6. different bindings, owners, and source files have different full identities;
7. global, alias, projection, temporary, source-hidden, and missing-declaration
   cases fail closed without a `PlaceId`;
8. proven owned/drop local yields `ArmedWholePlace` with its real InitMask;
9. proven trivial local yields `NoCleanup`;
10. cold/incomplete/custom-partial cleanup input yields `Indeterminate`;
11. `SlabID`, forced-explicit names, ordinary drop, ordinary no-drop, and
    indeterminate raw policy inputs reproduce the existing RC8 helper result;
12. callers cannot construct or overwrite the derived requirement;
13. equal hash with unequal typed identities/facts remains unequal; and
14. normal diagnostics, PAL/place state, D.3a, D.4a, Evidence v1, public JSON,
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
