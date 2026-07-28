# Phase 3 Semantic Evidence Necessity Audit

Date: 2026-07-11

This document records the implementation state at audit time. Phase 3A's
subsequent implementation is recorded in `phase3a_structured_facts.md`.
Phase 3B's implementation is recorded in `phase3b_decision_evidence.md`.
The later public contract is
[`../semantic_evidence_v1.md`](../semantic_evidence_v1.md); the decision below
is historical and no longer describes the current public-interface status.

This audit asks whether Toka needs a semantic evidence layer for the compiler
itself. It considers only needs demonstrated by the current implementation and
does not propose any language-design change.

## Decision

At audit time Toka needed a small, structured, internal semantic-fact layer.
It did not yet need a general event-sourcing architecture or a stable public
trace format. Public Semantic Evidence v1 later promoted the bounded decision
record—not a general trace—to a stable tooling protocol.

The immediate engineering need is to preserve facts that the compiler already
computes, but currently represents repeatedly as strings, transient side
tables, or local booleans. The first consumers should be:

1. consistent PAL/ownership/effects diagnostics,
2. Sema-to-CodeGen lowering assertions,
3. source/TKI decision-equivalence tests, and
4. a sound foundation for later alias analysis and LLVM attributes.

Backend optimization is a possible downstream benefit, not a justification
for weakening the required soundness boundary.

## Evidence Reviewed

### Current Compiler

- PAL stores shared and exclusive borrows in a lexical ledger and compares
  source paths by prefix overlap.
- Ownership, move, `cede`, escaping dependencies, member dependencies, and
  execution-boundary checks are already represented in Sema.
- TKI replay and cache invalidation preserve the frozen caller-visible facts.
- Diagnostic codes are stable, but diagnostics generally report only the
  final conflict and do not retain a structured causal chain.
- Assignment classification exists as `Payload`, `Handle`,
  `ResidualCompound`, or `Unclassified`, but only when assignment statistics
  are enabled. It is stored in a process-global map keyed by AST addresses.
- CodeGen independently chooses `SoulStore` or `EnvelopeRebind` and records
  the lowering carrier only in the same statistics mode.
- CodeGen emits no Toka-derived `noalias`, `nocapture`, `readonly`,
  `writeonly`, TBAA, or scoped alias metadata. LLVM can infer some attributes
  at `-O2` when a body is available, but source-less declarations do not carry
  equivalent frontend facts.

The PAL path implementation is not yet a suitable optimizer contract:

- path construction is duplicated across Sema files,
- paths are encoded as strings,
- array indices are not represented as precise projections,
- raw pointers and casts do not have an explicit `Unknown` provenance state,
- borrow-source canonicalization contains fixed depth limits, and
- the ledger records conflict state, not the origin and transition evidence
  needed for diagnostics or IR scope construction.

## Real Compiler Needs

### N1. Canonical Access Paths And Provenance -- Required

Introduce one internal structured path representation shared by PAL,
ownership, effects, diagnostics, and later analyses. A path should contain a
stable root symbol identity and typed projections such as field, constant
index, dynamic index, dereference, and unknown/raw escape.

Required properties:

- one canonical builder instead of per-file stringification,
- explicit `MustOverlap`, `NoOverlap`, and `MayOverlap` results,
- conservative `MayOverlap` for dynamic index, union, raw pointer, cast, and
  unresolved provenance,
- cycle detection rather than fixed borrow-chain depth limits, and
- source locations for root and derivation edges.

This is needed now for correctness maintenance and diagnostics. It also removes
the largest technical blocker to a future alias-analysis bridge.

### N2. Minimal Semantic Decision Record -- Required

Record only facts that already drive a compiler decision. The initial record
should contain:

- rule ID and operation class,
- canonical subject path,
- access/transition kind,
- optional origin path and source location,
- decision (`Allow`, `Reject`, or `ConservativeReject`), and
- reason category.

This is not a replay log of the entire compiler. It is evidence attached to
the relevant typed AST node or analysis state and may be emitted for tests on
demand.

Immediate uses are multi-location diagnostics, deterministic decision tests,
and confirmation that source and TKI consumers reach the same rule and reason.

### N3. Production Assignment Classification -- Required And Narrow

Move the existing assignment classification out of the optional statistics
side table and attach it to the typed assignment node. CodeGen should consume
that classification or assert that its independently selected lowering agrees
with it.

Keep `Unknown`/residual cases conservative. Do not build a runtime topology
cache until a real compiler pass needs one. The useful current result is the
Sema-to-lowering invariant, not the simulator itself.

This item closes an existing compiler-internal gap and does not require
extending the classification into a general PAL event model.

### N4. Internal Function Memory Summary -- Required Before Backend Claims

Toka signatures currently preserve caller-visible ownership and dependency
facts, but they do not constitute a complete LLVM memory-effect summary.
Before emitting optimization attributes, compute an internal summary with at
least:

- parameter/root reads and writes,
- handle rebind and invalidation,
- capture or escape through return, aggregate, global, closure, or async state,
- calls into unknown/extern/unsafe code,
- raw-pointer provenance loss, and
- allocation, free, and ownership transfer effects.

The summary is an internal analysis product, not new Toka syntax. It should be
derived conservatively from checked bodies and verified call contracts.

An untrusted source-less TKI must not be allowed to assert body-derived
optimization facts that the importer cannot revalidate. Cross-module use needs
one of: facts derivable from a checked signature, trusted compiler cache
provenance, or bitcode/LTO with the body available.

### N5. LLVM Attribute Bridge -- Valuable, But Gated

LLVM attributes can produce real gains, especially across separately compiled
modules, but incorrect attributes introduce undefined behavior and
miscompilation. Enable each attribute only after a separate soundness audit.

Candidate order:

1. call-site or internal-function `nonnull`, alignment, and
   `dereferenceable(N)` where the generated ABI itself constructs the pointer;
2. capture facts only after escape analysis proves no surviving copy;
3. read/write memory effects only after the function summary accounts for
   indirect calls, globals, interior mutability, raw pointers, and unsafe code;
4. scoped alias metadata after PAL borrow scopes map precisely to emitted
   memory instructions; and
5. function/call-site `noalias` last.

Current PAL call-argument conflict checks prove only path overlap and access
compatibility among visible arguments. LLVM parameter `noalias` is stronger:
it also constrains accesses through other based and non-based pointers during
the call. Unique ownership alone is not enough while raw aliases, globals,
external linkage, and disabled borrow checking remain possible.

The `--disable-borrow-check` mode must never emit optimization facts whose
soundness depends on PAL. Unsafe/raw boundaries must explicitly degrade alias
and capture knowledge to `Unknown` unless a separately verified contract
restores it.

## What Not To Build Now

- No universal semantic event bus.
- No mandatory trace for successful compilation.
- No stable public JSON trace ABI yet.
- No serialization of uncheckable optimization promises into ordinary TKI.
- No `noalias` based only on `^`, `cede`, a mutable parameter, or pairwise PAL
  call-argument checks.
- No attempt to formalize all PAL/ownership/async behavior before improving
  the concrete compiler representation.
- No in-function async-suspension rule change in this phase.

## Recommended Implementation Sequence

### Phase 3A: Structured Facts

1. Add canonical `AccessPath`, projection, overlap, and provenance types.
2. Replace duplicated path stringification at PAL decision points.
3. Remove fixed-depth borrow-source chasing using stable identities and cycle
   detection.
4. Attach assignment classification to typed assignment nodes.
5. Add lowering-agreement assertions and focused tests.

Success means no language decision changes, existing suites remain stable, and
all PAL conflicts can identify both the current operation and originating
borrow/transfer.

### Phase 3B: Diagnostics And Replay Evidence

1. Associate frozen rule IDs with decision records.
2. Emit primary and origin locations for PAL/ownership/effects conflicts.
3. Add an opt-in deterministic test dump; keep it internal/versioned.
4. Compare source and source-less TKI decisions by rule, operation, and reason,
   not by prose alone.

Success means the evidence layer has a real user and maintenance payoff even
if no backend optimization is ever enabled.

### Phase 3C: Memory Summary And Optimization Experiments

1. Implement conservative per-function memory summaries.
2. Validate summaries against generated IR and unknown-call degradation.
3. Add IR contract tests for source, TKI, unsafe, raw, generic, and async
   boundaries.
4. Enable one low-risk attribute class behind an experimental flag.
5. Run differential correctness tests across `-O0` and optimized builds, then
   benchmark before enabling by default.

`noalias` remains a separate final gate and requires its own written soundness
argument.

Phase 3C is complete through its benefit audit. The first emitted contract,
`nocapture`, remains behind an experimental flag: it demonstrated a narrow
coroutine-frame and object-size reduction, but no stable runtime improvement
sufficient for default enablement. See
`phase3c_nocapture_benefit_audit.md` for the replayable method and decision.

## Resulting Compiler State

After 3A and 3B, Toka will have a structured, explainable semantic core whose
facts survive long enough to support diagnostics and verify lowering, without
turning the compiler into a research artifact.

After a successful 3C, selected source-level guarantees can become checked
backend contracts across module boundaries. That would make PAL and explicit
resource semantics useful not only for rejection safety, but also for
optimization. The gain is credible only when each emitted contract is weaker
than or equal to what the compiler has actually proved.
