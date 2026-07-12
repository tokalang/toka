# FZ-2 Semantic And TKI Closure

Status: `Complete`

`FZ-2` closes the high-risk combinations between PAL, ownership, escaping
dependencies, async execution boundaries, and same-version `.tki` replay. It
adds no syntax and does not broaden an accepted 1.0 language rule.

## Closed Rule Combinations

- Simultaneous call checking now has source/source-less replay for overlapping
  and disjoint member paths and for cede/read alias conflicts.
- Generic resource transfer is replayed through imported functions and methods,
  with explicit-calling obligations, double-consumption rejection, and
  use-after-move rejection.
- Reference, `str`, and `bytes` return dependencies are replayed from generated
  interfaces and continue to block replacement of their declared source.
- `.start` replays concrete and generic owned handoff only when the imported
  parameter and call argument both carry `cede`.
- Dangling async calls are rejected in direct and conditional-block expression
  contexts.
- Removed shape-header and shape-member dependency syntax is rejected when it
  is injected through a forged interface.

The rule-by-rule disposition is recorded in `rule_matrix.md`. There are no
remaining `Missing coverage` entries for a frozen FZ-2 rule. Trait-gated
Send/Sync widening for `.start` is Post1.0, not an unverified 1.0 promise.

## Implementation Closure

The combination tests exposed three Sema state/representation defects:

- expression dependency scratch state survived from one top-level function to
  the next, allowing a later function to inherit a prior borrow source;
- impl substitution recognized bare `Self` but not decorated forms such as
  `cede Self`;
- an `auto` binding retained the call-edge `cede` marker after accepting a
  returned transfer, instead of owning the underlying value.

The implementation now scopes dependency scratch state to each checked
function, substitutes `Self` inside decorated method types, and removes the
call-edge cede marker when a local binding accepts the transfer. These changes
make existing signatures compositional; they do not introduce a new transfer
form.

The legacy `ShapeDecl::LifeDependencies` field and exporter branch were also
removed. The parser remains the only entry point for both source and `.tki`, so
forged excluded declarations fail with `E01247` or `E01248` instead of
re-entering through an internal compatibility path. Borrow-like fields still
derive dependencies from their initializers.

## Replay Contract

Every semantic replay consumer is checked twice:

1. against provider source with interface cache use disabled;
2. against the generated same-version `.tki` after provider source is hidden.

Acceptance, primary diagnostics, and required structured semantic evidence
must agree. Cache cases separately prove that a semantic source change rejects
the old interface with `SourceHashMismatch`, falls back to source, regenerates
the interface, and preserves the new decision after source is hidden again.

Missing, forged, stale, or mismatched interfaces are never partially trusted.
The `.tki` guarantee remains same-version semantic equivalence, not a stable
cross-version format or binary ABI.

## FZ-3 Handoff

The generic function/method resource fixture compiles and replays identical
semantic evidence, but its linked executable exits through `SIGABRT` during
runtime cleanup. The replay consumer is therefore compile-only and the valid
program failure is registered as `FZ-3-C03`. It is not hidden or counted as an
FZ-2 semantic success at runtime; ownership lowering and cleanup correctness
belong to the next phase.

## Verification Snapshot

- compiler build: passed;
- compiler negative suite: 226 passed, 0 failed;
- warning suite: 1 passed, 0 failed;
- semantic source/source-less replay: 10 passed, 0 failed;
- semantic cache invalidation: 12 passed, 0 failed;
- semantic evidence and trusted memory evidence: passed;
- TKI metadata/cache validation, path behavior, unsafe revalidation, and forged
  excluded-syntax revalidation: passed;
- incremental build suite: passed;
- experimental `nocapture`/`readonly` regression gates: passed;
- full positive suite: 311 passed, with only the same three pre-existing
  environment/runtime network cases registered as `FZ-3-T01` failing;
- generic resource runtime failure: reproduced with exit status 134 and handed
  to `FZ-3-C03`.

## Decision

`FZ-2` is complete. Every frozen semantic-core rule has a closed coverage
disposition, source/source-less replay has no known acceptance or diagnostic
divergence, and excluded interface syntax fails closed. Runtime cleanup,
sanitizer, mutation, determinism, and supported-platform reliability now move
to `FZ-3`.

Milestone commit subject: `feat: close Toka 1.0 semantic and TKI matrix`.
