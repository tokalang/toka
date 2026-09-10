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

## Latest checkpoint — composite implementation authorized and connected

The user explicitly approved all five composite allocation/cleanup boundaries.
The earlier approval blocker is closed; the historical blocked notes below do
not describe the current state. No new Accepted/freeze is claimed.

- An actual prepared local composite selects the pre-allocation snapshot.
  Its complete declaration/field types, source liveness and PAL are checked;
  every native child needs its own existing factory/cleanup qualification.
  Other admitted fields are closed primitives. Custom composite Drop, partial,
  unknown, contradictory or nonempty native targets are rejected.
- A sealed allocation plan records every field and the native child cleanup
  witness. CodeGen validates declaration/layout/field correspondence and only
  field-drops the live prepared source on allocation failure, freeing an empty
  partial target separately. There is no whole-drop plus field-drop path.
- Once/WaitGroup keep their unique `make()` APIs and now have explicit
  `make_shared()` wrappers. The latter declare the mutable payload view used by
  their existing methods; no implicit unique/shared conversion was introduced.
  Their checked public methods forward to private source-owned composition
  operations with exact owner/argument contracts. Field-specific native
  witnesses remain necessary; a method/type name or @Send alone grants none.
- The actual condvar, Once and WaitGroup examples use explicit shared factories,
  complete capture spelling and the current spawn Result/JoinHandle API. Once
  still makes three attempts and asserts exactly one initializer; WaitGroup
  waits for all three workers, checks their result and joins them. The local
  Once initializer borrows within its worker, never across the thread boundary.
- Nested local borrowed native views retain their owner relation without
  becoming owned environment evidence. Rechecking a method replaces its old
  access plan, preventing stale speculative access carriers from surviving.
  No capture layout, runtime ABI, refcount algorithm or language syntax changed.

Verified so far: the public thread/sync runtime/parity/denial set passes
**19/19, no skips**. Prepared-composite success/failure runs pass **10/10**
(unique/shared Once and WaitGroup; outer allocation and shared-control-block
failure). Composite-specific missing/mismatched field-plan tests pass **26/26**
without object/IR artifacts. Existing public-owner and witness CTests pass.
The nonempty native-child rejection also passes both object/IR checks with
the prepared source restored (**2/2**, no E0438/E0410). The final composite
CTest rerun passed in 34.45 seconds.
These are directed results, not a fresh full PASS/FAIL or baseline comparison.

The delivery remains open at **managed-slot replacement**, then the final
relative-`eedd0bf0` verification. The required positive investigation fixture
`tests/semantics/std_thread_handoff/sync_managed_slot_replace.tk` now diagnoses
the exact mismatch: bare `slot` assignment has selected `Token#`, while the
initialized element contract is `^Token`. It is not a successful handle
replacement and has not been turned into a permanent negative. Existing
`^/&` selector and morphic probes are also retained; no permission/type check
was relaxed to make one of those spellings pass. The 19/19 set does not include
or claim this still-open row.

All remaining work is within the existing thread/sync delivery; unrelated
network/container/binding debts stay outside. No push, PR, Actions or frozen-ref
movement is part of this checkpoint.

## Earlier checkpoints (historical)

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

**The actual `sync_thread_pending.tk` now compiles and runs successfully,
unchanged.** This proves the first delivery scenario, not the full integration.
No full suite, acceptance, push or PR has occurred.

## RwMutex / CondVar continuation

Public RwMutex factory/read/write/drop and CondVar factory/wait/notify/drop now
delegate to the accepted private adapters. CondVar keeps its unique `make()`;
an explicit `make_shared()` wrapper uses the same checked allocation protocol.
The rejected unique-to-shared-promotion proposal was not applied (the current
Stage 1 source language rejects that older spelling).

Rw witnesses distinguish read and write acquisition/access/discharge, and read
slots receive no replacement authority. CondVar witnesses describe native-only
storage, not a fabricated payload. A witnessed wait requires the same-type
live Mutex guard and no outstanding slot loan across the wait.

The original `g09_sync_condvar.tk` is migrated to explicit handle capture,
the current spawn Result API, and bounded borrow scopes around wait; it runs
successfully. `sync_thread_rw.tk` also runs successfully. The closeout runner
passes 16/16 checks. The owner failure matrix passes 77 runtime cases; the
witness matrix passes 40 no-artifact faults and 8 strict-parity denials.
The relevant CTest set passed 9/9 in 216.06 seconds. A subsequent targeted
source-hidden provider TKI/object test also passed: a consumer without the
native storage witness rejects with no artifact instead of trusting type-only
interface metadata. No full-suite comparison was run at this checkpoint.

A shared copy inside a closure exposed a preflight target bug: a capture probe
had cached only the inferred soul in TypeName. The target now recomposes the
unchanged binding-side morphology using the existing typed constructor. The
actual shared copy is retained; no source example bypasses that path.

Once/WaitGroup composite allocation cleanup is blocked by automatic review,
not by a new language-design requirement. See the precise
[implementation authorization boundary](native_composite_allocation_authorization.md).
Their non-authorizing child recipes may be recorded, but no composite witness
or cleanup grant has been enabled. No full suite or final candidate is claimed.

Awaiting-approval investigation: the additional whole unique/shared-element
replacement matrix was probed without modifying compiler rules. Current
reference/handle target spellings reject (`^&^T`/reference-target mismatch or
insufficient write capability); this does not prove a valid replacement route.
The exploratory source is preserved in `managed_slot_replacement_probe.tk`,
outside the executable gate list because its source contract is not established.
The required matrix row remains open—direct-value replacement passing must not
be reported as proof that managed-handle replacement passes. No permission,
source-view, or cleanup rule was relaxed during this investigation.

## Continue from the actual program, not another helper milestone

The Mutex witness now combines the exact owner recipe with completed factory,
managed-allocation, owner cleanup, lock, guard cleanup and guard-access source
contracts, plus ClosedPayload. Callable environment and public thread plans
carry the same witness, and stale/escaped owner recipes invalidate stored
environment summaries. CodeGen validates source plans and witnessed access.

The first end-to-end run exposed a captured-shared receiver bug: the closure's
carrier address was passed to ordinary payload `self`. The fix extracts the
payload from the already evaluated, physically typed shared capture; no capture
layout or refcount/ABI change was made. A small non-native shared-capture
receiver regression also runs successfully.

Guard results and `borrow_mut` references now carry the acquisition relation.
Complete whole-slot replacements use a matching Sema plan and reclaim the old
element after RHS preparation. The resource replacement thread regression
checks one drop on replacement and a second on final owner cleanup and passes.
Ordinary references did not receive ownership or a generic drop exemption.

Directed source/CodeGen witness checks pass: 30 no-artifact faults and six
strict-parity denials. Of the source denials, forged/private-field access is
stopped by E0418, and borrowed `str` currently stops at the earlier E0454
constructor return-dependency boundary; these are not misreported as witness
diagnostics. Handle escape, post-capture invalidation, and unknown call exposure
exercise witness invalidation. The expanded runtime/denial closeout gate is
14/14 without skips.

Witness/slot regression run: 9/9 relevant CTest targets passed in 173.56 seconds.
This includes all preceding allocation/source gates as well as the real thread
program and new witness denial matrix. No full PASS/FAIL run yet; RwMutex,
CondVar, once/waitgroup and the final relative-baseline audit remain required.

Mandatory outstanding checks include:

- Complete successful factory/managed-owner publication, matching terminal
  owner cleanup and guard acquisition/discharge/slot replacement. Check all
  actual input/environment dependencies; ClosedPayload alone is insufficient.
  The first Mutex path and direct-resource replacement now pass; additional
  unique/shared-element replacement, rollback, and native-owner compositions
  remain part of the required matrix, not implicitly covered by those results.
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
