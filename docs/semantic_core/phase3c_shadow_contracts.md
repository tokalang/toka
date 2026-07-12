# Phase 3C Shadow Memory Contracts

Phase 3C-3 adds an optimization-neutral consumer for the verified function
memory summaries. It evaluates backend attribute candidates against the actual
LLVM function ABI, but does not emit attributes or change generated code.

## Evaluated contracts

The shadow consumer evaluates every source parameter for:

- `nocapture`
- `readonly`
- `writeonly`
- `noalias`

Each record is either `Candidate` or `Reject` and carries a stable reason.
Candidates require a source-body summary, a matching pointer-valued LLVM
parameter, enabled borrow checking, and no suspend, unsafe, raw, or unknown
boundary.

The contract-specific gates are:

- `nocapture`: no capture, escape, transfer, or unknown root effect, plus no
  capture found by LLVM CaptureTracking on an analysis-only normalized IR
  clone;
- `readonly`: no write, rebind, invalidation, transfer, or unknown root effect;
- `writeonly`: at least one write and no read, invalidation, transfer, or
  unknown root effect;
- `noalias`: always rejected with `SeparateNoAliasGate`.

`noalias` is intentionally not inferred from unique ownership, cede, mutable
parameters, or PAL call compatibility. It remains a separate soundness task.

## Conservative boundaries

- Signature-only TKI declarations are rejected for body-derived contracts.
- `--disable-borrow-check` rejects PAL-dependent candidates.
- Async/suspending functions are rejected until coroutine-split ABI behavior
  has a separate proof.
- Raw, unsafe, unresolved, and unknown-call boundaries reject candidates.
- Non-pointer LLVM parameters are rejected because these parameter attributes
  would not be ABI-legal.
- Multiple AST declarations sharing one LLVM symbol are merged
  conservatively: every declaration must produce a candidate.

## Verification

The verifier recomputes the complete shadow analysis and requires exactly
equivalent records. It rejects duplicate symbol/parameter/contract keys and
scans every corresponding pre-optimization LLVM parameter to ensure that `nocapture`,
`readonly`, `writeonly`, and `noalias` were not emitted.

`--dump-memory-contracts=json` emits deterministic schema version 2 records,
including whether an experimental contract was emitted.
The mode is mutually exclusive with other JSON and evaluation modes.

Focused verification:

```sh
python3 tools/scripts/test_memory_contract_shadow.py
```

The focused suite covers source bodies, transitive calls, TKI-only imports,
raw/unsafe code, disabled borrow checking, generic instances, async functions,
shared LLVM symbols, object-code equivalence, and executable checks at `-O0`,
`-O2`, and `-O3`.

## Emission gates

Phase 3C-4 enables `nocapture` only behind an explicit experimental flag, as
documented in `phase3c_experimental_nocapture.md`. No performance claim is made
by the emission gate itself. Phase 3C-5 measures the resulting optimizer and
machine-code effects in `phase3c_nocapture_benefit_audit.md`; its decision is
to keep emission experimental. `noalias` remains last.
