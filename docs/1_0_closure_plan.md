# Toka 1.0 Closure Plan

Status: `InProgress`

This document is the single execution index for taking Toka from the current
0.9.8 line to the 1.0 language and compiler freeze. It records scope, phase
status, blockers, evidence, and stop conditions. It does not duplicate the
language specification or redefine an existing language rule.

## 1. Frozen Project Decisions

The following decisions govern all 1.0 closure work:

- The 1.0 scope is the public source language and semantics, compiler safety
  and correctness, same-version `.tki` semantic replay, and the core runtime
  contract needed by the frozen language.
- Standard-library breadth, peripheral tools, ecosystem maturity,
  self-hosting, and backend performance are not 1.0 blockers.
- Linux and macOS are the supported 1.0 release platforms. Windows/MSYS2,
  WSL2, and WASI may remain available or experimental, but failures there do
  not block 1.0.
- Toka 1.x promises source semantic compatibility. `.tki`, build-cache, and
  binary ABI compatibility remain compiler- and format-version-bound.
- The current async color and consumption model (`fn -> async T`, `.await`,
  `.wait`, and `.start`) is in the 1.0 surface. Richer cancellation, async
  blocks, parameterized `.start`, and structured concurrency are post-1.0.
- Experimental `nocapture` and `readonly` stay non-default. `writeonly` is
  stopped at its summary-precision boundary, and `noalias` is paused. None is
  a 1.0 completion condition.
- A safety or miscompile fix may reject source that relied on unsound behavior.
  Such a change must be recorded as a safety fix; it is not a general license
  to change frozen semantics.

No implementation task may introduce syntax, change the meaning of an
accepted program, or expand the 1.0 language surface without a separate design
decision from the project owner.

## 2. Normative Document Order

When documents disagree, resolve the contradiction explicitly in this order:

1. `docs/1_0_freeze_decision_list.md` defines 1.0 scope and exclusions.
2. `docs/syntax.md` is the normative English source-language specification.
3. `docs/syntax_zh.md` is the synchronized Chinese specification.
4. `docs/semantic_core/rule_matrix.md` maps rules to implementation,
   diagnostics, interface replay, and tests.

This plan tracks whether those sources agree. It does not silently choose a
new semantic answer when they do not.

## 3. Maintenance Protocol

Every 1.0 task must name one `FZ-*` phase or one blocker from this document.
Each completed task must leave reviewable evidence in tests or documentation
and update this ledger in the same change.

Only these phase states are valid:

- `Pending`
- `InProgress`
- `Blocked`
- `Complete`
- `Deferred`

A phase is not `Complete` merely because implementation work was performed.
Its exit criteria and evidence must both be present. Each phase is committed
as an independently reviewable milestone. The commit that introduces a phase
record is identified by its subject when it cannot contain its own final hash.

## 4. Phase Ledger

| Phase | Status | Purpose | Exit evidence |
| --- | --- | --- | --- |
| `FZ-0` | `Complete` | Establish the closure ledger and classify the public surface | This document, the capability ledger, and the unresolved-item register |
| `FZ-1` | `Pending` | Close the language contract and in-function async suspension rules | Frozen async rule matrix, dedicated pass/fail coverage, synchronized specification |
| `FZ-2` | `Pending` | Close high-risk semantic combinations and source/TKI equivalence | Rule coverage closure and three-path replay results |
| `FZ-3` | `Pending` | Close compiler crashes, miscompiles, ownership cleanup, determinism, and platform reliability | Sanitizer/mutation evidence and clean Linux/macOS gates |
| `FZ-4` | `Pending` | Freeze public specification, compatibility policy, diagnostics, and core runtime contract | Consistency audit and stable public-contract tests |
| `FZ-5` | `Pending` | Run the release-candidate moratorium and final 1.0 gate | Deterministic gate report and clean supported-platform release runs |

Work proceeds in phase order. A later phase may collect evidence early, but it
cannot be declared complete while an earlier semantic blocker can invalidate
that evidence.

## 5. Public Capability Ledger

The classification is about the 1.0 contract, not implementation quality.
Items classified `Frozen` may still have open verification work in `FZ-1`
through `FZ-4`.

### Frozen

- Payload/handle separation and the existing `&`, `*`, `^`, `~`, `#`, and `$`
  placement and permission rules.
- `auto` bindings, payload mutability, handle rebinding, nullability, and
  explicit `cede` transfer obligations.
- Shapes, named construction, named destructuring, tagged variants, field
  morphology, private structural facts, and resource-bearing shape rules.
- Functions, methods, closures, explicit `cede`/`copy` capture, escaping
  dependencies, `effects:` routing, and member-specific return dependencies.
- Traits, facet constraints, `where:`, associated `type`/`per type`, and
  single-facet `dyn @Trait` within the frozen object-safety boundary.
- `@encap`, `pub`, `pub(crate)`, `pub(path)`, wildcard visibility, imports,
  re-exports, and the filesystem-path/name hyphen boundary.
- `if`, `guard`, `match`, `loop`, `for`, `break`, `continue`, block `pass`,
  enum exhaustiveness, guard handling, or-pattern consistency, and
  resource-safe patterns.
- PAL path overlap, simultaneous call borrow groups, branch/loop state merge,
  move invalidation, borrow validity, and conservative local proof.
- Async effect consumption, async return dependencies, `.start` execution
  boundaries, and explicit owned handoff. In-function suspension combinations
  remain the `FZ-1` verification target.
- Same-version `.tki` replay, semantic cache invalidation, untrusted-interface
  revalidation, and object-bound trusted evidence fallback.
- The public unsafe/raw redline and the rule that raw pointers are outside the
  PAL safe-borrow guarantee.
- Core text/byte forms and the current explicit conversion boundary described
  by the 1.0 string specification.

### ConservativeRejection

- PAL cases that cannot be proven by the local path and control-flow model.
- Non-enum matches without an unguarded wildcard, `default`, or unconditional
  variable arm; 1.0 does not perform full literal/range/string domain proofs.
- Escaping borrow-like values without explicit signature dependencies,
  including private helpers.
- Hidden borrow capture across thread/task boundaries and `.start` with
  borrowed, raw, or dependency-bearing state.
- Shape/resource transfer across `.start` without both a `cede` parameter and
  an explicit `cede` call argument.
- Dynamic trait objects with generic methods, non-receiver `Self`, associated
  types without a supported binding model, or more than one facet.

### Experimental

- `--experimental-memory-contracts=nocapture`.
- `--experimental-memory-contracts=readonly`.
- Windows/MSYS2 native delivery and WASI as non-blocking targets.

Experimental behavior is not covered by the 1.x source compatibility promise
unless it is separately promoted through a new audited decision.

### Post1.0

- Multi-facet dynamic trait objects, associated-type binding on dyn objects,
  and dyn object lifetime/ownership annotations.
- Shape-internal self-borrow dependencies and stable-placement/immovable
  construction semantics.
- Full value-domain exhaustiveness, private-helper dependency inference, and
  more aggressive PAL acceptance.
- Larger iterator/async trait formalization, async blocks, parameterized
  `.start`, cancellation semantics, and structured concurrency.
- Global destructuring and formatted `String`/`str` printing beyond the
  currently supported plain formatting path.
- Resolver-normalized cross-package identity refinements for `pub(path)`.
- PAL-derived backend `noalias`, further memory-contract promotion, and
  build-cache performance redesign not required by correctness.

### Removed

- Shape-header dependency syntax such as `shape Ref <- field`.
- Shape-member dependency annotations such as `&view: T <- owner` in the 1.0
  grammar; a future stable-placement design must not reuse the removed header
  syntax as a shortcut.
- Tuple and legacy `union` declaration syntax.
- Positional shape construction/destructuring and mixed named/positional shape
  forms.
- `while`; conditional loops use `loop condition`.
- Value-yielding loops and `break` values.
- `let`/`var` binding syntax and implicit string concatenation with `+`.

## 6. Open Blocker Register

| ID | Phase | Status | Blocker | Required resolution |
| --- | --- | --- | --- | --- |
| `FZ-1-A01` | `FZ-1` | `Pending` | Borrow, move, init, and dependency state across in-function suspension is not yet frozen by dedicated tests | Audit the current model, add branch/loop/frame tests, and synchronize the async specification without adding syntax |
| `FZ-2-R01` | `FZ-2` | `Pending` | The semantic rule matrix still lists explicit source-less and generic replay gaps | Close every listed gap or reclassify it with an evidence-backed `Deferred` decision |
| `FZ-2-R02` | `FZ-2` | `Pending` | TKI export/import retains legacy shape life-dependency paths although both old source forms are outside 1.0 | Prove excluded syntax cannot re-enter through a forged or stale interface, then remove or tightly bound the legacy paths |
| `FZ-3-C01` | `FZ-3` | `Pending` | Panic lowering contains a temporary trap/abort path | Determine whether public valid programs reach it, then implement or formally bound the 1.0 behavior |
| `FZ-3-C02` | `FZ-3` | `Pending` | Bare expression lowering for `ArrayInitExpr` still returns no value outside the specialized initialization paths | Build a public-syntax reachability matrix and either implement the valid expression cases or reject them before CodeGen |
| `FZ-3-R01` | `FZ-3` | `Pending` | Import symbol filtering has an unresolved resolver TODO | Reproduce its public effect and either fix it or record why it is unreachable in the frozen grammar |
| `FZ-3-T01` | `FZ-3` | `Pending` | The current full positive run has three environment/runtime-dependent network or async failures | Make mandatory tests deterministic on supported platforms; do not hide failures with retries |
| `FZ-3-A01` | `FZ-3` | `Pending` | No bounded parser/Sema/interface mutation gate is part of the release contract | Add fixed-seed mutation coverage and run the core corpus under ASan/UBSan |
| `FZ-4-D01` | `FZ-4` | `Pending` | Logical capture semantics versus scalar value ABI is not stated as a public FFI boundary | Document the logical/physical distinction and lock representative ABI tests |
| `FZ-4-D02` | `FZ-4` | `Pending` | README platform wording still presents Windows parity as near-term while the 1.0 decision makes it non-blocking | Align public support wording with the frozen Linux/macOS release boundary |
| `FZ-4-D03` | `FZ-4` | `Pending` | Several diagnostics say "not yet supported" without naming their 1.0 classification | Replace ambiguous wording with frozen exclusion or post-1.0 terminology while preserving diagnostic identity |

`Blocked` is reserved for work that cannot proceed without a design decision or
an external supported-platform result. Ordinary incomplete work remains
`Pending` or `InProgress`.

## 7. Phase Exit Contracts

### FZ-0: Closure Ledger

- Every public capability has one classification from this document.
- Every discovered unresolved public item has a phase/blocker or an explicit
  post-1.0 classification.
- PAL, ownership, effects, semantic evidence, TKI replay, and trusted-cache
  work already completed are recorded as the starting baseline.

### FZ-1: Language And Async Closure

- Existing async syntax and consumption behavior are documented consistently.
- `.await`/`.wait` preserve declared dependencies and `.start` rejects hidden
  borrowed state in source and source-less replay.
- Frame-local ownership and PAL/init/move state survive suspension through
  branches, loops, `break`, and `continue` without allowing invalid use.
- Owned detached handoff requires both sides of the `cede` contract.
- Raw pointers remain outside PAL; no public safe interface gains an implicit
  raw exemption.
- No unresolved async design question remains in the 1.0 surface.

### FZ-2: Semantic And Interface Closure

- Every `Core guarantee` has focused positive and negative evidence and a
  stable diagnostic identity where rejection is expected.
- Every cross-module rule agrees across source import, same-version source-less
  `.tki`, and cache-invalidated source regeneration.
- Missing, stale, forged, or incompatible interfaces fail closed without
  partial semantic trust.
- Every `Missing coverage` entry in the rule matrix is closed or explicitly
  deferred without weakening a 1.0 guarantee.

### FZ-3: Correctness And Reliability

- No known valid/invalid-source crash, LLVM verifier failure, miscompile,
  ownership violation, double release, or use-after-move remains.
- Parser, Sema, and interface import have deterministic fixed-seed mutation
  coverage; the core corpus passes ASan/UBSan.
- Diagnostics, `.tki`, semantic evidence, and cache reports are deterministic.
- Mandatory network/async tests are deterministic and use environment-safe
  resources rather than fixed-port assumptions.
- Linux x64/arm64 and macOS x64/arm64 build and pass all mandatory gates.

### FZ-4: Public Contract Freeze

- English and Chinese specifications, freeze decisions, diagnostics, and
  implementation agree.
- Error codes are not reused for different meanings during 1.x; wording may
  improve without changing rule identity.
- Source compatibility and the safety-fix exception are public and tested.
- The minimal core runtime and core types required by frozen language features
  have explicit behavior and failure contracts.

### FZ-5: Release Candidate

- RC work observes a language-feature moratorium.
- One release-gate entry point runs build, pass/fail/warn, semantic replay,
  cache invalidation, incremental build, async, sanitizer, and package smoke
  checks and emits deterministic JSON outside the source tree.
- Every blocker fix resets the final gate. The final revision has one complete
  clean run for every supported Linux/macOS release target.
- Version and interface metadata are updated only after all earlier phases are
  `Complete`.

## 8. Current Evidence Baseline

The starting baseline already includes:

- a frozen PAL path/borrow/ownership rule matrix;
- structured semantic facts and replayable decision evidence;
- same-version source/source-less TKI replay and cache invalidation suites;
- object-bound trusted memory evidence with all-or-nothing validation;
- completed bounded audits for experimental `nocapture` and `readonly`;
- a stopped `writeonly` preflight with an explicit summary-precision reason.

These are foundations for 1.0 correctness. Their experimental optimizer
consumers are not release requirements.

## 9. Final 1.0 Stop Conditions

Toka 1.0 may be frozen only when all of the following are true:

- all `FZ-0` through `FZ-5` phases are `Complete`;
- no 1.0 capability or semantic decision is unclassified or unresolved;
- no known compiler crash, miscompile, ownership-safety defect, or source/TKI
  divergence remains;
- every frozen rule has focused evidence and every required diagnostic has a
  stable identity;
- async suspension, execution boundaries, and lifetime dependencies are
  closed;
- Linux and macOS release gates pass reproducibly;
- English and Chinese specifications agree with compiler behavior;
- all backend memory contracts remain non-default unless separately promoted.

At that point the document status changes to `Frozen`, 1.0 semantic expansion
stops, and new expressive power moves to an additive 1.x proposal or a 2.0
design track.

## 10. Scope Guard

External private work is outside this plan and must not be referenced from its
documentation, tests, commits, or reports. Every phase is justified and
recorded only as compiler and language engineering. Unrelated user files and
worktree changes remain outside all phase commits.
