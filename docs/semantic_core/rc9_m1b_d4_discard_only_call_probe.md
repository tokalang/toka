# RC9 M1b-D.4 Discard-Only Ordinary Call Probe

**Design status:** Proposed for independent review.

**Implementation status:** Not implemented. M1b.1a is not authorized until
this contract passes review.

**Qualified baseline:** D.3a ordinary direct-call Shadow observation at
`0235a35cd3f60f4be0ab225dfae7abe5a6eea4fb`.

## Objective

M1b.1a isolates one real speculative path: overload candidate argument checks
performed by `functionAcceptsCall()` for ordinary direct calls.

Today those checks run `checkArgumentWithHandleCapture()` against the live AST,
Scope, PAL, diagnostics, and Sema context, then restore only selected flow
fields. The first transactional slice replaces that partial restoration for a
strictly admitted candidate shape. It does not introduce a standalone
transaction kernel.

The only escaping value is:

```text
CandidateProbeResult
    CandidateDeclarationId
    CandidateOrdinal
    Compatible              bool
    ActualTypeIdentity
    InternalDiagnosticCodes[]
```

No transfer edge, state patch, diagnostic, Evidence record, identity
allocation, or elaborated AST node escapes the child.

## Scope

The first slice admits only:

- source-backed ordinary direct calls whose resolver has an overload set with
  at least two candidates;
- non-generic, non-variadic, synchronous candidates;
- exactly one explicit formal and one actual;
- no default, `init`, outcome, execution-boundary, or return-dependency
  contract;
- a bare or explicit-`cede` whole local place actual;
- a concrete type already established on the local binding; and
- a resolved concrete formal type requiring no deduction, instantiation,
  closure typing, or source-hidden reconstruction.

Literals, temporaries, projections, closures, nested calls, generic calls,
methods, callables, externs, async calls, `.start`, thread handoff, and
source-hidden candidates remain on the named legacy probe path and are not
evidence that M1b.1a is complete.

## Discard-only child

The implementation introduces one private, move-only child:

```text
OrdinaryCallProbeChild
    CandidateDeclarationId
    ClonedActual
    WorkingScopeChain
    WorkingPAL
    LocalDiagnostics
    SavedSemaContext
    State = Open | Evaluated | Discarded
```

There is no `adopt()`, semantic `commit()`, journal, general state registry,
branch merge, or immutable-model publication in this slice.

Lifecycle is one-way:

```text
Open -> Evaluated -> Discarded
Open -------------> Discarded
```

RAII guarantees discard on every return and failure path. Evaluation after
discard, double discard, or result extraction before evaluation fails closed
in debug/test builds and produces no parent mutation.

## Transactional state manifest

The child owns working copies of only the real state touched by the admitted
probe.

### Cloned actual

The candidate checks a clone of the actual expression. Resolved type, resolved
callee/shape, implicit conversion, and other elaboration may change only on
that clone. The source AST and its argument vector remain byte-for-byte
unchanged.

### Working Scope chain

The visible Scope chain is copied structurally for the probe. At minimum its
`SymbolInfo` copies isolate:

- used/mutated flags;
- whole and exact-place move/init facts;
- move origin and initialization mask;
- borrowed path and dependency sets;
- permission/capability flow; and
- conditional editor-only facts.

The admitted source binding must resolve to the copied function-local Scope.
Module globals and imported bindings are outside this slice.

`ImportingDecl` and other shared declaration pointers grant lookup only. Probe
code may not write their used flags or declaration caches; the working copy
removes or buffers such write paths. The final legacy call remains responsible
for real use marking.

### Working PAL

The child receives `PALCheckerState.snapshot()` and makes all borrow,
invalidation, conflict, and transient-loan changes on that copy. Discard does
not call `restore()` on the parent because the parent was never replaced.

### Local diagnostics

A stack-scoped diagnostic sink captures candidate errors and notes without
changing global error count, records, printing state, or warning deduplication.
Its codes may enter `CandidateProbeResult` for qualification only. They are not
public diagnostics or Evidence.

### Saved Sema context

Expected type, expected cede transfer, permission-suffix state, borrowing
selection state, and active-node context are installed through stack RAII and
restored on every exit. The admitted slice forbids an operation that would
populate generic/type/declaration caches or allocate a semantic identity.

Public Evidence v1 remains disabled inside the child. D.3a receives no child
receipt because the existing dynamically scoped speculative context remains
active for the entire evaluation.

## Candidate evaluation

For an admitted overload set:

1. Freeze candidates in canonical declaration-identity order.
2. Capture the parent manifest and D.3a command-local counters.
3. Create one child per candidate from the same captured parent manifest.
4. Check the cloned actual exactly once with that candidate's formal type.
5. Extract `CandidateProbeResult`, then discard the child.
6. Prove the parent manifest and D.3a counters are unchanged.
7. Select a unique compatible candidate using only the immutable results.
8. Run the selected call's existing final legacy argument check exactly once
   on the source AST.

Rejected siblings are never adopted. A selected child is also never adopted;
its contextual facts are advisory for candidate selection only. Final-call
Sema, diagnostics, Evidence v1, D.3a observation, and CodeGen continue to derive
from the single legacy final traversal.

If zero or multiple candidates remain compatible, the existing legacy
resolution outcome and diagnostics remain authoritative, but are reproduced
from the immutable results without rerunning a live candidate probe. M1b.1a
must not invent a new overload policy.

## Parent preservation gate

Before and after every child, qualification compares structural fields for:

- source call/actual AST and resolution caches;
- visible Scope symbols covered by the manifest;
- parent PAL and transient loans;
- diagnostics and complete Evidence buffers;
- D.3a considered/factory/envelope counters;
- semantic identity builders/counters, if any; and
- the admitted cache inventory, which must remain unchanged.

Digests may be printed for debugging but never replace structural comparison.
Probe order is tested in forward, reverse, and repeated-candidate sequences.

## Qualification matrix

M1b.1a requires real compiler fixtures for:

- two candidates where exactly one whole-local type matches;
- reversed declaration/candidate order with the same selected declaration;
- a `cede` local whose rejected sibling attempts invalidation;
- active PAL borrow conflict in one or all children;
- zero-match and ambiguous-match outcomes;
- nested-call/generic/temporary/projection exclusion with child count zero;
- source AST, parent Scope/PAL, diagnostics, Evidence v1, and D.3a receipt
  parity with the legacy compiler; and
- injected early failure at child creation, evaluation, result extraction, and
  discard, each leaving the parent unchanged.

The repository test must also prove that the child implementation does not
include or call SemanticModel publication, CodeGen, Evidence v2, or ownership
commit APIs.

## Exit criteria

M1b.1a is complete only when:

- the admitted real overload path no longer runs candidate checks against the
  source AST or parent Scope/PAL/diagnostics;
- all children are discarded and no public semantic behavior changes;
- the selected declaration, final diagnostics, exit status, Evidence v1, and
  D.3a final receipt match the qualified baseline;
- parent preservation and probe-order gates pass; and
- the implementation receives independent post-implementation acceptance.

## Non-authorization

This contract does not authorize generic deduction isolation, closure
precompute migration, whole-call prepare/validate, multi-argument planning,
SemanticModel publication, ownership adopt/commit, CodeGen consumption,
Evidence v2, route convergence, `E04570` removal, or caller-spelling
activation. Those require later reviewed slices.
