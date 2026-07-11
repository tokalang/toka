# Phase 3C Memory Summary Core

Phase 3C-1/2 establishes an internal, conservative memory-effect summary for
every checked function. It is derived after semantic analysis and generic
instantiation, before LLVM IR generation. It does not change source-language
rules and does not emit optimization attributes.

## Summary lattice

Each parameter root records monotonic may-effects:

- `read`, `write`, `rebind`, `invalidate`
- `capture`, `escape`, `transfer`
- `unknown`

Each function records:

- `allocate`, `free`, `touch_global`
- `unknown_call`, `raw_provenance`, `unsafe_boundary`
- `suspend`, `unknown_boundary`

Local effects are retained separately from the interprocedural closure. Direct
calls are propagated to a fixed point, including recursive call graphs. Return
provenance follows declared lifetime and member dependencies.

## Conservative boundaries

- Source functions are analyzed from the typed AST.
- Source-less interface functions receive signature-only summaries and cannot
  claim body precision.
- Extern, unresolved, raw, and unsafe boundaries degrade affected roots to
  `unknown`.
- Disabling borrow checking degrades every memory-bearing parameter root to
  `unknown`.
- Ceded parameters always imply transfer, invalidation, and escape.
- Async functions record suspension, capture/escape of memory-bearing roots,
  and the coroutine-frame allocation required by lowering.
- Summaries are internal and are not serialized into ordinary TKI files.

## Verification and replay

The structural verifier checks root completeness and required implications.
Source-level allocation/free lowering carries internal IR metadata; the IR
verifier checks that tagged events have matching local summaries. It also
checks coroutine-state and frame-allocation evidence without interpreting
compiler-inserted cleanup as source effects.

`--dump-memory-summaries=json` emits deterministic schema version 1 output for
replay and debugging. It is an observational mode and is mutually exclusive
with other JSON/evaluation output modes.

Run the focused regression with:

```sh
python3 tools/scripts/test_memory_summary.py
```

Phase 3C-3 consumes these facts through optimization-neutral shadow contracts,
documented in `phase3c_shadow_contracts.md`. LLVM attribute emission remains
out of scope until each candidate class has its own proof and counterexamples.
