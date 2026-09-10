# Native sync private adapters — implementation checkpoint, not Accepted

Subsequent source-plan WIP, the direct-owner view repair, and remaining chain are
tracked in [native_sync_factory_source_progress.md](native_sync_factory_source_progress.md).
The figures below describe the `aaac3267` adapter checkpoint, not a full witness acceptance.

Base: `636d29b6`. This implements the private operation bodies from the accepted
first-batch contract. Public Mutex/RwMutex/CondVar wrappers are deliberately not
switched over until exact witness qualification is connected.

## Implemented

- Mutex/RwMutex factory: own incoming T, allocate payload storage, allocate/init
  native storage, transfer complete T once, then return the owner.
- Failed payload/native allocation releases incoming T once. Failed native init
  frees native storage without destroy, then releases T and payload storage.
- Read/write acquire returns Error on native failure without creating a guard.
- Guard release, native destroy and cond wait/notify failures terminate via the
  existing C `_Exit` function. Failed native destroy is never followed by free.
- Owner Drop destroys the live T and frees payload storage before checked native
  destruction, preserving the accepted order. Retired addresses are cleared.
- The cleanup helper returns the complete transferred T into the caller's
  full-expression cleanup. An Option wrapper was rejected during development
  because generated code added an extra shared retain; enum/refcount machinery
  was not changed to accommodate this helper.
- POSIX runtime compilation checks the storage reserves against real pthread
  sizes/alignment. The test additionally verifies the sizes actually requested.
  This is not qualification of an untested target or of arbitrary over-aligned T.

## Directed evidence

`toka_native_sync_adapters` runs **76** cases using the exact SDK adapter source
plus a temporary in-module test driver. A copied SDK keeps normal module trust
and visibility rules; no test entry is installed in the production library.
Native allocator/pthread return faults are injected at the real call boundary.
The test observes payload/native frees, initialization/destroy calls, value Drop,
and the remaining shared owner's reference count before fatal termination.

Coverage includes scalar/resource/unique/shared, payload and native allocation
failures, native init failure, acquire error, release/destroy errors, condition
allocation/init/drop/notify errors, and condition-wait failure. Additional scalar
tests execute lock/write/relock and rw write/read against the stored value.

Directed CTest **4/4** passes: adapter matrix, ClosedPayload prerequisite,
existing sync storage, shared ABI (23/23 plus IR checks). No full suite ran.
Log: `/private/tmp/toka-native-adapter-gates.log`.

## Still not implemented / not qualified

- Exact factory/result/owner/storage witness publication and propagation;
- matching guard-acquisition proof, alias/return/capture propagation and
  replacement validation under a shared owner;
- source-hidden missing-contract rejection at those new operation boundaries;
- successful threaded condition-wait interleavings, outer owner-allocation failure
  after factory publication, and the final complete source/fault matrix.

The adapters and ClosedPayload result grant no witness by themselves. The
`sync_thread_pending.tk` positive remains mandatory and unclosed. No subsequent
thread caller migration, public trait, A/B/capture/refcount change, TKI expansion
or new runtime ABI entry was made. `_Exit` is an existing platform function.
