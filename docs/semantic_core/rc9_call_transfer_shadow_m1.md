# RC9 M1 Call Transfer Shadow

**Status:** Audit-only implementation foundation. Non-normative and not an
activation of the signature-driven call-transfer ADR.

**Baseline:** `a3de6d4787f22f2b003949e62bf9aa1c83b40d17`.

M1 records a resolved-formal call plan beside the existing RC8 call semantics.
Normal Sema, PAL, diagnostics, cede obligation evidence v1, and CodeGen do not
consume this plan. In particular, `E04570` remains active and an implicit
owning call does not mutate source state.

Shadow computation itself is enabled only by the audit command. Ordinary
compilation and every existing evidence mode do not run the planner or populate
its Copy/dependency caches as a side effect.

## Audit command

```text
tokac --call-transfer-shadow=json --check-only source.tk
```

The command emits the internal, audit-only schema:

```text
toka.internal.call-transfer-shadow / version 1
```

It is not cede obligation evidence v2 and has no public compatibility promise.
It exists to qualify the planner before any behavior or public evidence change.
It cannot be combined with another JSON, semantic, or evaluation output mode.

## Recorded dimensions

Each record is created after a call route resolves its formal parameter:

```text
route             ordinary | static | method | callable | extern
spelling          implicit | explicit
transfer          BorrowCapture | CopyValue | CopyIdentity
                  | TransferShared | MoveOwned | ConsumeTemporary | Reject
source            KeepLive | InvalidatePlace | NoSourcePlace | NoStateChange
dependency        None | Borrowed | RawUnsafe | Indeterminate | Unclassified
place_eligibility NotApplicable | PendingValidation | Eligible | Reject
legacy            cede exemption and missing-explicit-cede facts
boundary          async and .start/thread handoff facts
```

`argument_index` is one-based. `source_path` is the canonicalized legacy path
display for an addressable source.

## Deliberate M1 limitations

- `PendingValidation` is not permission to move. M1 does not yet share the
  whole/partial exact-place eligibility checker with calls.
- A shape whose nested dependency closure has not been routed into the planner
  is `Unclassified`, not dependency-free.
- The plan is produced per argument after the legacy checker has visited the
  expression. It is not yet the ADR's all-arguments prepare/validate/commit
  transaction.
- CodeGen continues to use `CedeExpr` and the existing aggregate transfer
  paths. It never reads `ShadowArgumentTransfers`.
- Consuming callable receivers remain outside argument planning and retain
  their existing `cede callable()` contract.
- Cede obligation evidence v1 is unchanged.

## Qualification

`tools/scripts/test_call_transfer_shadow_m1.py` checks:

- named Copy place preservation and explicit invalidation;
- whole temporary consumption;
- ordinary, static, method, callable, generic, and extern routes;
- legacy missing-`cede` facts without behavior changes;
- shared-handle transfer classification;
- async `.start` boundary annotation; and
- non-`cede` formal rejection with no source-state commit.

The script emits a compact receipt named
`toka.rc9-m1-call-transfer-shadow-audit`.

## Next admission step

M1 does not authorize the caller-spelling behavior flip. The next step is to
replace `PendingValidation` and `Unclassified` with shared exact-place and
dependency decisions, then prepare all argument plans before atomically
committing any PAL, `PlaceState`, or drop-liability change.
