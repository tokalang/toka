# Toka Async Runtime P5 Specification: Task Cancellation, Join Substrate & Structured Concurrency

**Document Status**: Draft / Runtime Closure Pending  
**Target Release**: Toka v0.9.5-P5  

---

## 1. Executive Summary

This specification defines the formal mechanics for:
1. **Linearized Task Cancellation**: Atomic cancellation of active tasks competing through `WaitRegistry` winner CAS.
2. **Completion Subscription & Join**: Subscription mechanism waking waiters upon task completion or cancellation.
3. **N-Way WaitSet Allocation**: Atomic allocation of $N$-slot wait sets for multi-way race combinators.
4. **Loser Cleanup Guarantee**: Absolute invariant that `race2` / `select2` combinators do not return to caller until loser tasks are fully terminated and disarmed.
5. **Structured Concurrency (`TaskScope`)**: Retained task lifetime management guaranteeing zero orphan tasks and zero UAF (Use-After-Free).

---

## 2. Cancellation Linearization Architecture

### 2.1 TCB State Machine & Active Registration Link
Every `TokaTCB` tracks:
- `_Atomic uint32_t state`: `TOKA_TCB_CREATED` (0), `TOKA_TCB_RUNNING` (1), `TOKA_TCB_SUSPENDED` (2), `TOKA_TCB_QUEUED` (3), `TOKA_TCB_COMPLETED` (4), `TOKA_TCB_PREPARING` (5), `TOKA_TCB_COMPLETED_CANCELED` (7).
- `_Atomic uint8_t cancel_requested`: atomic boolean flag.
- `_Atomic uint8_t cancel_handled`: set only when the current coroutine
  explicitly observes cancellation through `.await?`; it permits normal
  domain completion instead of `TOKA_TCB_COMPLETED_CANCELED`.
- `_Atomic uint32_t active_wait_id` and `_Atomic uint32_t active_slot_gen`: links suspended task to its active `WaitRegistration` / `WaitSet`.

### 2.2 Linearization Invariant
When `toka_task_request_cancel(tcb)` is invoked:
1. `cancel_requested` is atomically set to `1`.
2. If `state == TOKA_TCB_SUSPENDED` and `active_wait_id != TOKA_NO_WAIT_ID` (0xFFFFFFFF):
   - `toka_wait_registry_try_wake(active_wait_id, active_slot_gen)` is called.
   - If CAS on `WaitSet` succeeds, the task transitions to `TOKA_TCB_QUEUED` and is pushed onto the ready queue with cancellation wake status.
   - If CAS on `WaitSet` fails (e.g. IO or timer event won first), the cancellation flag remains set. Upon resuming, the task detects `cancel_requested == 1` and unwinds.
3. When unwinding, the coroutine executes cleanup destructors and transitions state to `TOKA_TCB_COMPLETED_CANCELED`.

---

## 3. Loser Cleanup Guarantee (`race2`)

For `race2(cede first, cede second) -> async RaceWinner`:
1. **Preparation**: Allocates 2-slot `WaitSet` bound to parent task `(task_id, wait_gen)`.
2. **Subscription**: Subscribes completion notifications for `first` and `second`.
3. **Suspension**: Two-phase suspend on parent coroutine.
4. **Winner Resolution & Loser Join**:
   - Inspects `toka_wait_registry_is_winner`.
   - Issues `task_cancel(loser)`.
   - **Mandatory Join**: Awaits `loser` until `toka_tcb_is_done(loser)` returns `true` (`TOKA_TCB_COMPLETED` or `TOKA_TCB_COMPLETED_CANCELED`).
   - Releases `WaitSet` slots.
   - Returns `RaceWinner::First(res1)` or `RaceWinner::Second(res2)`.

No caller receives the result of `race2` until the loser task is completely terminated and disarmed.

---

## 4. Retained Lifetime (`TaskRef` & `TaskScope`)

- `TaskScope` manages tasks using `TaskRef` objects holding explicit TCB reference counts (`tcb->ref_count`).
- Even if `TaskHandle` is detached or dropped by the caller, `TaskScope` retains valid references to the TCB.
- `scope.close().await`:
  1. Issues `cancel_all()`.
  2. Joins all tracked tasks until `toka_tcb_is_done` returns `true`.
  3. Releases retained TCB reference counts.
- Eliminates UAF (Use-After-Free) entirely.

## 5. Cancellation and Synchronous Wait Boundaries

- `race2` installs completion subscriptions before activating either input. It
  then idempotently starts both inputs, so cold `TaskHandle` values are valid
  race operands.
- A structured combinator registers each child TCB with its parent cancellation
  context. Parent cancellation snapshots and cancels all registered children;
  their timer/IO registrations are reclaimed before the canceled parent frame
  is released.
- Async `.await` propagates `CANCELED` through the current coroutine and does
  not fabricate a `T` payload. A synchronous `.wait`/`block_on` encountering
  an unhandled canceled task is a non-returning runtime error; callers needing
  recoverable cancellation must use `.await?`. This explicit outcome boundary
  produces `Option<T>`: `Some(T)` consumes a normal result and `None` captures
  a cancellation from the current or awaited task. A task that handles its own
  cancellation through this boundary may return a normal domain outcome; the
  runtime records that completion as `COMPLETED`, not as an unhandled canceled
  task.
