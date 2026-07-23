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

---

## Target Semantics (Pending Runtime Closure in Step 3-6)
- **Created Task Handle Reclamation**: Unstarted Created handle drop resource reclamation (Step 3).
- **Executor Shutdown & Detached Drain**: Normative detached task executor drain on root exit (Step 3).
- **Ready Queue Saturation Protection**: Dynamic ring-queue auto-expansion (Step 5).
