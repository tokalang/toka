# Public std/thread handoff integration

Date: 2026-09-09
Branch: `impl/std-thread-handoff-v1`
Base: `07ff495b465df77e3e7d1f950388c3c75d5c3351`
Status: implementation in progress — NOT a public-thread review candidate

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

The legacy std/thread has not yet been replaced. Backend tests alone do not
qualify public threads, binding, Stage 1, or a release. No full suite, push or PR
is part of this intermediate checkpoint.
