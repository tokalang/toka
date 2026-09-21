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
passed 3/3 (120.53 seconds). The combined fixed-candidate full run is pending.
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
