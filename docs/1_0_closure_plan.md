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
- The synchronous iterator surface is the formal `@Iterable`, `@Iterator`, and
  `@BorrowIterator` protocol. Consuming and async iteration remain post-1.0.
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
| `FZ-1` | `Complete` | Close the language contract and in-function async suspension rules | `semantic_core/fz1_async_suspension_closure.md`, frozen async rule matrix, focused pass/fail coverage, and synchronized specification |
| `FZ-2` | `Complete` | Close high-risk semantic combinations and source/TKI equivalence | `semantic_core/fz2_semantic_tki_closure.md`, closed rule coverage, 10/10 source-less replay, and 12/12 cache regeneration cases |
| `FZ-3` | `Complete` | Close compiler crashes, miscompiles, ownership cleanup, determinism, and platform reliability | `semantic_core/fz3_compiler_reliability_closure.md` and clean revision `3ab00dff` release gates on Linux x64/arm64 and macOS x64/arm64 |
| `FZ-4` | `Complete` | Freeze public specification, compatibility policy, diagnostics, and core runtime contract | `semantic_core/fz4_public_contract_freeze.md`, synchronized specifications, stable diagnostic tests, and ABI-boundary execution coverage |
| `FZ-5` | `InProgress` | Run the release-candidate moratorium and final 1.0 gate | The previous four-target RC evidence covers revision `3ab00dff`; the explicitly authorized late iterator and callable closures require a fresh clean matrix |

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
- The single `@Callable` protocol and its `self` / `self#` / `cede self`
  receiver modes, including `fn`, `fn#`, and `cede fn` type preservation.
- Consuming Result/Option `!`, exact or one-step `@ErrorInto<Target>` error
  conversion, deterministic early-return cleanup, and typed
  `ErrorContext<E>` without error erasure.
- Traits, facet constraints, `where:`, associated `type`/`per type`, and
  single-facet `dyn @Trait` within the frozen object-safety boundary.
- Implicit prelude visibility for exactly `@encap`, `@Send`, `@Sync`, and
  `@Callable`;
  every other trait follows ordinary lexical imports.
- `@encap`, `pub`, `pub(crate)`, `pub(path)`, wildcard visibility, imports,
  re-exports, and the filesystem-path/name hyphen boundary.
- `if`, `guard`, `match`, `loop`, `for`, `break`, `continue`, block `pass`,
  enum exhaustiveness, guard handling, or-pattern consistency, and
  resource-safe patterns.
- The formal `@Iterable`, `@Iterator`, and `@BorrowIterator` facets, including
  associated item types, mandatory source dependencies, morphology-selected
  value/borrow iteration, scoped hidden-cursor cleanup, and same-version TKI
  replay.
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
- Consuming iterators, async iterators and combinator breadth, async blocks,
  parameterized `.start`, cancellation semantics, task groups, async join
  combinators, and structured concurrency.
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
| `FZ-1-A01` | `FZ-1` | `Complete` | Borrow, move, init, and dependency state across in-function suspension lacked dedicated tests | Closed by the frame/branch/loop pass case, four state-preservation fail cases, async-context diagnostics, and source/TKI `.await` replay recorded in `semantic_core/fz1_async_suspension_closure.md` |
| `FZ-2-R01` | `FZ-2` | `Complete` | The semantic rule matrix listed explicit source-less and generic replay gaps | Closed by member/cede call replay, generic function/method transfer replay, `str`/`bytes` dependency replay, generic `.start` handoff, and evidence-backed Post1.0 classification of trait-gated widening |
| `FZ-2-R02` | `FZ-2` | `Complete` | TKI export/import retained legacy shape life-dependency paths although both old source forms are outside 1.0 | Removed the internal field/export path and proved forged header/member declarations fail closed with `E01247`/`E01248` in `test_tki_excluded_syntax_revalidation.sh` |
| `FZ-3-C01` | `FZ-3` | `Complete` | Nullable raw-pointer unwrap reached a temporary LLVM trap | Lowered through the stable runtime panic path and locked by an expected-panic fixture |
| `FZ-3-C02` | `FZ-3` | `Complete` | Bare expression lowering for `ArrayInitExpr` returned no value outside specialized initialization paths | Classified bare `[N]T(...)` construction outside 1.0 and rejected in Sema with `E04586` |
| `FZ-3-C03` | `FZ-3` | `Complete` | Generic resource transfer through imported cede functions and methods aborted during runtime cleanup | Added branch-safe drop-live state and closure cleanup; executable transfer replay and exact-drop resource matrix pass |
| `FZ-3-R01` | `FZ-3` | `Complete` | Selective imports leaked symbols through global resolver maps | Separated physical discovery from lexical value/type/trait namespaces and replayed selected, aliased, and hidden forms through source and `.tki` |
| `FZ-3-R02` | `FZ-3` | `Pending` | Same-name generic shape templates still use legacy process-global instantiation and cache keys; concrete shape identity closure does not prove their isolation | Build one source/TKI reproducer before changing implementation; if it fails, repair template/instance/cache identity as a bounded correctness task without changing generic syntax |
| `FZ-3-T01` | `FZ-3` | `Complete` | Mandatory network and async tests depended on fixed ports | Bind ephemeral ports and query the assigned local port; the complete positive suite passes without retries |
| `FZ-3-A01` | `FZ-3` | `Complete` | No bounded parser/Sema/interface mutation gate was part of the release contract | Added deterministic 82-case fixed-seed audit; normal and ASan/UBSan builds pass |
| `FZ-3-P01` | `FZ-3` | `Complete` | A macOS arm64 workstation cannot produce clean native results for every supported release target | Closed by four clean native reports for revision `3ab00dff` in release-gate run `29202522704`; every row passed without ignored failures |
| `FZ-4-D01` | `FZ-4` | `Complete` | Logical capture semantics versus scalar value ABI was not stated as a public boundary | Specifications now separate source semantics from version-bound target lowering; mutable scalar and shape execution lock the distinction |
| `FZ-4-D02` | `FZ-4` | `Complete` | README platform wording presented Windows parity as near-term while the 1.0 decision made it non-blocking | README now names Linux/macOS as supported 1.0 platforms and Windows/MSYS2, WSL2, and WASI as available or experimental non-blockers |
| `FZ-4-D03` | `FZ-4` | `Complete` | Diagnostics used "not yet supported" without naming their 1.0 classification | `E04547` and `E0744` retain their identities and now state explicit 1.0 exclusions, with focused negative tests |
| `FZ-4-D04` | `FZ-4` | `Complete` | Closure capture ownership existed, but shared, exclusive, and consuming invocation were not a replayable public contract | Added one `@Callable` protocol, receiver-morphology inference, `fn#`/`cede fn` type modes, exact consuming cleanup, stable diagnostics, iterator/thread composition, and source-less replay |
| `FZ-4-D05` | `FZ-4` | `Complete` | `!` accepted layout-compatible error types and CodeGen copied unmatched union storage without a conversion contract | Added one-step `@ErrorInto<Target>`, parameterized trait bounds, typed context, exact sync/async cleanup evidence, stable diagnostics, and source-less replay; `dyn error` and `main -> Result` remain deferred |
| `FZ-5-G01` | `FZ-5` | `Complete` | Release checks were split across scripts and the release workflow ignored positive-suite failures | Added one fail-closed twelve-stage gate, deterministic JSON, two sustained reference applications, package smoke, and a four-target workflow with no ignored mandatory failures |
| `FZ-5-P01` | `FZ-5` | `Complete` | Final RC evidence requires clean native reports that one workstation cannot produce | All four `v0.9.8-08-RC` reports for revision `3ab00dff` have `source_dirty: false` and `result: pass` in run `29202522704` |
| `FZ-5-P02` | `FZ-5` | `Complete` | The authorized late language closures and later application-driven fixes changed the candidate revision after `3ab00dff` | The clean four-target pre-release matrix passed and annotated pre-release tag `v0.9.8-09-RC` points to revision `a39c6acd` |
| `FZ-5-E01` | `FZ-5` | `Complete` | Sustained applications exposed avoidable compiler mechanics in ordinary source, including allocation-only text comparison, redundant contextual literal suffixes, and bare-name shape collisions | The bounded `docs/1_0_ergonomics_audit.md` ledger is closed without hidden allocation or ownership action; two consecutive QSLite and native-builder qualifications pass, and further convenience discovery is stopped for 1.0 |

The Toka-native incremental build orchestrator is the sustained `FZ-5`
reference application. Its workload, finding policy, and finite stop conditions
are maintained in `native_build_reference_plan.md`; it does not expand the 1.0
language surface or replace the clean four-platform release gate.
Local `NB-1` through `NB-3` evidence is complete, including 100 fixed-seed
mutation cycles. The qualification is now a mandatory stage of the unified RC
gate. `NB-4` remains in progress until clean `v0.9.8-09-RC` reports cover both
supported platform families.

QSLite is the second bounded `FZ-5` reference application. Its persistent
storage workload, corruption policy, toolchain replay requirements, and finite
stop conditions are maintained in `qslite_reference_plan.md`. It validates the
frozen language through a real stateful program and does not add SQL, storage,
or language breadth to the 1.0 contract. `QS-0` through `QS-4` are complete:
the fixed-seed storage/corruption qualification and source-less TKI,
incremental, locked, and offline toolchain paths pass. The final four-target RC
matrix runs after these QSLite-driven fixes so later application work cannot
invalidate the release evidence.

The bounded 1.0 source ergonomics audit is maintained in
`1_0_ergonomics_audit.md`. It removes application-proven repetition when the
compiler already has one zero-cost interpretation. It does not permit hidden
allocation, clone, transfer, or ambiguous coercion, and it has explicit stop
conditions so release work cannot become an indefinite syntax-polish cycle.
`ERG-1` through `ERG-7` are complete. Two consecutive sustained qualifications
found no new workaround, so additional preference-level convenience work is
post-1.0 unless a reference application or release gate demonstrates a new
correctness issue or ordinary-code blocker.

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
- frozen async suspension state and source/TKI dependency replay;
- closed high-risk PAL/ownership/effects/async combinations, removed-syntax
  revalidation, and same-version source/source-less replay;
- local FZ-3 correctness closure with 318/318 positive tests, 237/237
  negative tests, 11/11 semantic replays, 12/12 cache cases, and normal plus
  ASan/UBSan fixed-seed reliability audits at 82/82;
- completed FZ-4 public-contract freeze with synchronized compatibility,
  source/ABI, runtime, platform, and diagnostic boundaries;
- completed the FZ-5 unified RC gate, deterministic report, self-contained
  package smoke, and four-target clean matrix at revision `3ab00dff`;
- completed the local iterator-protocol closure with 320/320 positive tests,
  242/242 negative tests, 1/1 warning tests, and 12/12 source/source-less
  semantic replay cases; a replacement four-target RC matrix is still pending;
- completed the callable-protocol closure with receiver-morphology inference,
  generic/user callables, iterator and thread composition, consuming exact-drop
  execution, stable diagnostics, and a thirteenth replay case;
- completed the async runtime lifecycle audit: detached coroutine frames now
  have an explicit destruction path, context helpers own their shared state,
  reactor registration failure no longer suspends forever, and the unsafe
  pre-1.0 `TaskGroup` cancellation experiment is removed;
- completed the bounded QSLite reference application with deterministic
  persistent-state and corruption qualification plus source-less TKI,
  incremental, locked-package, and offline replay evidence;
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

The `v0.9.8-08-RC` evidence at revision `3ab00dff` is the historical baseline,
but it predates the authorized iterator and callable closures and no longer
satisfies the final-current-revision condition. `v0.9.8-09-RC` is the active
replacement candidate. This document and `FZ-5` remain `InProgress` until its
four-target matrix passes and an explicit decision authorizes the 1.0 version
transition and final release act.

At that point the document status changes to `Frozen`, 1.0 semantic expansion
stops, and new expressive power moves to an additive 1.x proposal or a 2.0
design track.

## 10. Scope Guard

External private work is outside this plan and must not be referenced from its
documentation, tests, commits, or reports. Every phase is justified and
recorded only as compiler and language engineering. Unrelated user files and
worktree changes remain outside all phase commits.
