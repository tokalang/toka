# Thread/sync closeout — internal work log, not an acceptance request

Delivery boundary fixed by the user on 2026-09-10:

1. Run the existing `sync_thread_pending.tk` unchanged: shared Mutex capture,
   worker lock/write, join/read, final-owner cleanup.
2. Complete the accepted thread/sync responsibility matrix and necessary
   condvar/once/waitgroup migrations.
3. Submit one integrated candidate with required thread/sync gates green and
   no added/expanded failures relative to the `eedd0bf0` integration baseline.

Unrelated network/container/binding debt and the rest of the cede RFC are not
part of this delivery. Intermediate commits are internal checkpoints only.

## Current work

- Public Mutex new/make/make_shared/lock/drop and guard drop now delegate to the
  accepted checked private adapters. Managed constructors first prepare the
  complete private-factory result, then allocate an empty owner carrier and
  move that prepared owner into its payload. No spread/new syntax or
  capture/refcount-layout change was introduced.
- Preparing the native factory return nominal before entering its Unchecked
  body cache avoids eager impl checking being mistaken for body recursion.
  The existing Unchecked rejection is unchanged; an actual recursive private
  factory is explicitly tested to reject without an artifact.
- The one old null-allocation oracle for the migrated public factory now
  expects the accepted `_Exit(134)` fatal instead of SIGABRT. Other source
  positives, rejection codes and normal/shadow parity are unchanged.
- Private, non-authorizing owner recipes now track the actual private factory,
  managed allocation, return provider and caller edge. A shared copy retains
  the same owner recipe; different caller sites get different owner edges.
  Snapshot/merge/rollback and storage-identity invalidation carry these recipes.
- Real-source assertions verify `Mutex<i32>::make_shared` -> shared copy ->
  explicit capture preserves that same recipe. Captures keep it in body-local
  facts, not in the runtime capture layout. Pending recipes do not set a
  factory plan Validated bit or grant callable/thread/guard authority.
- Managed Mutex allocation now carries a Sema plan tied to the exact new site,
  binding, checked definition and prepared source. The source must already be
  live before allocation; only the reviewed empty carrier may use this cleanup
  path. Definition/drop completion is required before sealing. CodeGen checks
  the plan separately at owner and shared-control-block allocation. On failure
  it cleans the prepared native owner and frees any empty partial carrier,
  then uses the accepted `_Exit(134)` policy. Existing drop/refcount algorithms
  and the ordinary non-native allocation path are unchanged.
- Additional qualification rejection restores the exact assignment-entry
  snapshot. A missing recipe on that same controlled initialization edge now
  rejects explicitly, rather than silently bypassing the new check after an
  earlier failed specialization.

## Verified so far

Incremental compiler build and diff check pass. Five directed targets pass:
factory source flow, sync storage subset, shared parameter ABI, native adapter
runtime/failure matrix, and native factory source/fault matrix (including the
new true-recursion rejection). Last combined run: 5/5, 75.53 seconds. The later
explicit-capture identity assertion also passed in its direct source-flow run.

The public-owner gate independently passes 44 real success/failure cases:
unique/shared Mutex owners, scalar/resource/unique/shared payloads, remaining
shared payload owner, payload/native allocation and initialization failures,
outer owner allocation failure and shared-control-block allocation failure.
It also passes 28 owner/control-stage missing/mismatch no-artifact checks, strict
normal/shadow parity, and a nonempty-carrier rejection with source rollback.

Allocation-closeout regression run: 7/7 directed targets passed in 107.71
seconds (ClosedPayload, native factory flow, sync storage, shared ABI, private
adapters, factory source/faults, public owner). No full suite was run. This is
an internal regression checkpoint, not the integrated thread/sync candidate.

**The actual delivery program still rejects at callback initialization with
E04661 / IncompleteFacts. It has not been modified or marked an expected fail.**
No full suite, acceptance, push or PR has occurred.

## Continue from the actual program, not another helper milestone

The owner recipe reaches the explicit capture, but there is deliberately no
environment lifetime exemption yet. Complete the finite accepted witness before
letting `collectStage1CallableEnvironment` and public thread qualification accept
it. Both must consume the same exact relation; CodeGen must reject missing or
mismatched authority rather than independently deciding ownership.

Mandatory outstanding checks include:

- Complete successful factory/managed-owner publication, matching terminal
  owner cleanup and guard acquisition/discharge/slot replacement. Check all
  actual input/environment dependencies; ClosedPayload alone is insufficient.
- Extend the now-tested Mutex managed-allocation failure path to the remaining
  native owner wrappers as they are migrated. The passing Mutex matrix is not
  a claim that all RwMutex/CondVar constructors have already been migrated.
- Unknown/raw storage escape and stale rebind must invalidate every related
  alias/capture, not leave an earlier Complete environment summary reusable.
- Source-hidden missing-contract rejection, witness/guard/replacement mismatch
  fault checks, and the actual end-to-end thread timing/cleanup matrix.
- Extend the verified source chain to RwMutex/CondVar and the already requested
  once/waitgroup/condvar callers, then run one integration/full comparison.

Do not solve the outstanding callback failure by treating Mutex, @Send,
ClosedPayload, or absence of roots as an independently-owned environment.

Two broader alternatives were rejected by automatic review and not applied:
accepting arbitrary implicit Addr view casts in empty-carrier recognition, and
arming rollback for all assignments in a native constructor. The implementation
keeps strict literal recognition (the SDK writes `0:Addr#` where the field
declaration requires it) and the original exact-target entry snapshot. No
temporary debug instrumentation remains.
