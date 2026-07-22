# RFC: Toka Async Runtime Refactoring (TCB, Generation-based WaitTokens, and Cooperative Cancellation)

- **Status**: Approved (Phase 0 Complete - Final Locked Specification)
- **Target Release**: Toka v1.0-async
- **Authors**: Toka Core Runtime Team

---

## 1. Overview & Core Motivation

Toka's current async runtime supports basic coroutine lowering, `.await`, `.start`, epoll/kqueue/select event loops, and basic timer functions. However, under high concurrency, racing event conditions, or task cancellation, the runtime exhibits several safety and correctness gaps:

1. **Raw Frame Address Passing**: OS Reactors and Timers directly store raw coroutine frame pointers (`void*`), leading to Use-After-Free (UAF) if a task completes or is destroyed before a late IO/Timer event fires.
2. **Inline Resumption Reentrancy**: Completing an awaited coroutine directly calls `coro_resume` on the awaiter inline, causing deep callstacks and reentrancy deadlocks/double-resumptions.
3. **Lack of Cancel/Timeout Coordination**: Timers cannot be efficiently canceled without $O(N)$ heap scans, and cancelled I/O registrations can cause stale wakeups on reused file descriptors.

This RFC freezes four non-negotiable runtime invariants and establishes the technical blueprint for Phase 1 through Phase 6.

---

## 2. The Four Non-Negotiable Invariants

1. **No Raw Frame Addresses**: OS Reactor (epoll/kqueue/select) and Timer Heap MUST NEVER store raw coroutine frame addresses.
2. **Scheduler Queueing Only**: All wakeups MUST go through `Scheduler.try_schedule(task_id, task_schedule_generation)`. Event sources and completion paths CANNOT directly invoke `coro_resume`.
3. **Exactly-Once Scheduling**: A single suspension epoch (`task_schedule_generation`) enters the ready queue AT MOST ONCE. This is enforced by an atomic TCB state transition `Suspended(gen) → Queued(gen)` (not by ready-queue scanning).
4. **Frame Life-Cycle Bound**: A coroutine frame is freed ONLY AFTER:
   - Task reaches terminal completion (`TCBState == Completed`),
   - ALL `WaitRegistration`s for the task are inactive (`no_active_registration`), AND
   - Task result has been consumed OR the `TaskHandle` has been explicitly detached (`result_consumed || handle_detached`).

---

## 3. Data Structures, Disambiguation & State Machine

### 3.1 Generation Terminology Disambiguation

To prevent epoch collision, generation counters are strictly split into two independent domains:

1. `wait_slot_generation` (u64): Monotonically managed by `WaitRegistry` slots. Validates token freshness when events fire.
2. `task_schedule_generation` (u64): Monotonically managed by `TaskControlBlock` (TCB). Incremented at every async suspension point (`suspend_and_register_wait`) to guarantee idempotent ready-queue scheduling.

### 3.2 Data Structure Definitions

```toka
shape TaskControlBlock (
    id: u64,
    frame: *void,
    task_schedule_generation: u64,
    state: TCBState,
    cancel_requested: bool,
    result: TaskResult, // None, Ok, Canceled
    result_consumed: bool,
    handle_detached: bool,
    active_wait_id: u64,
    join_waiter_id: u64
)

shape WaitToken (
    wait_id: u64,
    wait_slot_generation: u64
)

shape WaitRegistration (
    token: WaitToken,
    task_id: u64,
    task_schedule_generation: u64,
    state: WaitState,
    source_tag: u32
)
```

### 3.3 Strict TCB & Wait State Transition Matrices & CAS Order

#### 3.3.1 TCBState Lifecycle Matrix

| Current State | Target State | Guard / Action | Valid / Illegal |
| :--- | :--- | :--- | :--- |
| `Created` | `Queued(1)` | `start_task(tid)` (Enqueues task with gen=1) | Valid |
| `Queued(gen)` | `Running(gen)` | `pop_worker_task()` (Requires `Queued`, dequeues task) | Valid |
| `Running(gen)` | `Suspended(gen+1)` | `suspend_and_register_wait(tid)` (Requires `Running`, increments gen, requires slot `active == false`) | Valid |
| `Suspended(gen)` | `Queued(gen)` | `try_wake` / `try_schedule` (Atomic CAS) | **Valid (Single transition allowed)** |
| *Any except Suspended(gen)* | `Queued(gen)` | Duplicate `try_schedule` attempt | **REJECTED (Returns false)** |
| `Running(gen)` | `Completed(Result)`| `publish_result(tid)` (Requires `Running`, finishes cleanup) | Valid |
| `Completed` | *Any* | Any further transition attempt | **ILLEGAL** |

#### 3.3.2 WaitState CAS Order & Cancellation Side Effects

> [!IMPORTANT]
> **CAS-First Order Requirement**:
> `try_wake` MUST execute the atomic CAS transition `WaitState::Waiting → Won(reason)` BEFORE applying any TCB side effects (such as setting `cancel_requested`).
> ONLY the winning event source whose CAS successfully transitions `WaitState` out of `Waiting` is permitted to set `cancel_requested` and execute `try_schedule`.

| Current State | Target State | Trigger / Action Order | Valid / Illegal |
| :--- | :--- | :--- | :--- |
| `Waiting` | `Won(Ready)` | CAS succeeds $\to$ `try_schedule` | Valid |
| `Waiting` | `Won(Timeout)` | CAS succeeds $\to$ `try_schedule` | Valid |
| `Waiting` | `Won(Canceled)` | CAS succeeds $\to$ **Sets `TCB.cancel_requested = true`** $\to$ `try_schedule` | Valid |
| `Won(*)` | `Won(*)` | Trailing event arrival | **NO-OP (CAS fails, returns false, NO side effects)** |

---

## 4. Complete vs Cancel Linearization Semantics

Cancellation is a cooperative request, NOT an immediate terminal state.

1. **Complete-First Execution Path**:
   - Event/Task completion linearizes first via `try_wake(token, Ready)`.
   - CAS transitions `WaitState` to `Won(Ready)`. `TCB.cancel_requested` remains `false`.
   - Task body finishes normally and calls `publish_result(tid, Ok)`.
   - Trailing cancel attempts fail the `WaitState` CAS, returning `false` without setting `cancel_requested`.
   - Result is published **EXACTLY ONCE** as `Ok`.

2. **Cancel-First Execution Path**:
   - Cancellation linearizes first via `try_wake(token, Canceled)`.
   - CAS transitions `WaitState` to `Won(Canceled)`.
   - **ONLY UPON CAS WIN**: Sets `TCB.cancel_requested = true` and schedules the task.
   - Worker dequeues task, observes `cancel_requested == true`, runs scope cleanup/drop handlers, and calls `publish_result(tid, Canceled)`.
   - Trailing completion attempts fail the `WaitState` CAS.
   - Result is published **EXACTLY ONCE** as `Canceled`.

---

## 5. WaitRegistration Recycling & Frame Memory Protocol

> [!IMPORTANT]
> **Active Slot Overwriting Guard**:
> `suspend_and_register_wait` MUST verify that any existing registration slot at `slot_idx` is inactive (`active == false`) BEFORE overwriting.
> Overwriting an active `Waiting` slot is strictly prohibited to prevent orphan tasks.

1. **Recycling Sequence**:
   - Step 1: `WaitState` leaves `Waiting` via CAS (`Won(Ready)`, `Won(Timeout)`, or `Won(Canceled)`).
   - Step 2: Task scheduling and execution decision completes.
   - Step 3: Registration slot is logically invalidated, and `WaitRegistry` increments `wait_slot_generation` (`gen = gen + 1`).
   - Step 4: Physical OS Reactor unregistration executes asynchronously (safe because trailing kernel events carry only the old `WaitToken`).
2. **Frame Life-Cycle Guard**:
   - Coroutine frames can ONLY be marked freed when ALL 3 conditions hold:
     1. `tcb.state == Completed`,
     2. `no_active_registration` for task, AND
     3. `result_consumed || handle_detached`.
   - `try_wake` MUST validate `wait_slot_generation` BEFORE accessing TCB or coroutine frame pointers.
   - Trailing/stale token events immediately return `false` with ZERO TCB/frame dereferences.

---

## 6. TaskHandle Ownership & Detach Policy

1. **Phase 1–4 Backward Compatibility**: `TaskHandle::drop` retains `detach` semantics (task continues running in background until completion).
2. **Result Recycling**: If a detached task completes (`handle_detached == true`), its return value is dropped and TCB/frame resources are freed immediately.
3. **Structured Concurrency**: `TaskScope` (introduced in Phase 5) provides parent-child lifecycle binding where child tasks receive automatic cancellation if the scope drops.

---

## 7. Phased Implementation Roadmap & Scope Freezes

### Phase 1: Minimal CodeGen Promise Hook + TCB & Unified Ready Queue
- Introduce `TaskControlBlock` (TCB) with atomic `Suspended(gen) → Queued(gen)` state machine and `Scheduler` Ready Queue.
- **CodeGen Realignment**: Introduce minimal runtime hooks (`task_complete`, `task_yield`) so coroutine completion queues awaiter to Ready Queue instead of calling inline `coro_resume`.

### Phase 2: Generation-based WaitRegistry & Timer Heap
- Implement `WaitRegistry` with `wait_slot_generation` lazy invalidation.
- TimerHeap stores `WaitToken` instead of raw frame pointers.

### Phase 3: Multi-Platform Reactor Tokenization
- Tokenize epoll (Linux) and kqueue (macOS) userdata to `WaitToken`.
- **Windows Baseline Freeze**: Phase 3 tokenizes existing Windows `select` Reactor. Migration to IOCP is explicitly decoupled as a future milestone.

### Phase 4: Context & IO Timeout Integration
- Integrate `CancellationToken` with `WaitRegistry`.
- Update `TcpStream` async IO operations to use Reactor tokens and timeouts.

### Phase 5: `race2` / `select2` & Structured `TaskScope`
- Implement `race2` and `select2` with explicit shape results.
- Loser cleanup guarantee before `race2` returns.

### Phase 6: CodeGen ABI Convergence & Cross-Module Stabilization
- Finalize C-ABI runtime hooks. Remove legacy direct promise layout access in CodeGen.
- Verify `.tki` cross-module serialization stability.
