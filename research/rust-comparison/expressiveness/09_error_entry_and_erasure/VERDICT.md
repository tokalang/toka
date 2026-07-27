# Verdict: fallible entry point and error erasure

## Observed current boundary

The Rust program returns `Result` from `main` and uses `Box<dyn Error>` to
erase a concrete standard-library error at the entry boundary.  It uses only
`std`; the `?` conversion is part of Rust's ordinary error trait ecosystem.

Toka's passing baseline is intentionally not a claim that it lacks typed
errors.  It returns an ordinary `Result<i32, AppError>` from a helper and
handles the typed error explicitly at an `i32` entry point.  Toka also supports
whole-binding `!` propagation and one explicit `@ErrorInto` conversion in
Result-returning helpers.

Toka 1.0 rejects `main -> Result<...>` with `E04596` and deliberately has no
universal `dyn error`, conversion-chain search, or implicit error erasure.

## Classification

This is an **error-model and entry-protocol tradeoff**, not a character-count
comparison between `?` and `!`, and not a PAL limitation.  Rust gains a common
application/CLI exit path that can absorb heterogeneous errors.  Toka retains
typed errors and makes the termination decision explicit at the entry boundary.

## Extension boundary

`main -> Result` and universal error erasure are separate decisions.  Either
would need a dedicated RFC for exit-code mapping, diagnostics, cleanup-error
precedence, async main, error ownership, dyn ABI, and `.tki` representation.
Neither should be added as an implicit widening of `@ErrorInto`.
