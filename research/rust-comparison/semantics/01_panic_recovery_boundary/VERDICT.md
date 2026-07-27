# Verdict: panic recovery boundary

## Observed current boundary

The Rust program runs under Rust's default unwinding configuration.  It catches
the panic with `std::panic::catch_unwind`, observes an error result, and proves
that a local `Drop` implementation ran during unwinding.  This is not a claim
about Rust builds configured with `panic = "abort"`, where recovery is not
available.

The Toka program compiles but terminates the process at `panic`; the comparison
runner treats a normal return as failure.  Toka 1.0 specifies this as a
non-returning process-termination boundary.  It is neither a catchable
exception nor a promise of stack unwinding or cleanup after the panic point.
See [`docs/syntax.md`](../../../../docs/syntax.md) and
[`fz4_public_contract_freeze.md`](../../../../docs/semantic_core/fz4_public_contract_freeze.md).

## Classification

This is a **semantic/runtime tradeoff**, not a PAL expression comparison.
Toka's contract is simpler and predictable for fail-fast runtime faults.  Rust
can support test harnesses, plugin containment, and selected recovery flows in
an unwind-enabled build.  Neither program establishes a general reliability or
security ranking.

## Deliberate non-proposal

This case does not propose `catch_panic` for Toka.  Any such feature would need
an independent specification for cleanup, ownership state, FFI boundaries,
async cancellation, and the distinction between language panic and process
failure.  It must not be introduced as a library-only convenience that silently
changes the 1.0 panic contract.
