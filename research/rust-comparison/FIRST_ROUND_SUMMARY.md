# First-round evidence and ROI snapshot

This is an interim evidence summary, not a release decision and not a language
ranking.  It aggregates only the runnable cases in this directory.

## Evidence inventory

| Area | Classification | What the runnable evidence establishes |
|---|---|---|
| Handle / payload authority | Observed distinction | Toka independently expresses payload write, handle replacement, and both; use-site syntax cannot forge the missing axis. Rust safely selects `&mut T` / `&mut Box<T>` views, but `&mut Box<T>` is not source-level handle-only authority because of mutable dereference. |
| Field-level interior mutability | Observed distinction | Toka's `field#` permits only that field to be written through a shared aggregate view. Rust expresses an equivalent operational pattern with `Cell<T>` and rejects the ordinary-field variant. |
| Scoped borrowed concurrency | Current 1.0 boundary | Rust `thread::scope` safely permits a borrowed child. Toka detached `.start` rejects borrowed input with `E04583`; current `TaskScope` is runtime task management rather than lexical borrowed-child semantics. |
| Detached non-borrowing work | Observed parity | Ordinary Rust `thread::spawn` also rejects a borrowed parent capture unless a separate scoped protocol is used; both languages accept detached work with non-borrowing input. |
| Lazy consuming iterator | Current 1.0 boundary | Rust's standard iterator can own a source and mutable callback for deferred iteration. Toka supports the eager generic callback baseline but has no frozen consuming/lazy iterator protocol. |
| Ordinary borrowed iteration | Observed parity | Toka `next_ref` and Rust `iter()` both traverse borrowed elements without consuming the collection. |
| Dyn object with associated type | Current 1.0 boundary | Rust can erase `Readable<Item = i32>` as a trait object. Toka rejects `dyn @Readable<Item = i32>` with `E0617` under its fixed 1.0 dyn-object ABI rule. |
| Async trait protocol | Current interface boundary | Toka accepts a concrete `fn -> async T` trait method, but rejects the generic borrowed-receiver await that Rust accepts with `E04583`. The accepted subset remains unfrozen. |
| Fallible entry and error erasure | Current 1.0 boundary | Rust supports `main -> Result` with `Box<dyn Error>`. Toka keeps typed helper errors and requires `i32`/`void` main, rejecting `main -> Result` with `E04596`. |
| Shared-write diagnostic | Quality comparison | Both reject invalid shared-field writes. Toka keeps its authority-specific diagnostic sequence and now adds a field-authority note; Rust gives one direct mutability message, whose suggested `&mut` is not always the desired interior-mutability repair. |
| Panic recovery | Semantic/runtime tradeoff | In an unwind-enabled build Rust can catch a panic and run `Drop`; Toka 1.0 defines panic as non-returning process termination with no cleanup promise after the panic point. |

## Claims the evidence does **not** support

The current cases do not support claims that Toka lacks generic callbacks,
ordinary borrowed iterators, zero-copy views/parsers, or simple borrowed return
dependencies.  They also do not show that Toka needs user-visible lifetimes,
HRTBs, or GATs to gain the two deferred protocols.

## Preliminary ROI hypotheses

These are prioritization hypotheses for future design work, not approved
roadmaps.

| Candidate | User value | Engineering cost / semantic risk | Preliminary ROI |
|---|---:|---:|---|
| H/P-aware diagnostics and warnings (`EXP-001` through `EXP-003`) | High immediate learnability; no new source feature | Low to medium; compiler diagnostic/dataflow refinement | **High**. First note/warning slice is implemented locally; frozen error codes remain unchanged. |
| Consuming/lazy iterator protocol | High library composability and parity with common systems-language idioms | Medium; needs ownership, adapter, `for`, and return-abstraction decisions | **Promising**, but begin with an RFC and one owned `Map<I,F>` vertical slice. |
| Scoped borrowed concurrency | High for structured parallelism and scoped task ergonomics | High; needs lexical scope, scheduler completion, PAL scope anchors, escape checks, and thread-safety interaction | **Potentially high**, but design-first. Do not implement as a small library patch. |

## Next research tests before a decision

1. Review the scoped-task RFC before choosing syntax or an implementation
   slice; keep detached `.start` unchanged.
2. Review the owned `Map<I,F>` RFC before adding traits; keep borrowed/lending
   adapters out of the first slice.
3. Decide separately whether stable diagnostic-code cascades should be reduced;
   the current implementation intentionally preserves them.
4. Add one parser/view parity case only if it is a real open question; current
   repository evidence already rules out a blanket "Toka has no zero-copy view"
   claim.
5. Treat dynamic `RefCell`-style borrowing as an exploration only; it is not a
   demonstrated Toka gap because static `field#`, `Mutex`, and `RwMutex` already
   cover different interior-mutability policies.
6. Audit whether the accepted static async-trait form should be qualified and
   frozen, or deliberately rejected until an async-interface RFC closes its
   TKI, cancellation, visibility, and dyn boundaries.

The design candidates are recorded in
[`scoped_borrowed_task_rfc.md`](../../docs/semantic_core/scoped_borrowed_task_rfc.md)
and
[`owned_lazy_iterator_rfc.md`](../../docs/semantic_core/owned_lazy_iterator_rfc.md).
The dynamic-borrowing question is tracked separately in
[`dynamic_borrowing_exploration.md`](../../docs/semantic_core/dynamic_borrowing_exploration.md).
The already drafted post-1.0 resource-cleanup RFC is
[`droptime_spec.md`](../../docs/droptime_spec.md); its cleanup guarantee is
normal-exit-only and therefore does not change the panic comparison boundary.

## Current reproducibility status

```bash
bash research/rust-comparison/run_expressiveness.sh
bash research/rust-comparison/run_diagnostics.sh
bash research/rust-comparison/run_semantics.sh
bash research/rust-comparison/run_ergonomics.sh
```

All four commands passed when this summary was written.  They use the local Toka
compiler and `rustc` from `PATH`; no network, GitHub Action, or benchmark suite
is involved.
