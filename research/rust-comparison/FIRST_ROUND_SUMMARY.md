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
| Shared-write diagnostic | Quality comparison | Both reject invalid shared-field writes. Toka keeps its authority-specific diagnostic sequence and now adds a field-authority note; Rust gives one direct mutability message, whose suggested `&mut` is not always the desired interior-mutability repair. |

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

The design candidates are recorded in
[`scoped_borrowed_task_rfc.md`](../../docs/semantic_core/scoped_borrowed_task_rfc.md)
and
[`owned_lazy_iterator_rfc.md`](../../docs/semantic_core/owned_lazy_iterator_rfc.md).

## Current reproducibility status

```bash
bash research/rust-comparison/run_expressiveness.sh
bash research/rust-comparison/run_diagnostics.sh
```

Both commands passed when this summary was written.  They use the local Toka
compiler and `rustc` from `PATH`; no network, GitHub Action, or benchmark suite
is involved.
