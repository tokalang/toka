# Toka Phase 1 Async Runtime & CodeGen Release Notes

## Summary

Toka Phase 1 establishes the unified async parameter ownership ABI, lazy cold-task execution timing, and CAS-linearized TaskHandle ownership release.

## Key Delivered Capabilities

### 1. Unified Async Parameter Ownership ABI (P0=0)
- **Direct Scalars (`i32`, `bool`)**: Materialized into single value allocas (`valAlloca`).
- **Single Handle Move (`cede ^T`)**: Materialized into single 8-byte handle allocas holding heap pointers.
- **Shared Handles (`~T`)**: Materialized into full 16-byte `{ptr, count_ptr}` struct allocas. Ceded shared handles transfer ownership (`isOwnedParam = true`); non-ceded shared handles are borrowed (`isOwnedParam = false`).
- **Borrowed Identity (`self#`, `&ref`, `value#: i32`, non-ceded aggregates)**: Preserves identity pointer address on coroutine-frame pointer slots (`frameSlot`).

### 2. Cold-Task Execution Timing
- Calling an `async fn` constructs a task in the `Created` state without executing body statements.
- `.await`, `.wait`, or `.start` activates execution.
- `.start` enqueues tasks to the runtime ready queue without synchronous inlined stack execution.

### 3. Task Handle Ownership Release
- Atomic `compare_exchange_strong` on `owner_released` ensures exactly-once release when tasks are detached and completed.

### 4. Unstarted Created Task Handle Reclamation (Step 3)
- Dropping an unstarted `TaskHandle` (in `Created` state) unwinds frame-owned `cede` parameters without executing task body (`body_run_count == 0`, `global_drop_count == 1`).

### 5. Executor Shutdown & Detached Task Drain (Step 4)
- Runtime executor tracks active detached tasks (`g_active_detached_task_count`).
- `block_on` continues pumping ready queue, timers, and reactor events until all runtime-owned detached tasks complete before shutting down.

---

## Target Semantics (Pending Runtime Closure in Step 5-6)
- **Ready Queue Saturation Protection**: Dynamic ring-queue auto-expansion (Step 5).
