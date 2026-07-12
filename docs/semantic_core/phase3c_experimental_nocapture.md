# Phase 3C Experimental Nocapture

Phase 3C-4 enables one backend contract class behind an explicit experimental
flag:

```sh
--experimental-memory-contracts=nocapture
```

Default compilation does not emit the attribute. `writeonly` and `noalias`
remain shadow-only; `readonly` is handled by the bounded Phase 5A experiment.

## LLVM contract

LLVM 20 defines parameter `nocapture` as a guarantee that the callee does not
capture the particular pointer copy passed through that parameter. It does not
prevent the same address from being captured through another, unannotated
parameter. This per-parameter meaning matches Toka's root-specific memory
summary and does not require a pairwise non-alias claim.

Reference:

- <https://releases.llvm.org/20.1.0/docs/LangRef.html#parameter-attributes>

## Proof obligation

Emission is allowed only when the Phase 3C-3 shadow record is a `nocapture`
`Candidate`. Therefore all of the following must hold:

- the function has a checked source body;
- the source parameter maps to a pointer-valued LLVM parameter;
- borrow checking is enabled;
- the function has no suspend, unsafe, raw, or unknown boundary;
- the parameter root has no `capture`, `escape`, `transfer`, or `unknown`
  effect; and
- LLVM CaptureTracking independently reports no return, store, or call capture
  in a private clone of the unannotated generated IR after promotable local
  stack slots are removed; and
- every AST declaration sharing the LLVM symbol independently satisfies the
  same candidate test.

Signature-only TKI declarations, async functions, cede/ownership transfer,
returned references, closure/global escape, raw pointers, unsafe code, and
unknown calls fail the gate.

## Emission and verification

The pipeline is deliberately ordered:

1. compute and structurally verify memory summaries;
2. generate and verify unannotated LLVM IR;
3. clone the IR, promote analysis-only local stack slots, then compute and
   verify shadow contracts while asserting zero frontend emission;
4. if the experimental flag is present, emit `nocapture` for Candidate records;
5. verify that emitted attributes exactly equal the Candidate set; and
6. run the LLVM module verifier again before optimization.

Memory-contract JSON schema version 3 includes an `emitted` boolean and the
later `ProvenByTrustedCache` reason. This makes the distinction between proof,
rejection, and experimental emission deterministic and replayable.

## Tests

Focused and full positive-corpus verification:

```sh
python3 tools/scripts/test_experimental_nocapture.py
python3 tools/scripts/test_experimental_nocapture.py --full
```

The tests prove:

- default IR remains unannotated;
- direct read, direct write, non-escaping store-target, and generic candidates
  are emitted;
- calls through parameters without an already proven LLVM `nocapture`
  contract are conservatively rejected by the independent IR gate;
- cede, returned-reference escape, pointer storage, pointer-address exposure,
  raw/unsafe, async, disabled-PAL, and TKI-only boundaries are not emitted;
- unsupported experimental contract names are rejected;
- default and experimental executables agree at `-O0`, `-O2`, and `-O3`; and
- every source in `tests/pass` compiles with the flag.

## Non-goals

This phase does not enable `nocapture` by default and makes no performance
claim. Phase 3C-5 subsequently audited its real optimization effect and kept
the attribute experimental, as documented in
`phase3c_nocapture_benefit_audit.md`. It also does not weaken the separate
final gate for `noalias`.
