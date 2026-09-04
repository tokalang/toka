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

Assignment additionally prepares the destination `PlaceId`, exact view, and
reachability region from the same snapshot. An invalidating source is compared
with that destination before admission; self, ancestor, and descendant overlap
rejects as `DestinationOverlap + NoStateChange`.

`^owner` is classified as `IntrinsicUniqueMove` only for return, assignment,
and initialization. User-written `cede` remains source invalidation.
`NoSourcePlace` flows remain compiler-synthetic. A drop-bearing temporary uses
a deterministic non-call liability identity based on logical module,
destination, group site, edge/member name, stable slot, and temporary
expression coordinate, never a physical worktree path.

Aggregate members and explicit closure captures are prepared as whole groups
before any member/capture legacy check. Every item shares one snapshot and
group identity. The pure group planner validates every local plan and then
checks pairwise source/referent/dependency overlap. A duplicate source leaves
both local items `Live` while the group rejects atomically with
`NonCallGroupAliasConflict`; sequential legacy mutation is never treated as a
planner fact.

The non-call-only preflight elaborator resolves literals, variables with
hat-off payload access, calls, methods, constructors, `new`, common binary
expressions, and transparent cede/unsafe/postfix/ascription wrappers without
running `checkExpr`. Exact source identity is computed after transparent
wrappers are removed, while an ascription still supplies the produced type.
This keeps `cede payload` distinct from `cede ^owner` and keeps
`return ^owner:^T` an intrinsic unique move.

The generic candidate journal includes non-call records. A failed or cached
invalid specialization rolls its body records back with its other prepared
evidence; lack of a repeated diagnostic cannot publish a stale non-call plan.

## Qualification boundary

`tools/scripts/test_non_call_transfer_shadow_stage0.py` requires strict
normal/shadow diagnostic parity and one admitted explicit named-Copy source for
each destination. `tests/ExplicitCedePlan.cpp` independently checks all seven
non-call destination/context pairs, statement-end versus destination Drop
liability, storage/return/discard obligation discharge, bare Copy preservation,
and cross-route explicit-`cede NoSourcePlace` rejection.
The dynamic gate additionally covers assignment self-overlap, unique
handle/payload view separation, two drop-bearing aggregate temporaries with
distinct cleanup identities, aggregate/capture duplicate-source group
rejection, common read-only preflight expressions and wrappers, moved sources,
and invalid generic-body journal rollback.

The command records plans only. It does not commit source invalidation or
liability transitions and is not a destination-matching CodeGen plan. Missing
generic-body call qualification and snapshot-revision gaps retain the accepted
call-freeze status and are not widened by this slice.
