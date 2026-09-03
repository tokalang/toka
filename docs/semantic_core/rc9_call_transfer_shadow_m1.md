# RC9 M1 Call Transfer Shadow

**Status:** M1a.2 shadow isolation and base-carrier qualification implemented.
Exact-place/dependency admission and atomic commit are not implemented.
Non-normative and not an activation of the signature-driven call-transfer ADR.

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
toka.internal.call-transfer-shadow / version 4
```

It is not cede obligation evidence v2 and has no public compatibility promise.
It exists to qualify the planner before any behavior or public evidence change.
It cannot be combined with another JSON, semantic, or evaluation output mode.
Version 4 retains the version-3 RC9 fields and adds a nested audit-only Stage-0
plan produced by the Accepted explicit-cede pure classifier. The legacy fields
remain the behavior oracle; Stage 0 neither changes diagnostics nor grants
CodeGen authority. Version 3 originally replaced the M1a.1 schema and retains
structured source/referent
identity, stable indices, value category, drop disposition, dependency paths,
and cleanup-mask capacity, and adds actual `init` spelling. Postfix value
operations, `T | miss` payloads, and thread boundaries now use qualified
semantic facts rather than syntax/name shortcuts.

## Recorded dimensions

Each record is created after a call route resolves its formal parameter:

```text
route             ordinary | static | method | callable | extern
                  | indirect-fn | indirect-dyn-fn | dynamic-trait-method
argument/formal   stable one-based original indices
value_category    Place | Temporary | InitStorage | Indeterminate
spelling          implicit | explicit
init spelling     formal_init and actual_init are independent facts
transfer          BorrowCapture | CopyValue | CopyIdentity
                  | TransferShared | MoveOwned | ConsumeTemporary | Reject
source            KeepLive | InvalidatePlace | NoSourcePlace | NoStateChange
dependency        None | Borrowed | RawUnsafe | Indeterminate | Unclassified
place_eligibility NotApplicable | PendingValidation | Eligible | Reject
drop               SourceRetainsLiability | DestinationAssumesLiability
                   | NoLiability | NoStateChange | PendingValidation
execution_boundary None | StartHandoff | ThreadHandoff
                   | StartAndThreadHandoff
source_identity    structured AccessPath plus display path
referent/deps      PAL referent and lifetime dependency paths
cleanup_mask       null until an admitted partial cleanup plan exists
legacy            cede exemption and missing-explicit-cede facts
effect             async fact
stage0             outcome/rejection, independent value/source/destination/
                   drop/obligation dimensions, exact SourceView and
                   reachability, deterministic semantic root when proven
```

Every Stage-0 rejection carries `source=NoStateChange`. A place whose stable
declaration coordinate cannot yet be recovered rejects `IncompleteFacts`
rather than falling back to the legacy process-local `source_root_id`.

`argument_index` and `formal_index` are one-based and remain stable when
`@Callable` lowering inserts a synthetic receiver. Shadow plans are discarded
when an AST call is cloned and its resolved formal is reset.

## Deliberate M1 limitations

- `PendingValidation` is not permission to move. M1a.2 does not yet share the
  whole/partial exact-place eligibility checker with calls.
- A shape, smart pointer, array, or dyn callable whose complete dependency
  closure has not been routed into the planner is `Unclassified`, not
  dependency-free. Direct borrowed-view dependency paths are recorded
  separately from the source binding and PAL referent.
- The plan is produced per argument after the legacy checker has visited the
  expression. It is not yet the ADR's all-arguments prepare/validate/commit
  transaction.
- CodeGen continues to use `CedeExpr` and the existing aggregate transfer
  paths. It never reads `ShadowArgumentTransfers`.
- Existing RC8 thread-safety diagnostics still use their source-spelling
  classifier. M1a.2 records declaration-identity boundary facts but does not
  switch those diagnostics; a user same-named function therefore retains its
  historical diagnostic until a later admitted replacement.
- Consuming callable receivers remain outside argument planning and retain
  their existing `cede callable()` contract.
- Cede obligation evidence v1 is unchanged.

## Qualification

`tools/scripts/test_call_transfer_shadow_m1.py`, registered as
`toka_call_transfer_shadow_m1` in CTest, checks:

- named Copy place preservation and explicit invalidation;
- whole temporary consumption;
- ordinary, static, method, callable protocol, indirect `fn`, indirect
  `dyn fn`, dynamic-trait method, generic, and extern routes;
- legacy missing-`cede` facts without behavior changes;
- shared-handle transfer classification;
- audit/normal diagnostic parity for dynamic-trait calls and suppression of
  closure capture-precompute records and AST call lowering;
- async `.start`, nested non-boundary calls, and resolved-declaration thread
  handoff annotation, including aliases and a user same-named function;
- init formal/actual spelling, unknown actuals, unary/cast/address/postfix
  temporaries, exact source identity, borrowed-view and projected referent
  paths, recursive `T | miss` carriers, and multi-argument indices;
- source/source-less plan parity;
- forced check-only single-document JSON and fail-closed output conflicts;
- four normal-mode diagnostic/success receipts; and
- non-`cede` formal rejection with no source-state commit.

The script emits a compact receipt named
`toka.rc9-m1-call-transfer-shadow-audit`.

## Next admission step

M1a.2 does not authorize the caller-spelling behavior flip. The proposed
[`M1b-D.3 Ordinary Direct-Call Vertical Slice`](rc9_m1b_d3_direct_call_vertical_slice.md)
does not promote these AST vectors into commit authority or build a standalone
synthetic transaction engine. Current implementation remains limited to the
M1b.0a opaque identities and empty model. The next code slice, if independently
accepted, must exercise one real ordinary direct-call Shadow path.
