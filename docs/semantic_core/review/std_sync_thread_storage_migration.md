# std/sync storage migration — authorized implementation

Status: applied under explicit full-diff authorization; NOT Accepted
Date: 2026-09-09

The public thread protocol runs for the qualified source subset. Restoring the
original mutex/thread integration tests encounters existing std/sync storage
code: writes cast Addr to read-only `*[T]` and try to add payload writability at
the destination binding. Normal Sema correctly rejects this. Mutex/RwMutex Drop
also uses the old raw-element `cede p[0]` plus Option temporary construction.

The [reviewed full diff](std_sync_thread_storage_migration.patch) was explicitly
authorized and implemented. It changes only:

1. Use the accepted explicit unsafe writable raw construction, retaining nullable
   checking and a same-source nonnull guard. Only existing writable storage uses
   are migrated; the RwMutex read-only pointer cast is unchanged.
2. Destroy one initialized T in place and release its malloc-family storage with
   the existing `free[1] *p` contract. Do not move through Option, call free twice,
   or alter native lock destruction order.

Allocation ownership and the live prefix remain unsafe implementation duties.
The helper does not prove initialization or create ownership. No capture layout,
reference counting, A/B state transition, general raw_take rule, or allocation
API change is proposed. Allocation-null handling stops before dereferencing.

The historical automatic-review rejection is superseded by the user's explicit
full-diff production-cleanup authorization. This is not merely fact recording.
No compiler/runtime authority rule was weakened to apply it.

Directed executable checks now cover scalar lock mutation/relock, Mutex and
RwMutex resource Drop, a remaining shared lock owner, and allocation-null abort
before dereference. The allocator fault is test-only (Darwin dylib interposition
or Linux link wrapping); its armed hook verifies the requested allocation size.
Normal/shadow diagnostics match. Readonly writes and discarding an owner with a
live guard reject with no object/IR artifact.

Two positive targets remain unresolved, not reclassified as negative tests:

- `sync_managed_storage_pending.tk`: direct unique/shared T instantiates guard
  casts `*^T`/`*~T`, rejected by E0492 before runtime cleanup. The approved array
  storage cleanup itself is not proof that these guard morphologies work.
- `sync_thread_pending.tk`: a callable capturing the shared Mutex fails binding
  dependency completeness. Its opaque native/data Addr fields cannot gain an
  independent-environment witness from @Send alone.

The full thread/binding slice remains unaccepted. These cases are retained for
scope review; no raw/managed or opaque-environment whitelist was added.
