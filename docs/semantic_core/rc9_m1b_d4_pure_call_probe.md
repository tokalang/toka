# RC9 M1b-D.4a Pure Direct-Nominal Overload Probe

**Design status:** Accepted for D.4a implementation / bounded scope frozen.

**Implementation status:** D.4a implemented / bounded post-implementation
accepted at `a38a6f55e606f76b903fd9debeaeea9617e611d2`.

This acceptance closes D.4a only. It does not authorize M1b.1 or expand any
scope listed under Non-authorization.

**Qualified baseline:** D.3a ordinary direct-call Shadow observation at
`0235a35cd3f60f4be0ab225dfae7abe5a6eea4fb`.

## Decision

D.4a replaces one unnecessary live overload-candidate traversal with an atomic
pure query.

The admitted actual and every admitted formal are already-resolved direct
nominal shapes. Their compatibility authority is `NominalShapeId`. The pure
factory performs only full `NominalShapeId` equality and unique-match
classification.

D.4a introduces no AST clone, primitive classifier, alias resolution,
conversion, contextual typing, transaction child, Scope/PAL copy, diagnostic
sink, rollback, journal, adopt, commit, or model publication.

## Closed admission

Every condition below is required:

- source-backed ordinary direct call;
- resolver-owned overload vector containing at least two declarations;
- exactly one actual and exactly one explicit formal on every candidate;
- actual is a bare whole function-local place in `Live` state;
- actual type is a direct resolved nominal shape with a valid
  `NominalShapeId` and empty `ShapeType::VariantSuffix`;
- every formal type is a direct resolved nominal shape with a valid
  `NominalShapeId` and empty `ShapeType::VariantSuffix`;
- no primitive, alias, anonymous/structural type, attribute normalization,
  implicit conversion, generic parameter/instance, handle morphology,
  contextual typing, or incomplete type;
- inferred constructor bindings retain and validate their original source
  callee/shape spelling; a resolved underlying shape cannot erase weak-alias
  provenance;
- no `cede`, `init`, default, variadic, writable/rebindable, outcome,
  execution-boundary, or return-dependency contract;
- no active PAL conflict, moved/uninit state, place alias, dependency escape,
  or capability mismatch;
- candidate declaration identities are unique; and
- legacy ordinals are unique, contiguous `[0, candidate_count)`, and reproduce
  the resolver vector order exactly.

Any failed condition is excluded before pure factory invocation.

## Closed exclusion result

Integration returns one closed reason for every excluded attempt:

```text
ProbeExclusionReason
    WrongRoute
    NonSourceBacked
    NotOverloaded
    ArityOrDefault
    NonLocalOrNonLivePlace
    NonDirectNominalActual
    NonDirectNominalFormal
    VariantOrRefinedNominal
    PrimitiveOrAlias
    AttributesOrConversion
    GenericOrContextual
    HandleOrPermission
    ContractUnsupported
    PALOrDependencyConflict
    SourceHiddenOrIncomplete
```

Every enumerator requires a real fixture and an exhaustive switch without
`default`. Exclusion produces no pure batch and no candidate result.

## Frozen integration batch

Integration first freezes one resolver-owned value:

```text
FrozenOverloadBatch
    CallSiteId
    Candidates[]                    resolver vector order
        DeclarationId
        FunctionDecl*               integration-only read-only mapping
        LegacyOrdinal
        DirectNominalFormalFact
            NominalShapeId
            HasEmptyVariantSuffix = true
    LegacyFallback
        FunctionDecl*?
        DeclarationId?
    Actual
        RootSymbolId
        DirectNominalActualFact
            NominalShapeId
            HasEmptyVariantSuffix = true
```

`FunctionDecl*` never enters the pure DTO. It exists only in this frozen
integration batch so a returned declaration identity can be mapped back to the
same resolver vector.

Before factory invocation, integration validates:

- every candidate ID maps to exactly one element of this vector;
- every ordinal equals its vector index;
- no ID or ordinal is duplicated;
- fallback is absent or maps to exactly one element of this same vector; and
- call, actual, formal, and fallback identities are structurally valid; and
- actual and every formal retain an empty `VariantSuffix` at the same frozen
  integration point.

An invalid frozen batch is an infrastructure error, not an exclusion and not a
legacy fallback.

## Pure input and result

The independent pure translation unit receives no fallback:

```text
PureNominalProbeInput
    CallSiteId
    ActualNominalShapeId
    Candidates[]
        DeclarationId
        LegacyOrdinal
        FormalNominalShapeId
```

It returns:

```text
Expected<NominalProbeBatchResult, ProbeInfrastructureError>

NominalProbeBatchResult
    Disposition
        UniqueCompatible(DeclarationId)
        LegacyRequired(LegacyReason)
    Candidates[]
        DeclarationId
        LegacyOrdinal
        Compatible
```

`Compatible` is exactly:

```text
ActualNominalShapeId == FormalNominalShapeId
```

Equality is complete structural `NominalShapeId` equality. Hashes may optimize
lookup but never establish equality. Equal hash with unequal complete identity
remains incompatible.

Exactly one compatible candidate yields `UniqueCompatible`. Zero or multiple
yield:

```text
LegacyReason
    ZeroCompatible
    MultipleCompatible
```

The pure factory does not inspect or select the integration fallback.

`ProbeInfrastructureError` is closed:

```text
InvalidCallSiteIdentity
InvalidNominalShapeId
DuplicateCandidateIdentity
DuplicateLegacyOrdinal
NonContiguousLegacyOrdinal
MalformedBatch
```

Infrastructure error fails closed: no legacy fallback, one internal compiler
diagnostic, nonzero exit, and no parent-state change.
The integration marks the current call resolution terminal before emitting that
diagnostic; every caller of the overload probe must return `unknown`
immediately, before lexical fallback or final argument checking.

The internal qualification CLI may inject one closed infrastructure error only
after a real admitted source call reaches the corresponding frozen-batch or
pure-factory boundary. Injection cannot manufacture an exclusion or bypass
admission. Each injected run records a failed batch whose parent comparison is
captured before mapping, identity construction, and factory invocation, and is
completed before emitting the required internal diagnostic.

## Integration behavior

For `UniqueCompatible(id)`:

1. Resolve `id` only through the same frozen integration vector.
2. Select that declaration.
3. Run the existing final source legacy check exactly once.
4. Let that final check remain the only producer of diagnostics, Evidence v1,
   PAL/place changes, D.3a observation, and CodeGen input.

For `LegacyRequired(reason)`:

1. Record the exact closed reason in command-local qualification state.
2. Invoke the named current legacy overload path with the unchanged resolver
   vector and integration-owned fallback.

The pure result cannot replace, manufacture, or import a fallback from another
batch.

## Required equivalence theorem

For the closed admitted input only, qualification must prove:

```text
NominalShapeId(actual) == NominalShapeId(formal)
iff
legacy isTypeCompatible(formal, actual)
```

Both sides of this theorem require empty actual/formal `VariantSuffix`.
Suffix-bearing or otherwise refined nominal views are excluded before pure DTO
construction; suffix is not added to `NominalShapeId`.

The theorem is tested with direct nominal declarations from the same module,
different modules, same spelling/different nominal owner, and equal nominal
identity reached through the resolver. A counterexample inside this exact
domain blocks D.4a.

Primitive widening, aliases, coercions, anonymous records, attributes,
generics, handles, and contextual typing are outside the theorem and outside
D.4a.

## Parent preservation

Input construction and the pure factory use const queries only. Before and
after each attempted batch, qualification structurally compares:

- source call/actual AST and `ASTNode::NextNodeSerial`;
- visible Scope chain and exact source binding;
- PAL and transient loans;
- diagnostics and complete Evidence buffers;
- D.3a considered/factory/envelope state;
- semantic identities/builders; and
- relevant resolver/type/declaration/cache inventories.

There is no child state to discard or restore.

## Qualification protocol

The BUILD_TESTING/internal protocol is exact and separately versioned:

```text
schema = toka.internal.m1b-d4a-pure-nominal-overload-probe
version = 1
evaluation_schedule = legacy-order | reverse-for-testing
attempted_batch_count
pure_batch_count
excluded_count_by_reason
legacy_required_count_by_reason
infrastructure_error_count
infrastructure_error_count_by_reason
batches[]
    call site
    candidate declaration IDs + complete NominalShapeIds + legacy ordinals
    compatible bits
    disposition
    selected declaration ID?
    forced_legacy_selected_declaration_id?
    candidate_diagnostic_count
    final_legacy_check_count
    parent comparison fields
```

All exclusion, legacy-reason, and infrastructure-error enums have
exact schema values, exhaustive switches, and at least one fixture.

## Pure-versus-forced-legacy gate

Under `BUILD_TESTING`, every pure-admitted fixture runs two isolated compiler
executions over the same source and same frozen candidate identities:

- pure selection path; and
- forced current legacy candidate path.

The comparison requires:

- identical selected declaration identity;
- candidate diagnostics count is zero on the admitted legacy path;
- final source legacy check count is exactly one on both paths;
- identical final diagnostics, stderr, exit status, Evidence v1, D.3a final
  receipt, and ordinary output; and
- identical parent-state structural fields.

Scheduling-order tests reverse only the evaluation schedule of an immutable
copy of the same candidate vector. They do not reorder source declarations or
change declaration/ordinal identities.

## Qualification matrix

D.4a requires real fixtures for:

- two direct nominal candidates with exactly one compatible local actual;
- three direct nominal candidates with one compatible actual;
- same spelling with different nominal owner;
- the same nominal declaration with empty versus nonempty `VariantSuffix`,
  producing `VariantOrRefinedNominal` and pure factory count zero;
- zero-compatible and multiple-compatible legacy fallback;
- original, reverse-schedule, and repeated pure evaluation;
- every closed exclusion reason with pure factory count zero;
- duplicate/missing/foreign candidate and fallback mappings;
- full-key identity/hash-collision behavior;
- forced-legacy equivalence and exact final-check counts; and
- each closed infrastructure error with fail-closed parent preservation.

The pure translation unit must have no Sema, AST, Scope, PAL, diagnostic,
Evidence, cache, environment, CLI, callback, CodeGen, SemanticModel
publication, or mutable-global dependency.

## Exit criteria

D.4a is complete only when:

- the admitted nominal overload path performs no live candidate `checkExpr()`;
- pure and forced-legacy paths select the same declaration;
- unique/zero/multiple classification and same-vector mapping are exact;
- final source legacy checking occurs exactly once;
- closed exclusion/error gates are exhaustive;
- all parent-preservation and public parity gates pass; and
- implementation receives bounded post-implementation acceptance.

## Bounded post-implementation review rule

The post-implementation review may block D.4a only with a reproducible
counterexample that
satisfies every closed admission condition above and demonstrates one of:

1. nominal identity equality differs from legacy nominal compatibility;
2. frozen candidate/fallback mapping is incorrect;
3. unique/zero/multiple classification is incorrect;
4. the pure query changes parent state;
5. pure and forced-legacy selection, diagnostics, or final-check count differ;
6. a closed exclusion/infrastructure error gate is not exhaustive.

Primitive widening, aliases, attributes, conversions, contextual typing,
transactions, diagnostic replay, generics, other routes, and ownership
activation are explicitly out of scope and cannot reject D.4a.

If the bounded review produces no such counterexample, D.4a receives
post-implementation acceptance. New scope requires a later design slice rather
than reopening this contract.

## Non-authorization

D.4a does not authorize suffix-bearing/refined nominal, primitive, or alias
overload queries, contextual evaluation, transaction infrastructure, generic
deduction isolation, closure precompute migration, ownership
planning/adopt/commit, SemanticModel publication, CodeGen consumption,
Evidence v2, route convergence, `E04570` removal, or caller-spelling
activation.
