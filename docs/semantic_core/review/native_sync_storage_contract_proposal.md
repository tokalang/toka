# Minimal native sync storage contract — Accepted for implementation

Status: user-accepted first-batch design; implementation in progress.
Current implementation is limited to the read-only ClosedPayload prerequisite;
factory/access/drop adapters and value/guard witnesses are not yet activated.

Scope: close the owned environment proof for Mutex/RwMutex and composed sync
objects, without changing @Send, raw-pointer permissions, capture representation,
reference counting, or the accepted thread A/B protocol.

## Why this is a missing contract, not merely wiring

- `UnsafeRawConstructionPlan` proves the admitted construction and records
  `UnsafeCallerPrecondition`; it does not establish initialized extent, ownership
  of an allocation or a cleanup owner.
- `RawElementTakePlan` explicitly leaves initialization and remainder duties to
  the unsafe caller. It cannot certify a Mutex allocation left behind by a take.
- `CallableEnvironmentFacts` contains complete referents/local bounds, but no
  owned-storage relation. The public thread validator independently checks actual
  physical fields; an Addr has no such proof. Its rejection is currently correct.
- The Mutex/RwMutex source stores both allocation and native lock as Addr. The
  reviewed source implementation has the intended responsibility, but a shape
  name, @Send declaration, Drop body or absence of roots is not a machine-checked
  storage contract. Do not exempt those Addr fields by nominal type alone.

## Proposed semantic relation

Introduce one private, non-forgeable `NativeSyncStorageWitness<T>` contract.
This is a semantic witness, not a proposed new runtime allocation/refcount.
It binds all of the following to the **same exact value/owner instance**:

| Fact | Required meaning |
| --- | --- |
| Native lock identity | Exact lock resource and kind, with its destroy operation |
| Storage identity | Exact allocation and complete T morphology, size/alignment |
| Initialization | One initialized T after the controlled create operation; never inferred from arbitrary raw writes |
| Payload dependencies | Actual, complete dependencies carried by the incoming T; empty only if independently proved |
| Owner relation | This owner's lifetime retains the storage/native resource; copied shared owners retain that same lifetime |
| Guard relation | The exact element slot is borrowed under the selected lock; the live guard retains the owner dependency and unlocks before owner cleanup |
| Cleanup | Drop the live T, release its allocation once, then destroy the native lock, following the reviewed order |

The witness must be introduced by an explicitly reviewed **private creation
contract**, not by matching malloc/write/free AST patterns or recognizing a
function name. Its producer must bind the resolved, trusted declaration/entry,
validated incoming value plan and actual result edge. A controlled typed adapter
performs the initialization before publishing the witness. Its storage-validity
preconditions remain stated as unsafe implementation responsibility; they are
not relabeled as whole-heap compiler initialization proofs.

Public Mutex/RwMutex APIs stay unchanged. The following first-batch decisions
have explicit implementation authorization; this is not implementation acceptance.
There is no general user-implementable trait or annotation for this contract.

## Decision 1: private operations, identity and publication

All entries below belong to the resolver-authenticated toolchain module
`std/sync`. These are private **Toka declaration contracts**, not new C ABI
entry points. `'T` always carries its complete instantiated morphology; each
guard operation has its own concrete guard contract, not an erased guard type.

| Private signature | Contract and public wrapper |
| --- | --- |
| `__sync_mutex_create<'T: @Send>(cede 'data: T) -> Mutex<'T>` | Factory used by Mutex new/make/make_shared; returns one direct owner, which the pointer-producing wrappers move into their ordinary owning allocation. |
| `__sync_rw_create<'T: @{Send, Sync}>(cede 'data: T) -> RwMutex<'T>` | Same, for RwMutex new/make_shared. |
| `__sync_mutex_acquire<'T: @Send>(owner: Mutex<'T>) -> Result<MutexLock<'T>, Error> <- owner` | Native acquire followed by exact writable slot borrow; used by lock. |
| `__sync_rw_read<'T: @{Send, Sync}>(owner: RwMutex<'T>) -> Result<RwReadLock<'T>, Error> <- owner` | Native read acquire followed by exact readonly slot borrow. |
| `__sync_rw_write<'T: @{Send, Sync}>(owner: RwMutex<'T>) -> Result<RwWriteLock<'T>, Error> <- owner` | Native write acquire followed by exact writable slot borrow. |
| `__sync_mutex_release<'T: @Send>(held#: MutexLock<'T>)` | Discharges this guard's successful acquisition exactly once; guard Drop delegates here. |
| `__sync_rw_read_release<'T: @{Send, Sync}>(held#: RwReadLock<'T>)` / `__sync_rw_write_release<'T: @{Send, Sync}>(held#: RwWriteLock<'T>)` | Same for each exact native acquire mode. |
| `__sync_mutex_drop<'T: @Send>(owner#: Mutex<'T>)` / `__sync_rw_drop<'T: @{Send, Sync}>(owner#: RwMutex<'T>)` | Terminal owner cleanup, called only by the matching Drop adapter; cannot be an ordinary callable destruction escape. |
| `__sync_cond_create<'T: @Send>() -> CondVar<'T>` / `__sync_cond_drop<'T: @Send>(owner#: CondVar<'T>)` | Native-only creation and terminal cleanup; no fabricated payload slot. |
| `__sync_cond_wait<'T: @Send>(owner: CondVar<'T>, held: MutexLock<'T>)` | Uses the exact live mutex guard/native handle; native wait releases/reacquires that mutex without retiring its logical guard. The adapter itself neither replaces nor cleans the payload. |
| `__sync_cond_signal<'T: @Send>(owner: CondVar<'T>)` / `__sync_cond_broadcast<'T: @Send>(owner: CondVar<'T>)` | Uses the exact live native-only witness; never creates a storage witness. |

`borrow`/`borrow_mut` keep their existing signatures: they project the exact
guard slot with its owner dependency and complete `&'T`/`&'T#` morphology, not a
new raw-pointer cast. Existing explicit `unlock` is not an additional witness
producer: it cannot be accepted on a witnessed path while an undischarged guard
would later unlock again. Until its matching acquisition/discharge is proven,
that path is outside the first batch; no silent public-signature change or
double-unlock exemption is proposed.

Acquisition tokens are thread-bound. A native wait temporarily suspends access
until reacquisition; another writer may have replaced T in between, so no cached
payload-value fact survives that wait. The ClosedPayload invariant below does
survive. Escaping aliases/callbacks or a language suspension whose thread/access
continuity is unproven do not receive this first-batch witness authority.

Trust requires all of: resolver toolchain provenance, canonical logical module,
resolved declaration identity plus exact signature, and exact nominal template
identities for owner/guard/result. The selected generic instance must refer to
that declaration and have a complete validated plan. Names in this table are
dispatch labels only; same-name user modules, fields or functions grant nothing.
The reviewed adapters must check the native operations listed below. Today's
unchecked `sys_*_create/destroy` wrappers cannot themselves serve as that proof.

Witness carriage is compiler-side: an immutable, typed plan on the exact
factory/result AST edge and an ownership relation in Sema state keyed by the
semantic owner and storage identities. Moves preserve the relation; ordinary
shared retain/release preserves the same owner identity. A guard edge adds a
borrow/acquisition token referring to that owner and exact slot. Composition
uses field-specific child relations, not a nominal exemption for Once/WaitGroup.
No public field, runtime witness word, registry, capture layout or refcount
change is proposed. Validated lowering performs the corresponding real resource
operations; missing/mismatched plans fail closed rather than reconstruct facts.

There are two distinct publication points:

- **Static qualification:** after normal source validation and the complete
  adapter/instance contract are available, before caller ownership commit. No
  candidate probe, unresolved instance or rejected transaction publishes it.
- **Runtime owner publication:** only after native initialization and the one-T
  initialization commit both succeed. An access publishes a guard only after
  successful acquire; Drop retires the matching tokens, never a different owner.

First batch has **no serialized witness contract**. A source-hidden operation or
value missing these contracts is rejected for witnessed/thread qualification;
ordinary TKI type identity or `@Send` is insufficient. No TKI/key/ABI expansion is
part of this proposal. Private operations must not be emitted into source-hidden
consumers as unchecked ordinary calls.

## Decision 2: construction and native failure responsibility

Factory order is fixed: evaluate/validate the incoming T once; take its cleanup
responsibility into the factory; allocate uninitialized payload storage; allocate
and initialize the native object; perform one non-expanding full-T transfer into
the slot; publish the owner. The transfer commit does not allocate, retain, call
user code or suspend. Required retain/value preparation happens before commit.
Storage size/alignment and native-object extent must be target-proved; the current
hard-coded native allocation sizes are not themselves portable layout evidence.

Factories keep their existing infallible public API. Resource/setup failures
clean the owned partial state below, then take a **lock-free, allocation-free,
non-I/O fatal** path. This is an explicit proposed failure policy, not a claim
that current sys/sync already implements it. Fatal termination does not promise
completion of arbitrary user destructors or process-wide cleanup.

| Failure point / state | Responsibility before fatal or return |
| --- | --- |
| Argument evaluation rejects before factory entry | Existing call rollback rules; no factory resource or published witness. |
| Payload allocation returns null | Factory still owns incoming T; drop it once, with no dereference/free of null. |
| Native allocation returns null | Drop incoming T once; free only the uninitialized payload allocation. |
| Native initialization returns failure | Native storage is allocated but not initialized: free it **without destroy**; drop incoming T; free uninitialized payload storage. No witness. |
| Payload preparation fails before slot commit | Factory owns incoming T or its already-prepared replacement, never both. Drop that live value; free uninitialized payload storage; destroy successfully initialized native object, then free its allocation only on successful destroy. |
| Full-T slot commit | No recoverable failure point is admitted inside this operation. Before commit the factory owns T; after commit the slot owns T. Unsupported expanding initialization is rejected, not called a partly initialized complete T. |
| Outer unique/shared owner allocation fails after direct factory succeeds | The local direct owner performs its normal terminal cleanup; no second payload cleanup. Runtime fatal/unwind behavior of allocation must preserve that ownership edge before this wrapper is admitted. |
| Acquire returns error | Owner/storage remain live and unchanged; no guard token. Existing `Result<..., Error>` reports native status; failed error-value allocation may terminate without inventing a guard. |
| Guard release, terminal native destroy, cond wait/signal/broadcast returns error | No silent success or blind retry. Fatal outside internal locks; do not free a native object whose destroy failed. Terminal failure makes no promise that all remaining resources were reclaimed. |

On normal owner Drop, all guards are gone: drop the live payload and free its
allocation once, then destroy the native object and free its allocation once.
This preserves the reviewed payload-before-lock destruction order. Native-only
CondVar follows the same native state rules. Cond wait failure is terminal because
the implementation cannot safely publish a continuing guard with uncertain
native lock state. Native errors must be observed by private adapters; current
void wrappers which discard return codes must not be reused unchanged.

Factory/terminal cleanup holds no acquired payload guard and introduces no
hidden management lock. Replacing T through a write guard can run old-T Drop
while that user's native lock is held, as ordinary lock-protected mutation does;
this is not an A/B control-block lock and does not alter its lock-free-user-code
rule. Invalid unsafe addresses, corrupted initialization or concurrent unsafe
misuse are caller violations, not recoverable factory failures with a promised
rollback.

## Decision 3: dependency invariant under replacement

Choose the conservative first-batch option: a thread-publishable storage witness
requires `ClosedPayload<T>` proven for the **complete instantiated type and every
admitted safe replacement**, not merely for its initializer. This is internal
proof data, not a public trait. It means every live payload value allowed through
this slot's supported write paths has no external lifetime dependency, and every
reachable mutable component preserves the same property.

- Primitive values need real value semantics (Addr/raw identities do not qualify
  by size). Owned records/enums/arrays require recursive proof of all alternatives
  and replaceable fields. Unique/shared elements require the complete pointee and
  all permitted mutations to satisfy the same invariant. Shared ownership alone
  does not establish it. Existing independently proved canonical owning-string
  contracts may be consumed; nested native sync components require their exact
  child witness and ClosedPayload proof. Neither a type short name nor a Drop
  hook is proof; unrelated Addr fields remain unqualified.
- Borrowed `str`/references, raw fields, erased callables without a replacement-
  stable environment contract, unknown instances and cycles without a closed
  proof remain unqualified for crossing threads. A particular static initializer
  does not make all later borrowed replacements static. Their otherwise-valid
  local lock usage is not redefined by this thread qualification restriction.
- Every guard replacement first validates the incoming value, permissions, PAL,
  dependencies and full-T cleanup plan. It obtains the new responsibility before
  releasing the old one, and preserves the one-live-slot invariant on success.
  Rejection rolls back without changing the old slot/owner facts. Partial writes
  or consuming field operations without a proof preserving this invariant do
  not gain witness authority from having a write guard.
- All shared aliases refer to the **same immutable ClosedPayload invariant**,
  not copied initializer dependency lists. No first-batch concurrent mutable
  dependency summary is required. A write must never continue using stale roots
  from factory initialization; new value validation establishes the same closed
  invariant anew. Unknown/raw mutation or storage-field rebinding is not a way
  to retain a published witness: unsupported escape paths must be rejected before
  thread publication, and known restrictions cannot be erased through Addr.

Consequently the first batch does not implement changing external dependencies
under a shared lock. Supporting that later would require a separate shared
dependency-update protocol; it cannot be slipped into ordinary fact propagation.
`@Send`/`@Sync` remain additional checks, not substitutes for this invariant.

## Rules the implementation would have to enforce

1. Raw Addr values cannot manufacture a witness. Direct field construction,
   unknown storage mutation, rebind or escape without a validated transfer loses
   completeness and cannot cross the thread boundary.
2. Moves carry the same witness/cleanup responsibility. Shared copies retain the
   same owner. Generic substitution preserves the actual T dependency set; a
   borrowed/raw member must not become independent because its wrapper is owned.
3. Projection/lock operations derive a slot view from the witness without making
   a new owner. Read/write requests use the actual lock and element capabilities;
   read locking must not grant slot-write authority, and no view may amplify T's
   pointee permissions. PAL and source-view checks remain in force.
4. `collectStage1CallableEnvironment` and public thread qualification consume the
   same value witness and recursively qualified payload dependencies. Neither may
   separately whitelist Mutex, Addr, a private type name or @Send.
5. Failed creation leaves one well-defined cleanup owner. Drop cannot occur with
   live guards. Unknown/faulted facts fail closed before publishing authority.
6. A native-only resource (e.g. CondVar) needs its own exact native lifetime
   witness; it is not a dependency-free Addr. Once/WaitGroup compose validated
   component witnesses rather than gaining a name-based exemption.
7. Source-hidden persistence must preserve this contract or reject it. If a new
   TKI/evidence field or compatibility key is required, propose that exact change
   before activation. No current protocol/layout/key change is authorized by this
   design draft.

## Acceptance matrix

- scalar, direct resource, unique/shared T and a remaining shared owner;
- actual lock/read/write, repeated locking, guard owner retention and early exit;
- shared Mutex captured by repeatable/consuming callable, spawn failure cleanup,
  join, early/late detach/drop, worker-before-create-return and exact-once;
- actual payload borrowing/unknown pointers stay dependent/rejected; no @Send,
  same-name type, forged fields, stale rebind or unchecked template exemption;
- payload/native allocation failures, native-init failure, incoming-value cleanup,
  no destroy of uninitialized native storage, no free after failed destroy;
- native lock failure produces no guard; release/wait/notify failure follows the
  documented fatal policy; wrapper owner-allocation failure cannot strand storage;
- two shared aliases, repeated legal guard replacements and replacement exact-once;
  borrowed/static-first-then-local replacement cannot reuse initializer proof;
- closed generic instances versus borrowed/raw/unknown instances and nested mutable
  fields; missing replacement-stability proof rejects thread publication;
- missing/mismatched witness, wrong storage/element/owner/cleanup, readonly view,
  PAL conflict, source-hidden missing contract: rejection with no artifact.

This is the accepted first-batch responsibility relation, not a general dynamic
extent system. Runtime/production qualification is still required; acceptance
of the design alone does not grant any source value a witness.
