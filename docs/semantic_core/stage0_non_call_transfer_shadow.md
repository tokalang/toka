# Stage 0 Non-Call Transfer Shadow

**Status:** audit-only implementation slice. This does not activate the
Accepted explicit-call-boundary `cede` behavior and grants no CodeGen
authority.

**Call-transaction baseline:**
`fea743ba2379260187eec64e48d2a1cd341ade0e`.

The independent command

```text
tokac --non-call-transfer-shadow=json --check-only source.tk
```

emits:

```text
toka.internal.non-call-transfer-shadow / version 1 / audit-only
```

It cannot be combined with the frozen call-transfer v5 command or another
JSON/evaluation mode. Ordinary Sema, PAL, diagnostics, legacy ownership
mutation, CodeGen, TKI, ABI, and the compiler-interface key do not consume the
records.

## Covered destinations

The slice prepares one immutable source/PAL snapshot before legacy checking
for:

- standalone `ExprStmt(CedeExpr)` discard;
- return;
- assignment;
- binding initialization;
- aggregate member initialization, including variants;
- match target binding; and
- explicit `cede` closure capture.

Assignment and initialization remain distinct destinations even though the
roadmap groups them as one storage slice. Every route passes the same actual
type, exact source path/view, ownership, Copy proof, dependency, liveness,
cleanup, obligation, PAL, and source-authority facts to the frozen pure
planner. Non-call routes carry `FormalContract=None`; a destination type is a
type-compatibility fact, not a callee contract.

`^owner` is classified as `IntrinsicUniqueMove` only for return, assignment,
and initialization. User-written `cede` remains source invalidation.
`NoSourcePlace` flows remain compiler-synthetic. A drop-bearing temporary uses
a deterministic non-call liability identity based on logical module,
destination, and semantic source coordinate, never a physical worktree path.

## Qualification boundary

`tools/scripts/test_non_call_transfer_shadow_stage0.py` requires strict
normal/shadow diagnostic parity and one admitted explicit named-Copy source for
each destination. `tests/ExplicitCedePlan.cpp` independently checks all seven
non-call destination/context pairs, statement-end versus destination Drop
liability, storage/return/discard obligation discharge, bare Copy preservation,
and cross-route explicit-`cede NoSourcePlace` rejection.

The command records plans only. It does not commit source invalidation or
liability transitions and is not a destination-matching CodeGen plan. Missing
generic-body call qualification and snapshot-revision gaps retain the accepted
call-freeze status and are not widened by this slice.
