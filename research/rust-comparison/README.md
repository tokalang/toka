# Toka / Rust Comparison Lab

This directory is a reproducible comparison lab, not a benchmark or a claim
that either language is universally better.  Its two equal goals are to make
Toka's genuinely distinctive design choices concrete and to turn any revealed
weakness into a bounded Toka improvement candidate.  Each case uses actual code
in both languages and records what each language accepts, rejects, and makes
visible at the source level.

## Method

Every comparison case contains:

- a Toka program expected to compile and run;
- a Toka program expected to be rejected, where the rejection is material to
  the comparison;
- a Rust counterpart that is compiled with `rustc` when it is available;
- a short verdict that distinguishes an expressive difference from an
  ecosystem, tooling, safety, or performance claim; and
- a concrete improvement note when the comparison exposes a Toka gap.

The runner deliberately treats compiler acceptance and rejection as evidence.
It does not measure performance, infer soundness from one example, or compare
library ecosystem size.  Rust examples use only `std` unless a case explicitly
states otherwise.

## Claim taxonomy

Every conclusion must name its strength:

| Class | Meaning |
|---|---|
| **Observed distinction** | Both programs exist and run; they expose a real difference in source-level expression or API shape. |
| **Current 1.0 boundary** | A concrete Rust program exists and Toka 1.0 documentation plus a focused rejection/absence establishes that the corresponding protocol is not currently in the frozen surface. |
| **Design candidate** | A Toka-style extension has been sketched, but no implementation or proof exists yet. |
| **Model limitation** | Only allowed after evidence shows that no compatible Toka-style rule can express the program safely. |

The first two cases are **observed distinctions**.  No case in this repository
currently establishes a Toka model limitation.  In particular, missing scoped
borrowed tasks or consuming lazy iterators are current 1.0 boundaries, not
evidence that Toka needs user-written lifetime parameters or cannot gain those
protocols later.

## Run locally

From the repository root:

```bash
bash research/rust-comparison/run_expressiveness.sh
bash research/rust-comparison/run_diagnostics.sh
bash research/rust-comparison/run_semantics.sh
```

The runner requires a built `build/bin/tokac`.  It runs Rust counterparts only
when `rustc` is on `PATH`; a missing Rust toolchain is reported as `SKIP`, not
as a Toka result.

## Current scope

The first slice is deliberately narrow: source-level expression of authority.

| Case | Question |
|---|---|
| `01_handle_payload_permissions` | Can a program state handle-slot replacement and payload mutation independently? |
| `02_field_interior_mutability` | Can a program grant mutation to one field through a shared aggregate view without granting it to sibling fields? |
| `03_scoped_borrowed_concurrency` | Can a child task borrow a parent-local value when lexical scope guarantees join before parent exit? |
| `04_lazy_consuming_iterator` | Can an adapter own a consumed iterator and mutable callback state, then be consumed later? |
| `05_borrowed_iterator_baseline` | Can each language iterate through borrowed elements without consuming the collection? |
| `06_detached_non_borrowing_baseline` | Do both languages support ordinary detached work when input does not borrow its parent scope? |
| `07_dyn_associated_type_object` | Can a dynamically dispatched trait/object fix an associated output type at the erasure boundary? |

Semantic/runtime tradeoffs live separately from expression cases:

| Case | Question |
|---|---|
| `semantics/01_panic_recovery_boundary` | Can a panic be caught with cleanup, or is it an explicit process-termination boundary? |

Future dimensions should remain separate directories: diagnostics, generic
abstraction, async/concurrency, runtime profile, tooling, and ecosystem.  A
case should be added only with a concrete question and a falsifiable expected
outcome.

The cross-case candidate list is in [IMPROVEMENT_BACKLOG.md](IMPROVEMENT_BACKLOG.md).
It is deliberately a design backlog, not a claim that every item is a 1.0 bug
or should be implemented before release.

The current evidence and non-final ROI interpretation are summarized in
[FIRST_ROUND_SUMMARY.md](FIRST_ROUND_SUMMARY.md).
