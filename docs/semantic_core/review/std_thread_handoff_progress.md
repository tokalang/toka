# Public std/thread handoff integration

Date: 2026-09-09
Branch: `impl/std-thread-handoff-v1`
Base: `07ff495b465df77e3e7d1f950388c3c75d5c3351`
Status: implementation in progress — NOT a public-thread review candidate

Latest integration checkpoint:
[2026-09-09 sync migration and finite blocker inventory](std_thread_sync_closeout_2026_09_09.md).
The chronological implementation notes below do not supersede that full-suite
baseline or turn the thread/binding work into Accepted.

Next limited increment:
[managed guards and storage-contract review](thread_three_item_progress_2026_09_09.md).
This remains WIP; its required shared/thread positives are failing, not skipped.

The accepted private source subset is closed. This branch implements the public
thread responsibility chain; no additional private probe entry is introduced.
No previous freeze ref is moved. The unaccepted binding dependencies documented
at the base checkpoint remain unaccepted.

## Implemented backend deltas

- Unit now has an explicit adapter fact: invoke returns ABI void, result storage
  is canonical i8, no sret and no Drop. The adapter stores initialized zero after
  invoke; take moves that byte without allocation or user code. It rejects
  conflicting value-return, storage, sret and Drop tuples before module mutation.
- The same validated adapter emits `start_owned(packet, out_handle, native_code)`.
  Prepare failure destroys the still-owned packet; start failure disposes the
  prepared lease. Both return the status, and success transfers H without reading
  the packet/control afterward. This is not the private driver's fatal policy.
- `join_owned(inout_handle, uninitialized_destination, native_code)` uses the
  exact static ResultOps. Failure returns without touching H or the destination;
  success performs the existing no-failure lease move and returns success.
- Existing runtime declarations must match their exact C signatures before any
  adapter emission. These compiler wrappers add no runtime entry, registry,
  capture-layout change, reference-count algorithm or H/W state transition.

The LLVM/JIT adapter test contains 44 no-mutation rejection cases, actual runtime
Unit take/discard checks, and injected bridge prepare/start/join error/retry
checks. Waiting for packet cleanup before the bridge returns is tested; this
does not substitute for the independent B runtime's native-create scheduling
gate, nor for the requested future public-source scheduling matrix.

Directed verification: adapter plus the unchanged private-source CTest passed
2/2 (55.80 seconds). After adding consuming start-failure/success cleanup
controls, the adapter test passed again (1/1, 3.82 seconds). Incremental tokac
builds passed in both the testing build and the independent Release directory
configured with BUILD_TESTING=OFF. `git diff --check` passed. These are not fresh
full builds or a full regression run; no public source acceptance is claimed.

## Remaining implementation (not passed, not frozen)

1. Public source producer and exact carriers: thin/dynamic callable parameters,
   actual mutable/consuming invoke mode, state plus entry, complete environment
   dependencies and cleanup. Preserve existing thread positive cases; opaque or
   unproved facts cannot be upgraded merely from a signature or @Send.
2. Replace the old std/thread packet/id implementation with opaque H ownership,
   nonallocating ThreadError and Result-returning spawn/join/detach. Bind generated
   wrappers to the exact source edges; explicit operation failures retain H,
   while implicit drop uses the already accepted runtime fatal policy.
3. Qualify packet/allocation failure cleanup at the actual source boundary,
   result storage and unused returned Result cleanup. Do not let legacy thin-fn
   environment allocation bypass the new failure handling.
4. Public-source fault/schedule matrix, necessary call-site migration and
   source-hidden/new-old compatibility. Interface/cache rejection must accompany
   public activation; the current private ABI key is not a claim that legacy
   public JoinHandle interfaces can remain compatible after their replacement.
5. Directed convergence, then one full build/CTest/PASS/FAIL round, root-cause
   classification, and a single public responsibility-chain candidate review.

## Public source WIP following the backend checkpoint

The working implementation now replaces the library's old forged-environment /
native-id path with compiler-qualified public boundaries, Result/ThreadError,
an encapsulated two-word H/join-adapter handle, and join/detach/drop wrappers.
The producer checks canonical defining-module identity, full environment and
result facts, actual invoke mode and state/entry compatibility. The spawn
carrier parameter is inferred independently of T to avoid stripping consuming
mode through a single dynamic-function facade. Ordinary generic arity behavior
is unchanged outside the resolved thread intrinsic.

The SDK native object includes the versioned handoff component. Compiler and
runtime compatibility markers are now 0.9.9-19 and reject old -18 interfaces,
caches and runtime symbols. The private source gate explicitly constructs its
complete runtime and a separate missing-handoff control; no new private entry
has been added. Runtime algorithms and capture/refcount layouts are unchanged.

This is NOT a complete public-thread candidate yet:

- dynamic/state, owning closure records, consuming Unit (thin/dynamic), fresh
  mutable construction and value/unique/shared/string results run successfully;
- lexical-module helper lookup plus pending-handle recipes close the original
  cached-method plan gap. Complete remains false in probes; promotion occurs
  after Sema with checked wrapper body, valid specialization and exact edges;
- repeated/borrowed erased thin environments and aliased mutable dynamic values
  remain unqualified. No usage-flag uniqueness heuristic was introduced;
- public error, lifetime, ignored-result and fault tests pass for this subset;
- original caller and std/sync storage migrations are not complete.

Automatic review rejected both usage-flag-based uniqueness and immediate
speculative qualification exceptions. Neither patch was applied. The later
pending/validated implementation gives no CodeGen authority to a probe and
retains the actual toolchain-origin check. See
[the proposed pending/validated and environment-identity resolution](public_thread_pending_plan_design.md)
and the [unapplied rejected guard diff](public_thread_rejected_speculative_gate.patch).
The subsequent explicit full-diff authorization allowed the std/sync production
storage cleanup migration to be applied. See
[its exact scope and remaining positive targets](std_sync_thread_storage_migration.md).

Directed checks during this WIP: testing and non-test Release tokac builds;
production rejection of the fault option; public suite (8 strict parity fixtures,
22 runtime runs plus a source-hidden provider TKI/object run, 10 source-negative
and 10 fault no-artifact checks); adapter/private-source CTest 2/2; isolated
runtime 31 schedules + 14 fatal cases and 2 stderr checks; old -18 interface,
cache and runtime rejection; diff check. The migrated original thread/string
example and owned-state return example run successfully. No full suite or
public-thread/binding/release acceptance is claimed.

Self-review also closed consuming borrowed-formal invocation (E0473) and
consuming-callable Copy/explicit-copy-capture grants (E0606/E04581). The source
gate preserves source liveness on rejection. These are contract fixes, not a
receiver spelling change or an extension of capture/refcount mechanisms.

Regression classification: generic-body, dyn-fn lifecycle and binding-transfer
gates passed. The indirect unique fixture and return-reference-unknown fixture
fail at the same earlier E04656/E04661 diagnostics on a fresh, clean 50e78d16
build; these are unaccepted-base debt, not this patch's regressions. Their
oracles were not changed. The call-shadow live SDK case was switched explicitly
to current semantics with strict parity, while historical fixtures remain on
their original profile; remaining live synchronization cases await API/storage
migration. No full run, push, PR or movement of prior freeze refs occurred.

Final directed rerun for this checkpoint: adapter, private source, and public
responsibility CTest targets passed 3/3 in 63.36 seconds. This does not include
the open std/sync integration cases or the two confirmed base-fixture debts.
