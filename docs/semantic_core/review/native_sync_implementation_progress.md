# Native sync implementation — not complete

Latest: [private adapter and failure-cleanup checkpoint](native_sync_adapter_progress.md).
The adapters now have directed runtime coverage, but witness publication remains
unimplemented; the prerequisite-only notes below describe the earlier checkpoint.

Shared reception is isolated in `71e1c025`; its 23 required checks and carrier IR
assertions pass. This subsequent change starts the separately authorized storage
contract and does not mix in caller migrations.

Implemented: `Sema::checkNativeSyncClosedPayload`, a read-only prerequisite over
complete resolved type facts. It returns Closed / ExternalDependency / Incomplete
with an offending field/argument path. It checks every enum alternative, array
element type and mutable reachable field. Reference/raw/Addr/callable/slice
dependencies are not converted into owners. Unknown declarations, uninstantiated
templates, incomplete fields/arguments and unclosed recursive graphs reject.
Template facts are not substituted or reused across differing argument types.
Unproven phantom-argument irrelevance remains conservative.

The existing exact canonical owning-string contract can supply its already
accepted proof; a same-spelled user type or Drop hook cannot. Native owner values
are not whitelisted by this type predicate: their opaque fields still require
the separate per-value storage witness. This code creates no initialized extent,
native resource, ownership relation, guard token or thread authority.

The new C++ test exercises resolved semantic types, not user-provided qualification
booleans: primitive/owned/unique/shared positives, borrowed/raw nested records and
enum alternatives, same-name spoofing, differing generic instances, missing facts
and recursion. Its directed CTest passes; the shared ABI target remains 23/23.
No full regression suite was repeated.

Remaining authorized work, not claimed finished:

1. Private factory/access/drop adapters with checked native errors and the accepted
   partial-failure cleanup order, including target-backed size/alignment evidence.
2. Sealed factory/result plans and per-owner/per-guard witness propagation, source-
   hidden missing-contract rejection, and replacement validation under the same
   ClosedPayload invariant.
3. The real resource/fault/replacement matrix and `sync_thread_pending.tk`.
   Current required sync/thread gate is 11/12: shared storage and both managed
   guards now pass; the actual native-environment witness case still rejects.
4. Only after those pass, migrate the remaining condvar/once/waitgroup callers.

No new private demonstration entry, public trait, capture/refcount/A/B change,
TKI/ABI extension or raw_take-isolation relaxation was added.
