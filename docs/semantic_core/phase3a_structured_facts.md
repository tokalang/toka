# Phase 3A Structured Facts Completion

Date: 2026-07-11

Phase 3A replaces lossy compiler-local representations with a small set of
structured semantic facts. It does not add syntax, change an accepted or
rejected source form, or expose a new public format.

## Implemented Contracts

### Access paths

`AccessPath` now carries a stable root symbol identity, a display name, source
locations, and typed projections for fields, constant indices, dynamic
indices, dereferences, and unknown provenance.

Overlap is classified as `NoOverlap`, `MayOverlap`, or `MustOverlap`.
Unresolved or dynamic projections remain conservative. Legacy path rendering
is retained only at diagnostic boundaries so existing diagnostic text remains
stable.

### Stable borrow-source traversal

Symbols receive an internal identity when defined. Borrow-source traversal
uses those identities and a visited set, removing the former fixed-depth
limit. Long valid chains can therefore be resolved without silently changing
behavior at an arbitrary depth, while cycles terminate deterministically.

### Structured PAL facts

The PAL ledger is keyed by `AccessPath`, not path strings. Conflict checks
return a `PALConflict` containing the structured conflicting path and its
state. Sema consumers use that state directly; they no longer reconstruct
semantic state by parsing diagnostic text.

The operation being checked remains explicit through `PALOperationClass`.
Borrow authorization and diagnostics preserve their existing source behavior.

### Assignment lowering contract

Typed assignment nodes carry one of `Payload`, `Handle`,
`ResidualCompound`, or `Unclassified`. Sema computes this classification in
normal compilation, independently of optional statistics.

CodeGen verifies that a classified payload assignment selects `SoulStore` and
that a classified handle assignment selects `EnvelopeRebind`. A disagreement
is an internal compiler error. Statistics remain observational and are not a
semantic input.

## Verification

The following checks completed successfully:

- compiler build and access-path unit tests,
- a borrow chain extending beyond the former traversal limit,
- the complete negative diagnostic suite,
- PAL stress and member/payload borrow positive cases,
- topology evaluation across the complete positive source corpus,
- explicit assignment classification against the independent lowering model,
- source-less semantic replay and semantic cache invalidation,
- TKI cache validation and incremental-build validation, and
- the complete positive suite apart from its unchanged network/runtime
  environment failures.

The assignment audit reported no Sema/CodeGen disagreement and no missing
lowering for directly classified assignments. Topology evaluation reported no
disagreement between explicit assignment facts and its independent reference
classification. Residual and unrecorded simulator sites remain conservative;
Phase 3A does not claim that every AST operation now has a semantic record.

## Resulting State

The compiler now has a single structured representation for access paths at
PAL decision points, deterministic borrow-source traversal, structured PAL
conflict results, and a typed-AST assignment fact checked by lowering.

This closes the representation gap needed before richer diagnostic evidence
or alias-analysis consumers can be trusted. It does not yet provide rule-ID
decision records, causal diagnostic chains, per-function memory summaries, or
LLVM alias attributes. Those remain separate gates; none should be inferred
from Phase 3A alone.
