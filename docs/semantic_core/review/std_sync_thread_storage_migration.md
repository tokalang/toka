# std/sync storage migration — human approval required

Status: proposed; rejected patch was NOT applied
Date: 2026-09-09

The public thread protocol runs for the qualified source subset. Restoring the
original mutex/thread integration tests encounters existing std/sync storage
code: writes cast Addr to read-only `*[T]` and try to add payload writability at
the destination binding. Normal Sema correctly rejects this. Mutex/RwMutex Drop
also uses the old raw-element `cede p[0]` plus Option temporary construction.

The [unapplied full diff](std_sync_thread_storage_migration.patch) proposes only:

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

Automatic review rejected this production synchronization-resource lifecycle
change. The repository's std/sync.tk is unchanged. Verification after approval
must cover scalar/resource/unique/shared T, payload Drop once, native lock
release, remaining owners/guards keeping data live, readonly and unknown-source
refusals. This does not promise that every dependency-proof gap disappears and
is not a binding/thread acceptance.
