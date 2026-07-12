# Phase 4D Cross-Module Nocapture

Phase 4D activates validated cache summaries for the existing experimental
`nocapture` bridge and repeats the optimizer-benefit audit across a separately
compiled provider boundary. Default compilation remains unchanged.

## Activation Gate

Trusted evidence is activated only when all of these hold:

- `--experimental-memory-contracts=nocapture` is present;
- PAL is enabled;
- the resolver validated the build-cache `.tki/.tke/.o` tuple;
- every cached symbol and parameter root maps exactly to the checked interface;
- the cached root has no capture, escape, transfer, or unknown effect; and
- the LLVM declaration has a matching pointer-valued ABI parameter.

The active summary origin becomes `trusted_cache`. A resulting contract record
uses reason `ProvenByTrustedCache`; declaration parameters without LLVM names
are mapped by their verified parameter index. Default, disabled-PAL, missing,
tampered, and ordinary source-less paths retain `SignatureOnly` and emit
nothing.

The canonical evidence payload SHA-256 is retained as a marker in the backing
object. Import validates that marker in addition to the complete object digest,
so even structurally valid sidecar tampering cannot gain optimization trust.

## Audit Workload

The tracked workload separately compiles `read_payload`, then calls it on a
coroutine-local `Payload` before an await. Without cross-module `nocapture`,
LLVM must conservatively place that local in the coroutine frame and retain an
`observe.resume` path. Validated evidence keeps the local on the stack and
removes the resume function.

Run the deterministic static audit and optional runtime measurement with:

```sh
python3 tools/scripts/audit_cross_module_nocapture.py
python3 tools/scripts/audit_cross_module_nocapture.py --benchmark
```

The internal JSON schema is `toka.cross-module-nocapture-audit` version 1.

## Static Results

LLVM 20 on arm64 macOS produced different optimized IR and machine code at all
five levels:

| Level | Default object | Experimental object | Change |
| --- | ---: | ---: | ---: |
| `-O1` | 9288 | 9320 | +32 bytes |
| `-O2` | 9496 | 9200 | -296 bytes |
| `-O3` | 9216 | 8928 | -288 bytes |
| `-Os` | 9208 | 8976 | -232 bytes |
| `-Oz` | 10008 | 9992 | -16 bytes |

The `-O1` size increase prevents a blanket code-size claim. At the other four
levels the removed resume path gives a real static reduction.

## Runtime Result And Decision

The benchmark executes 50 million calls, performs one warm-up per variant, and
alternates seven measured runs. The recorded `-O2` medians were 1,553,046,417
ns for default and 1,539,659,625 ns for experimental, a 0.862% improvement.
This is consistent with a small gain but remains below the predeclared 2%
stable-improvement threshold.

The decision is `KeepExperimental` with reason `NoStableRuntimeBenefit`.
Cross-module evidence has demonstrated a real optimizer consumer and measurable
static effect, but it does not justify default enablement. Any future default
change remains a separate audit and rollout decision.

This completes Phases 4A through 4D. `readonly` and `writeonly` remain
shadow-only, and `noalias` remains last.
