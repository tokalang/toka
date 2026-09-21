# Wait across a helper: one pending contract decision

Status: proposed, not implemented or accepted. No change to phase E, runtime,
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

This needs a scope decision before production admission: it introduces the
task-result projection relation which the current helper boundary lacks. The
anonymous-record work can be completed independently without this contract.
