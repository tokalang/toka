# Phase 5A Bounded Readonly Contract

Phase 5A is one finite experiment for LLVM's parameter `readonly` contract.
It is not an open-ended effort to complete all memory optimizations.

## Scope

- add one explicit experimental `readonly` emission mode;
- consume only checked source summaries or validated compiler-cache evidence;
- preserve ordinary source-less TKI, missing-cache, stale-cache, tampered-cache,
  and disabled-PAL behavior;
- test direct source and separately compiled provider/consumer paths; and
- compare optimized IR, machine code, and runtime behavior using the same audit
  method as Phase 4D.

This phase changes no Toka syntax, PAL rule, ownership rule, async rule, or
ordinary TKI format. `writeonly` and `noalias` are outside this phase.

## Completion Gate

Phase 5A is complete when the following are all reproducible:

1. `readonly` is emitted only behind its explicit experimental flag and only
   for a `Candidate` record whose reason identifies source or trusted-cache
   evidence.
2. Default, disabled-PAL, ordinary source-less, missing-cache, invalid-cache,
   and tampered-cache paths emit no `readonly` attribute.
3. Positive and negative source cases cover reads, writes, rebinding,
   invalidation, transfer, escape/capture, raw/unsafe, async, unknown-call,
   generic, and ABI boundaries.
4. Direct and cross-module replay preserve behavior at `-O0`, `-O2`, and
   `-O3`; the full positive and negative suites remain green except for their
   recorded environment-only failures.
5. Optimized artifacts are compared at `-O1`, `-O2`, `-O3`, `-Os`, and `-Oz`.
   Runtime measurement is required only when machine code differs.

The cross-module audit is run with:

```sh
python3 tools/scripts/audit_cross_module_readonly.py
python3 tools/scripts/audit_cross_module_readonly.py --benchmark
```

## Audit Result

On LLVM 20 / arm64 macOS, the cross-module fixture produced these results:

| Level | Default object | Experimental object | Machine code |
| --- | ---: | ---: | --- |
| `-O1` | 9288 | 9288 | identical |
| `-O2` | 9496 | 9464 | different |
| `-O3` | 9216 | 9184 | different |
| `-Os` | 9208 | 9192 | different |
| `-Oz` | 10008 | 10008 | identical |

The default path emitted no attribute. The experimental path emitted one
`readonly` contract with reason `ProvenByTrustedCache`. Missing, disabled-PAL,
ordinary source-less, and tampered-cache paths remained conservative.

The targeted workload executes 50 million calls across the separately compiled
provider boundary. Seven alternating `-O2` runs measured medians of
1,614,437,750 ns for default and 1,598,058,208 ns for experimental, a 1.015%
improvement. This is below the 2% stable-improvement threshold.

The phase decision is therefore `KeepExperimental / NoStableRuntimeBenefit`.
`readonly` has demonstrated a real cross-module optimizer effect, but this
bounded cycle stops here. No default emission or further readonly-specific
analysis expansion is justified by this result. The next independent
direction may be considered separately; `writeonly` remains after readonly and
`noalias` remains last.

## Stop Decision

The cycle stops after one complete audit. It does not keep expanding the
summary lattice or adding ad hoc exceptions to increase the candidate count.

- If no stable runtime or code-size value is demonstrated, retain
  `readonly` as experimental, record the result, and move to another compiler
  direction.
- If a real benefit is demonstrated, retain the experimental default and
  record the result; default enablement requires a separate rollout decision.
- If any proof or trust-boundary case is unsound or ambiguous, reject the
  affected contract and stop the cycle rather than weakening the boundary.

The working threshold for a positive runtime result is a median improvement of
at least 2% on a repeatable targeted workload. This threshold is a stopping
criterion, not a claim that a small unmeasured improvement has no value.

`writeonly` may be considered only after this bounded `readonly` cycle has a
recorded conclusion. `noalias` remains the final independent soundness task.
