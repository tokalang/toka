# Thread three-item increment — WIP, not Accepted

Baseline: `eedd0bf031fd4618b1290e989a736e281504955d`.
Only managed guards, native lock storage evidence and dependent thread callers
are in scope. No networking/container backlog or existing oracle is changed.

## 1. Managed guard adaptation: partial, not closed

Implemented using existing source constructs:

- Address a `*[T]` element slot, not the forbidden raw scalar `*^T`/`*~T`.
- `&'(p[0])` selects the complete element identity; `&p[0]` selects the payload
  and is not a substitute. Guard returns spell `&'T` and select `self.&data`.
- Writable slot authority is mapped to the outer reference payload capability
  only for an exact selected managed raw element with a writable declared
  storage view, matching resolved type and unblocked source capability. The
  inner pointee permissions remain unchanged. Plain unsafe is not a grant.
- Constructors use actual morphic parameters and `cede 'data`. Previously the
  unique path could load payload bytes as a handle; the source migration preserves
  identity without changing a CodeGen/capture rule.

`sync_unique_guard.tk` executes actual Mutex lock/write/relock and RwMutex
write/read, checking values and exact-once Drop. A writable slot-view construction
is the positive control for the rejected read-guard capability upgrade.

Four negative controls reject without object/IR artifacts: readonly pointee field
write, read-guard slot capability upgrade, owner discard while a managed guard
is live, and a readonly raw slot supplying a writable handle reference. Explicit
mutable fields retain their preexisting interior-mutability rules; the readonly
control does not incorrectly label an explicitly mutable field readonly.

**Shared remains a blocker.** It now type-checks but fails the existing required
runtime positives: managed_storage returns 5 (final Drop count), shared_guard
returns 2 (remaining-owner/value check). This is not a successful recovery and
must not be shipped/frozen. The prior E0492 rejection had masked this path.

The IR shows the shared signature takes a pointer to the two-word carrier, but
the morphic shared body stores that pointer into carrier storage without the
matching captured-parameter indirection. CodeGen_Decl.cpp already forces
`typeObj->isSharedPtr()` in signature generation (near line 306), but its body
decision (near line 533) tests only the syntactic IsShared flag.

The proposed [exact body-alignment diff](shared_parameter_body_alignment_pending.patch)
is **not applied**. Auto-review rejected it because its effect covers generic
shared parameters beyond std/sync. No alternative tool, wrapper or runtime shim
was used to bypass that decision. It needs explicit scope approval and subsequent
runtime regression; it is intended to obey the existing ABI, not change it.

## 2. Native lock environment evidence: missing contract identified

No witness capable of expressing the owned allocation, initialized T, actual
payload dependencies, native resource lifetime, guards and cleanup order exists
in the current raw facts. The closure/storage pending fixture still rejects
IncompleteFacts; no @Send/type-name/Addr exemption was introduced.

The separate [minimal contract proposal](native_sync_storage_contract_proposal.md)
defines the required same-owner relation and validation matrix. It is a design
request, not implemented semantics or a request to change A/B/capture/refcount.

## 3. Caller migration: intentionally not started

Condvar/once/waitgroup migrations wait for the first two items, as instructed.
They are not silently excluded or changed to negative tests.

## Required gate and relative verification

Both formerly pending targets, plus actual unique/shared guard operations, are
now required positives in `toka_public_thread_sync_closeout`. It runs all cases
and reports failures, never xfail/skip. Current result: **9/12**, with the three
open positives above. Eight no-artifact rejection checks and the unique runtime
case pass. The gate must turn completely green before thread acceptance.

The six-target directed CTest run is **5/6**: public thread responsibility,
existing scalar/resource sync storage, unsafe raw construction, binding value
dependencies and frozen non-call shadow pass; only the new required closeout
gate fails. No complete CTest/PASS/FAIL suite was repeated.

Relative to the saved eedd0bf0 baseline:

- All **140** previously failing PASS cases still have the same return code,
  first error code and first reason. Source-line shifts are not interpreted as
  a new root cause. Inventory: `/private/tmp/toka-eedd-guard-failure-delta.jsonl`.
- The **9** previously passing direct sync/thread and level-2-view cases were
  rerun through the ordinary executable harness and remain **9/9**:
  g07_sync_rwmutex; g08_level2_borrow_views; g08_level2_return_views;
  g08_morphology_constraint_domains; g09_atomic_shared; g09_guard_bind_mutex;
  g09_guard_ext_lifetime; g09_sync_mutex; g09_thread_example.
- No .stderr/legacy oracle was changed. These checks establish the measured
  delta, not a new full 451/473 qualification or proof that unexecuted cases pass.

Logs: `/private/tmp/toka-guard-directed-ctest.log`,
`/private/tmp/toka-guard-closeout.log`, and
`/private/tmp/toka-eedd-guard-nine-passes.log`.

After the final defensive variable-lookup adjustment, tokac was rebuilt and the
two sync CTest targets rerun: the existing subset passes and the required new
gate remains 9/12 with the same three failures. Log:
`/private/tmp/toka-guard-final-check.log`. The pending CodeGen patch was checked
for applicability only and remains unapplied.
