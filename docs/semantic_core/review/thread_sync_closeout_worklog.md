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
  accepted checked private adapters. Managed constructors allocate an empty
  owner carrier, then assign the complete private-factory result into its
  payload using existing ordinary assignment lowering. No spread/new syntax or
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

## Verified so far

Incremental compiler build and diff check pass. Five directed targets pass:
factory source flow, sync storage subset, shared parameter ABI, native adapter
runtime/failure matrix, and native factory source/fault matrix (including the
new true-recursion rejection). Last combined run: 5/5, 75.53 seconds. The later
explicit-capture identity assertion also passed in its direct source-flow run.

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
- **Managed owner and shared-control-block allocation failures:** current
  public managed wrappers allocate the empty carrier before entering the
  factory. General `new` currently uses a panic null check and shared promotion
  has its own allocation. Do not claim those paths preserve the accepted input
  cleanup responsibility without a scoped plan and runtime fault tests. This
  is part of the already accepted failure contract, not a new allocator API.
- Unknown/raw storage escape and stale rebind must invalidate every related
  alias/capture, not leave an earlier Complete environment summary reusable.
- Source-hidden missing-contract rejection, witness/guard/replacement mismatch
  fault checks, and the actual end-to-end thread timing/cleanup matrix.
- Extend the verified source chain to RwMutex/CondVar and the already requested
  once/waitgroup/condvar callers, then run one integration/full comparison.

Do not solve the outstanding callback failure by treating Mutex, @Send,
ClosedPayload, or absence of roots as an independently-owned environment.
