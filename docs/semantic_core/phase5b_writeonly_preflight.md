# Phase 5B Writeonly Preflight

Phase 5B performs the bounded feasibility check for LLVM `writeonly`. It does
not add a writeonly compiler flag or alter the memory-summary lattice.

## Workload

The provider writes a field through a mutable `Payload` parameter and returns a
constant, so the source operation itself does not intentionally consume the
stored value:

```sh
python3 tools/scripts/audit_writeonly_preflight.py
```

The deterministic report is internal schema
`toka.writeonly-preflight` version 1.

## Result

The current summary for `write_payload.data` is:

- root effects: `read`, `write`;
- `writeonly` decision: `Reject`; and
- rejection reason: `ReadsMemory`.

The field assignment is conservatively modeled as reading the containing root
as well as writing it. Consequently there is no valid `writeonly Candidate`, no
trusted-cache contract to activate, and no meaningful optimizer or runtime
benchmark to run. The compiler correctly rejects
`--experimental-memory-contracts=writeonly` as unsupported.

## Stop Decision

This preflight stops before LLVM emission. Fixing the result would require a
new memory-summary precision project that distinguishes address formation and
field stores from memory reads. That is a semantic-analysis expansion, not a
small backend bridge, and there is no current evidence that it would produce a
useful optimization.

Therefore `writeonly` remains shadow-only and is not pursued further in this
direction. `noalias` remains the final independent soundness task; it is not
started by this preflight.
