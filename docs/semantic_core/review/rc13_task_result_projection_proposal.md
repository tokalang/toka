# Wait across a helper: one pending contract decision

Status: approved for implementation; implementation WIP, not accepted. No change to phase E, runtime,
ABI, TKI, raw_take, or caller spelling.

## Evidence

`core/task::TaskHandle<T>` contains only `coro_handle`; the checked result's
origins are not represented as a separate parameter/result contract.
`std/task::block_on<T>` accepts that opaque handle, waits, and returns its T.
The existing direct-Wait mapping requires the selected async producer and
therefore correctly refuses this parameter route. A type T or an empty
dependency set cannot recover the missing origins. Adding `return <- handle`
alone would supply an allowed upper bound, not prove the actual result source.

## Proposed limited contract

1. A successfully checked async producer may supply a **result-origin witness**:
   actual selected argument/field origins, or checked all-return static-storage
   witnesses. Execution/capture dependencies are not substituted for result
   dependencies. Unknown or invalid producers supply no witness.
2. A source-visible checked helper must explicitly declare the existing lifetime
   ceiling when its result may borrow (for example `block_on<T>(handle:
   TaskHandle<T>) -> T <- handle`). Do not infer a missing declaration or start E.
   Its validated body may summarize `wait(parameter)` as a symbolic
   **result projection of that exact task parameter**. This is not the address
   of the handle descriptor, not independence, and not a type-level property.
   Every return branch must validate; generic instances cache only the symbolic
   mapping, never the first caller's concrete source.
3. At each caller, substitute the actual task's current witness. Preserve dynamic
   origins, per-field associations and static witnesses separately. Missing,
   mismatched, modified, conflicting-merge or opaque/source-hidden evidence
   remains fail-closed. Copy/move, merge and rollback preserve or invalidate the
   same current witness; no replay of stale initializer names.
4. Normal source, permission, PAL, lifetime, ownership and cleanup checks still
   run. Only a completed matching projection may inform the existing planner.
   This explicitly includes production admission of instances previously
   rejected solely for the missing result-origin proof, through the complete
   planner rather than a direct `Admitted` override.
   There is no new public syntax or special privilege for a `block_on` name.
   Ordinary signature migration uses existing dependency syntax; no new TKI
   proof field/schema or interface-key version is introduced. A declaration's
   ceiling still cannot substitute for a missing actual task-result witness.

## Fixed validation scope

- Original `g09_async_wait_syntax`; scalar/owned result controls.
- Direct Wait versus source-visible helper, repeated generic cache hit with
  different actual origins, and producer declaration-order controls.
- Result borrowing only the second input: legal source succeeds, local source
  escapes fail, unrelated first-input dependencies do not replace result origins.
- Missing/source-hidden witness, mutation, shadowing, branch mixture, descriptor
  address, invalid producer and failed-call rollback remain rejected/unpolluted.
- Normal/shadow parity, valid runtime/exact-once, and object/IR non-production
  for negatives. No execution of dangling-view probes.

The user authorized the complete implementation and production planner admission.
No further per-helper approval is required. This does not constitute acceptance
of the implementation or the combined anonymous-record / Wait package.

## Implementation notes (WIP)

- Private expression/current-value witnesses distinguish independent, static,
  borrowed and symbolic task-result projections. They are not cloned or exported.
- Definition summaries are published only after ordinary body validation and
  complete returns. Generic summaries contain parameter positions, not a caller's
  values. Source-hidden definitions do not publish these summaries.
- Actual return origins and task-witness prerequisites are separate sets: using
  another task inside a helper must not contaminate the returned result's origins,
  nor may arithmetic on a result silently drop the prerequisite.
- Current-value maps are snapshot/merged/restored with AnalysisState; conflicting
  branch values lose their witness. Function prerequisites union across branches
  and roll back with rejected calls.
- Reference results retain addressed-storage identity; static character data
  cannot exempt a local descriptor. Borrowing an owned task-frame input or a
  non-reference parameter's descriptor is rejected.
- Call proof validation runs before its rollback guard exits; Wait snapshots
  before operand evaluation. No result proof authorizes consumption or cleanup.
- `block_on` declares `<- handle` and a whole-T local. `.wait` already starts and
  pumps the task, so the duplicated raw-handle pump call was removed; runtime and
  CodeGen result-take/repeated-take/cleanup implementations are unchanged.

The 26-case directed matrix passed, followed by the added live-variant owning
result runtime control. The associated safety/static-return/JSON CTest group
passed 3/3 (120.53 seconds). The combined fixed-candidate full run is complete;
see `rc13_task_result_candidate.md`. The package is still not complete/accepted
because the full run exposes two owned-buffer positive regressions.
Development probes have covered external sources after task cleanup,
static/scalar/unique/shared results, selected input/field, declaration order,
cache reuse, move/forward, branch preservation/conflict, mutation, source-hidden
rejection, producer/frame errors, post-proof rejection rollback, and the existing
runtime trap for repeated owning-result extraction. No unsafe escape program
was executed.

Known integration blocker: `g09_async_owning_payload_drop_regression` now reaches
its `Bytes` row. `Bytes` owns opaque raw buffer storage whose instance provenance
is not established by this result-projection implementation. Earlier rows
(`Owned`, `Option<Owned>`, the actual `Ok(Owned)` variant, and regular enum
payloads) have closed-value/variant proofs. `Bytes` must not receive the same
proof merely from its name, Drop method, empty dependency set, or a container
identity rule. The original positive remains unchanged and failing; this is not
declared a language prohibition. No Bytes/Vec storage contract was added here.

## Byte-buffer result handoff — approved for implementation

Inspected base: `8c659fac6370ef5c08651865f1508715aefdc12a` (implementation
`118d48608ff01ea9f22d1afd46795102f47a6884`). This section proposes a new,
limited library storage contract. The user approved the contract and per-instance
production admission; **the implementation remains WIP, not accepted**.
It does not reopen the task projection contract above.

### Why existing facts are insufficient

- `RawAllocationAncestry` explicitly proves ancestry only, not current ownership,
  initialized contents, or absence of external dependencies.
- `ResultIndependenceFact` currently has validated unique-result producers; its
  existence on those expressions cannot be extrapolated to a raw-field owner.
- `Vec` exposes `buf/len/cap` and `from_raw`; a correct nominal identity and a
  Drop implementation therefore cannot prove a particular instance independent.
- `Bytes::from_vec` exports the raw pointer after emptying the Vec. Today no
  ownership receipt connects that pointer, length and capacity to the new Bytes.
- The network wrappers move an input Vec through their coroutine and return it
  on both success and failure. Their result must remain conditional on the
  **actual input owner's proof**, not become independent for every Vec argument.

The original owning-payload program was rechecked on this base and still fails
at `block_on<Bytes>` (line 115), E04661 `TaskResultOriginsUnproven`. No source was
changed to avoid it. No new full-run numbers are claimed.

### Proposed trust boundary

Accept a private, source-visible **byte-owner operation contract**, limited to
the exact standard-library Vec specialization with builtin u8 elements and the
exact Bytes declaration. Declaration identity only selects the contract; it
never establishes value qualification. User homonyms, other element types,
raw imports and source-hidden declarations get no automatic witness.

The contract's unsafe implementation is responsible for allocation ownership,
initialized prefix and release discipline. The compiler checks operation
identity, instantiated signature, current owner/argument receipts, permitted
state transition and normal Sema success. It does **not** claim to have proved
arbitrary raw storage safe by inspecting an empty dependency set.

Contract registration must bind the exact source-visible declaration, full
physical layout, operation signature and supported implementation schema.
Changed/unrecognized implementations are unqualified, even in a trusted module.
This is an explicit trusted-library contract addition, not a new generic body
verification system or permission derived merely from a module/function name.

### Operation and responsibility table

| Existing operation | Required incoming evidence | Successful outgoing responsibility |
| --- | --- | --- |
| `Vec<u8>::new`, `with_capacity` | Contract-matching construction; fresh allocation or exact empty construction; no imported pointer | Fresh owner/storage receipt; initialized length zero, allocation alone never proves initialized bytes |
| Byte `push`, `resize`, internal `grow` | Current receiver receipt and normal write/PAL checks | Updated version of the same logical owner; growth transfers initialized prefix, retires old allocation, releases old storage once |
| `Bytes::from_vec(cede v)` | Actual consumed Vec receipt, exact raw export plus matching length/capacity, emptying source before publication | Same storage obligation on Bytes; source retired; no extra allocation or release |
| `Bytes::into_vec`, `Vec<u8>::take` | Current receiver receipt and verified extraction/emptying | Result owns prior storage; receiver has a new empty state; no two live owner receipts for that storage |
| Actual struct/enum packaging | A receipt for each byte-owner field and existing proof for every other live field | Field/variant-specific recipe, not a type-wide independent flag |
| Network write adapter | Incoming owner receipt, byte-view borrow valid across suspension | Same owner returned in either variant; execution borrow does not become result dependence on the stream |
| Network read adapter | Incoming owner receipt, prepared capacity, exact scoped FFI write and checked returned count | Only completed initialized prefix exposed; failure returns empty prefix with allocation responsibility retained |
| Drop / rejected handoff | Existing cleanup authority; state prior to attempted transfer on rejection | Exactly one existing cleanup path; no new Drop permission from this receipt |

`unsafe_into_raw` alone must not create a freely reusable ownership certificate.
Its export can discharge the exact `from_vec` adapter transition; an escaped raw
pointer or unmatched export loses qualification. `from_raw`, direct raw-field
construction and `unsafe_from_ffi_allocation` are **not fresh proof roots in this
first contract**. Supporting native adoption later requires its own explicit
ownership preconditions, not a name-based exception.

For network reads the current `unsafe_set_len(max_len)` before the read cannot
be called an initialized-prefix proof. Within the checked adapter transition it
is a preparation state, not publishable result qualification. Completion must
check `bytes_read <= prepared capacity/request`; errors publish length zero.
The adapter must neither expose/read unwritten bytes nor release storage while
the operation can still access it. Existing runtime completion/cancellation
semantics remain prerequisites; an unsupported exit must remain unqualified.
No new runtime or cancellation protocol is proposed.

### Concrete implementation delta (one package, not helper milestones)

| File / existing mechanism | Proposed change | Explicitly unchanged |
| --- | --- | --- |
| `include/toka/AST.h`, `include/toka/Sema.h` | Private immutable byte-owner receipt attached to actual values: exact type/owner identity, storage lineage, current state version, and operation prerequisites. Snapshot/current-binding state carries it; no default-valid boolean. | No ABI field, TKI serialization, shared AST type mutation or Copy classification |
| New `src/Sema/Sema_ByteBuffer.cpp`, `src/CMakeLists.txt` | Recognize/validate the finite operation contracts above and produce/discharge recipes. Fresh storage, preserving operation and ownership handoff are distinct cases. | No `closedTaskResultType(Vec/Bytes) = true`, recursive visited-set success or raw_take qualification |
| Existing successful binding/call/return commits and `AnalysisState` | Move receipts with actual values; invalidate on unmatched field writes, raw alias escape, unknown modifying calls, or stale version; failed operations restore the prior receipt. Joins require every incoming branch to qualify (alternative storage identities stay alternatives). | No replay of initializers by current names; no permission from witness identity |
| `Sema_TaskResult.cpp` plus existing selected-function summaries | Add conditional byte-owner/field recipes for checked results; all returns must satisfy them. Substitute each caller's actual receipt before converting the result proof to Independent. Preserve task requirements separately. | No empty-dependency inference, no `block_on` privilege, no task-frame lifetime extension |
| `Sema_Expr_Call.cpp` planner bridge | Supply verified dependency completeness only after recipe discharge; retain the full planner and rollback. | No direct Admitted override or weakened PAL/source/Drop checks |
| `lib/std/vec.tk`, `lib/std/bytes.tk`, `lib/std/net.tk` | Only contract-scoped adapter changes necessary to make export/retirement and read preparation/completion explicit; preserve public signatures and original programs. | No layout/runtime change, no arbitrary Vec<T> witness, no rewriting positives around the failure |

Raw writes that may alias a witnessed descriptor invalidate every possibly
affected receipt, not just the spelled root. Byte-content writes do not create
external pointer dependencies, but still require permission, valid bounds and
initialized-prefix handling. A stored raw alias is not a new owner. Borrowed
views continue to depend on the current owner; moving the owner into a task does
not legalize a still-live conflicting borrow.

Normal allocation failure/early-exit behavior must be preserved. Before a
handoff commits, the source retains its obligation; after commit only the target
does. A failed result-proof check restores analysis state and emits no artifact;
it does not invent runtime rollback after a successful unsafe transfer.

### Fixed completion matrix

- Both unchanged original programs: `g09_async_owning_payload_drop_regression`
  and `g13_net_buffer_abi_test`; the exact twelve-program net gate.
- Empty/nonempty, growth, freeze/thaw/take, actual struct and both Result variants;
  direct return, await, Wait, helper forwarding and repeated specialization calls.
- Successful and failed reads/writes; buffer identity/content/capacity retained;
  task-result disposal without extraction and after extraction, exact-once
  allocation cleanup (Document/owner Drop counters alone are insufficient).
- Forged same-name types, raw imports, overwritten buf/len/cap, escaped aliases,
  unknown mutation, unqualified branch, unmet caller prerequisites and
  source-hidden evidence: refuse qualification, never silently independent.
- Current Wait escape/static/selected-input/frame tests, readonly/nullable/PAL,
  repeated consumption, normal/shadow parity, rejected object/IR non-production
  and failure rollback remain mandatory.
- JSON `empty_storage_mutated` and existing CSV/task safety controls stay intact.

After targeted convergence, run one fixed-candidate full comparison. The ledger
remains four positives restored and two newly failing; the new network CTest
failure is the same buffer program, not a third independent regression.
Build/TOML/Template, source-hidden callable, E, push and freeze remain out of scope.

Implementation authorization includes the complete chain and production
admission. No further helper-level authorization is required. The current
implementation uses exact, compiler-pinned source-content schema seals for the
three reviewed modules, together with resolver declaration identity, physical
layout, concrete signatures and normally completed definition validation.
This deliberately rejects even otherwise harmless source edits until the
operation contract is rechecked; it is not a module/type-name whitelist or a
library-supplied fingerprint. No witness is serialized to TKI.

Byte-buffer receipts grant only result-independence facts. Ownership transfer,
permissions, initialization/consumption and generated cleanup remain governed by
their existing plans. Network preparation now initializes the requested prefix
with the existing byte resize operation; both TCP and TLS check the completed
count against request and capacity before publishing it. No runtime change.
