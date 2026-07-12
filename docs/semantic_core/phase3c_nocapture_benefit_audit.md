# Phase 3C-5 Nocapture Benefit Audit

Phase 3C-5 determines whether the experimental `nocapture` bridge has enough
measured value to justify default emission. It changes no language rule, TKI
trust boundary, or default compiler behavior.

## Method

The audit first proves that experimental emission is active at `-O0`, then
compares default and experimental optimized LLVM IR at `-O1`, `-O2`, `-O3`,
`-Os`, and `-Oz`. Argument ordering is fixed so command-line text cannot alter
the module identity.

When optimized IR differs, both variants are compiled to objects and compared
using normalized `llvm-objdump` disassembly. Runtime measurement is performed
only when machine code differs.

```sh
python3 tools/scripts/audit_experimental_nocapture.py
python3 tools/scripts/audit_experimental_nocapture.py --full
python3 tools/scripts/audit_experimental_nocapture.py --full --benchmark
```

The first two commands emit deterministic internal JSON with schema
`toka.nocapture-benefit-audit` version 1. The optional benchmark adds measured
timings, which are necessarily environment-dependent. This schema is evidence
for compiler maintenance, not a public tooling ABI.

## Static Results

The recorded audit used LLVM 20 on arm64 macOS. It compares 313 positive
programs at five optimization levels, for 1,565 default/experimental pairs:

| Level | Identical IR | Different IR | Different machine code |
| --- | ---: | ---: | ---: |
| `-O1` | 306 | 7 | 1 |
| `-O2` | 305 | 8 | 1 |
| `-O3` | 305 | 8 | 1 |
| `-Os` | 304 | 9 | 1 |
| `-Oz` | 302 | 11 | 1 |

Every machine-code difference is in `tests/pass/g10_websocket.tk`. The
experimental contract reduces the `ws_accept_async` coroutine frame from 1320
to 1304 bytes. Object size changes are:

| Level | Default | Experimental | Reduction |
| --- | ---: | ---: | ---: |
| `-O1` | 60208 | 60000 | 208 bytes |
| `-O2` | 60976 | 60768 | 208 bytes |
| `-O3` | 60000 | 59776 | 224 bytes |
| `-Os` | 58832 | 58768 | 64 bytes |
| `-Oz` | 56408 | 56360 | 48 bytes |

The other IR differences produce identical normalized machine code. Existing
benchmarks that are pointer-free or cross unsafe/raw boundaries are excluded
because they cannot exercise an emitted `nocapture` contract.

## Runtime Result

The targeted benchmark executes five million fast-failing invocations of the
affected WebSocket coroutine without requiring a working network. It verifies
the 1320-to-1304-byte frame change and alternates default and experimental
executables to reduce ordering bias.

A seven-iteration `-O2` audit measured medians of 1,128,229,875 ns for default
and 1,121,677,625 ns for experimental, a 0.581% reduction. This is below the
predeclared 2% stable-improvement threshold and is not treated as a runtime
performance claim.

## Decision

The attribute has a real but narrow static benefit: a 16-byte smaller
coroutine frame and a small object-size reduction in one workload. No stable
runtime benefit is demonstrated, and same-module LLVM optimization already
infers equivalent contracts for most programs.

The decision is therefore `KeepExperimental` with reason
`NoStableRuntimeBenefit`. Default emission remains disabled. Source-less TKI
continues to reject body-derived contracts, so no uncheckable optimization
promise crosses the interface trust boundary.

This evidence decision completes Phase 3C. Default enablement, if reconsidered
after future inter-module or backend changes, requires a new audit rather than
reinterpreting this result.
