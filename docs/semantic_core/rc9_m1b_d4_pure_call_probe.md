# RC9 M1b-D.4 Pure Ordinary Overload Probe

**Design status:** Proposed for independent review.

**Implementation status:** Not implemented. M1b.1a is not authorized until
this contract passes review.

**Qualified baseline:** D.3a ordinary direct-call Shadow observation at
`0235a35cd3f60f4be0ab225dfae7abe5a6eea4fb`.

## Decision

The first M1b.1 slice does not need a transaction child.

For its deliberately narrow input, candidate selection depends only on facts
already established before the call:

- the resolver-owned overload set;
- each selected declaration and resolved formal type; and
- one concrete whole-local binding type.

Running `checkExpr()` against the source AST during candidate selection is
therefore unnecessary. It is also the cause of speculative AST, Scope, PAL,
diagnostic, Evidence, cache, and node-serial mutation.

M1b.1a replaces that live candidate traversal with one pure value query. It
introduces no AST clone, probe-local node identity, Scope/PAL copy, diagnostic
sink, rollback, child lifecycle, journal, adopt, commit, or model publication.

Contextual expressions that genuinely require semantic evaluation remain for
a later reviewed transaction slice.

## Scope

The pure probe admits only:

- a source-backed ordinary direct call with at least two resolver-selected
  overload candidates;
- non-generic, non-variadic, synchronous candidates;
- exactly one explicit, non-default formal on every candidate and one actual;
- no `cede`, `init`, writable/rebindable, raw/reference/managed-handle, outcome,
  execution-boundary, or return-dependency contract;
- one bare whole function-local place actual;
- a concrete actual type already established on its `SymbolInfo`;
- concrete formal types already established before the probe; and
- actual and formal types in the exact-identity domain: plain primitives or
  resolved nominal values with no alias/coercion/structural conversion; and
- no active moved/uninit state, PAL conflict, place alias, dependency escape,
  or capability mismatch.

Literals, temporaries, projections, closures, nested calls, explicit `cede`,
permission-bearing formals/actuals, generic calls, methods, callables, externs,
async calls, `.start`, thread handoff, globals, captures, source-hidden
declarations, weak/strong aliases, anonymous records, function/dyn coercions,
`never`, and incomplete types are not admitted.

This scope is intentionally smaller than D.3a. Its purpose is to remove one
unnecessary speculative traversal, not to plan ownership.

## Immutable input

Integration constructs this value without checking or cloning the actual:

```text
PureOverloadProbeInput
    CallSiteId
    LegacyFallbackDeclarationId
    Actual
        RootSymbolId
        TypeIdentity
        PlaceState = Live
        IsWholeFunctionLocal = true
        HasPALConflict = false
        HasDependencies = false
        PlainCapabilities = true
    Candidates[]
        DeclarationId
        LegacyOrdinal
        FormalTypeIdentity
        ArityAndContractFacts
```

Every identity is built through the controlled structural identity builders.
Candidate input order is the current resolver's legacy order. A digest is not
identity and has no selection authority.

Input construction returns:

```text
Expected<PureOverloadProbeInput, ProbeInputError>
```

`ProbeInputError` is closed:

```text
InvalidCallSiteIdentity
InvalidFallbackIdentity
InvalidCandidateSet
NonConcreteTypeIdentity
```

An ordinary unsupported shape is `NotInSlice` before the pure factory is
called. A malformed supposedly admitted fact is `ProbeInputError` and fails
closed; it may not silently enter the pure path.

## Pure result

The independent pure translation unit returns:

```text
Expected<OverloadProbeBatchResult, ProbeInfrastructureError>

OverloadProbeBatchResult
    Disposition
        UniqueCompatible(DeclarationId)
        LegacyRequired(LegacyReason)
    LegacyFallbackDeclarationId
    Candidates[]
        DeclarationId
        LegacyOrdinal
        Compatible
```

`LegacyReason` is closed:

```text
ZeroCompatible
MultipleCompatible
```

`ProbeInfrastructureError` is closed:

```text
InvalidIdentity
DuplicateCandidateIdentity
DuplicateLegacyOrdinal
MalformedBatch
```

The factory contains no Sema, AST, Scope, PAL, diagnostics, Evidence, cache,
environment, CLI, callback, or global/static mutable state.

## Compatibility rule

For every admitted candidate, `Compatible` is exactly:

```text
formal arity is one
AND actual/formal identities are concrete
AND FormalTypeIdentity == ActualTypeIdentity
```

All other compatibility dimensions are excluded by admission rather than
reimplemented in the factory. The admitted exact-identity domain must prove
that identity equality is equivalent to the current legacy
`isTypeCompatible(formal, actual)` result. Unknown types cannot mean
compatible; they are an input error or `NotInSlice` before factory invocation.

The factory evaluates every candidate once in legacy ordinal order. Exactly
one compatible candidate yields `UniqueCompatible`. Zero or multiple yield
`LegacyRequired`; no fallback declaration is selected by the pure factory.

## Integration behavior

For `UniqueCompatible`:

1. Select the returned declaration.
2. Run the existing final legacy call check exactly once on the source actual.
3. Let that final traversal remain the only producer of diagnostics, Evidence
   v1, PAL/place changes, D.3a observation, and CodeGen input.

For `LegacyRequired`:

1. Increment a command-local, qualification-visible fallback counter with the
   exact closed reason.
2. Run the current named legacy overload-probe path unchanged.

Fallback is therefore explicit and semantically classified, not silent. It is
allowed only for a well-formed but unselected result. A
`ProbeInfrastructureError` never falls back to a partial or live candidate set;
it fails compilation with one internal compiler diagnostic and publishes no
candidate result.

Because the admitted pure path is statically limited to diagnostic-free
candidate checks, removing those checks changes no public diagnostic bytes.
The final legacy check still reports any actual call error once. Fixtures must
prove this premise; if a candidate diagnostic is observed for an admitted
shape, admission is wrong and the implementation fails qualification.

## Parent preservation

The pure path reads the source call, actual, Scope, PAL, diagnostics, Evidence,
and type facts through const queries only. Before and after each batch,
qualification structurally compares:

- source call/actual AST and global `ASTNode::NextNodeSerial`;
- the visible Scope chain and exact source binding;
- parent PAL and transient loans;
- diagnostics and complete Evidence buffers;
- D.3a considered/factory/envelope state;
- semantic identities/builders; and
- relevant type/declaration/cache inventories.

There is no state to discard or restore. Probe order is tested in original,
reverse-input, and repeated-batch executions; the selected declaration and all
parent fields must remain identical.

## Qualification protocol

One BUILD_TESTING/internal receipt records:

```text
schema = toka.internal.m1b-d4-pure-overload-probe
version = 1
attempted_batch_count
pure_batch_count
legacy_required_count_by_reason
infrastructure_error_count
batches[]
    call site
    candidate declaration identities + legacy ordinals
    compatible bits
    disposition
    selected declaration identity?
    parent comparison fields
```

It is command-local, emits no public Evidence, and is mutually exclusive with
other JSON/evaluation modes. The factory returns records by value; the audit
driver appends them only after parent comparison.

## Qualification matrix

M1b.1a requires real compiler fixtures for:

- two plain-value candidates with exactly one compatible local type;
- primitive and nominal exact-identity parity against the legacy compatibility
  query;
- three candidates and reversed declaration order with the same selected
  declaration;
- repeated pure batches with stable identity and output;
- zero-compatible and multiple-compatible classified legacy fallback;
- each syntactic/type/permission/lifetime exclusion with pure factory count
  zero, including aliases, anonymous records, coercions, and incomplete types;
- infrastructure-error fail-closed behavior with no live fallback;
- global node serial, source AST, Scope/PAL, diagnostics, Evidence v1, D.3a,
  exit status, and ordinary output parity; and
- proof that the pure TU has no forbidden compiler or mutable-global
  dependency.

The baseline overload fixture must additionally prove that legacy candidate
checks are diagnostic-free for the admitted shape. This is an admission theorem,
not an assumption hidden in implementation.

## Exit criteria

M1b.1a is complete only when:

- the admitted overload path performs no live candidate `checkExpr()` call;
- one unique compatible candidate is selected from immutable facts;
- unsupported/zero/multiple cases use an explicit classified legacy fallback;
- infrastructure failure fails closed without fallback;
- the final source actual is checked exactly once;
- all parent-preservation and parity gates pass; and
- the implementation receives independent post-implementation acceptance.

## Non-authorization

This contract does not authorize a transaction kernel, AST probe cloning,
generic deduction isolation, closure precompute migration, contextual literal
or temporary probing, ownership planning/adopt/commit, SemanticModel
publication, CodeGen consumption, Evidence v2, route convergence, `E04570`
removal, or caller-spelling activation.

A later slice may introduce a discardable child only when an admitted
contextual expression cannot be decided from immutable facts. That design must
solve node identity, complete Sema state ownership, and diagnostic policy at
that time rather than prepaying those costs here.
