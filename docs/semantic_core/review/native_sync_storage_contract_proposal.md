# Minimal native sync storage contract — design requested, not implemented

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

Public Mutex/RwMutex APIs should stay unchanged. The private factory/access/drop
spelling and its implementation plan must be frozen at design acceptance; do not
silently add a general user-implementable trait or annotation to make this pass.

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
- missing/mismatched witness, wrong storage/element/owner/cleanup, readonly view,
  PAL conflict, source-hidden missing contract: rejection with no artifact.

This is the smallest proposed responsibility relation, not a general dynamic
extent system and not implementation approval. It cannot cure the separately
identified shared-parameter body/ABI disagreement.
