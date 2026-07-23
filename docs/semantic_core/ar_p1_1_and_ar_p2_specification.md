# Async Runtime Phase 1.1 (AR-P1.1) & Phase 2 (AR-P2) Specification

Status: `AR-P1.1 Concurrency Gate Complete; AR-P2 WaitRegistry Roadmap Frozen`

---

## 1. AR-P1.1 Concurrency Entrance Gate Specification

AR-P1.1 eliminates the `PREPARING` lost-wake race window and enforces atomic coroutine suspension handshakes across multi-threaded executors and timer/reactor wakeups.

### 1.1 Unified CAS State Machine (`TokaTCBState`)
The TCB state is represented by atomic state transitions:
- `TOKA_TCB_CREATED = 0`: Initial cold task state.
- `TOKA_TCB_RUNNING = 1`: Task currently executing on stack.
- `TOKA_TCB_SUSPENDED = 2`: Task committed to suspended state.
- `TOKA_TCB_QUEUED = 3`: Task in runtime ready queue awaiting execution.
- `TOKA_TCB_COMPLETED = 4`: Task execution finished.
- `TOKA_TCB_PREPARING = 5`: Task preparing to suspend (caller registering timer/reactor).
- `TOKA_TCB_PREPARING_WITH_PENDING_WAKE = 6`: Wakeup event arrived while task was in `PREPARING`.

### 1.2 Handshake Invariants
1. **Prepare Phase (`toka_task_prepare_suspend`)**: Atomic transition `RUNNING -> PREPARING`.
2. **Wake Event (`toka_task_try_schedule`)**:
   - If `state == PREPARING`: Atomic CAS `PREPARING -> PREPARING_WITH_PENDING_WAKE`.
   - If `state == SUSPENDED`: Atomic CAS `SUSPENDED -> QUEUED`, enqueues to ready queue.
3. **Commit Phase (`toka_task_commit_suspend`)**:
   - If `state == PREPARING`: Atomic CAS `PREPARING -> SUSPENDED`.
   - If `state == PREPARING_WITH_PENDING_WAKE`: Atomic CAS `PREPARING_WITH_PENDING_WAKE -> QUEUED`, enqueues directly to ready queue.

---

## 2. AR-P2 `WaitRegistry` & `TimerHeap` Data Model Specification

AR-P2 introduces a generation-based `WaitRegistry` and `TimerHeap` tokenization without IO reactor or `race2` scope inflation.

### 2.1 `WaitToken` & `WaitRegistration`
```c
typedef struct {
    uint32_t wait_id;               // Slot index into WaitRegistry
    uint32_t wait_slot_generation;  // Slot generation for stale invalidation
} WaitToken;

typedef struct {
    WaitToken token;
    uint64_t task_id;
    uint64_t task_schedule_generation;
    _Atomic uint32_t state;         // WAITING = 0, WON = 1, CANCELLED = 2, EXPIRED = 3
    uint16_t source_tag;            // TIMER = 1, REACTOR_READ = 2, REACTOR_WRITE = 3
    void *wait_set;                 // Reserved for AR-P5 WaitSet winner CAS
    TokaTCB *tcb;                   // Retained strong TCB pointer while registration active
} WaitRegistration;
```

### 2.2 OS Reactor 64-bit Userdata Mapping
For future AR-P3, `epoll_event.data.u64` and `kevent.udata` map 1-to-1:
- High 32 bits = `wait_id`
- Low 32 bits = `wait_slot_generation`
This guarantees $O(1)$ zero-indirection lookup and zero memory allocations.

---

## 3. AR-P2 Phased Roadmap (Strict RFC Scope)

1. **Step 1**: `WaitRegistry` Core & Slot Lifetime (`allocate_slot`, `validate_and_retain`, `try_wake`, `invalidate`, `release`).
2. **Step 2**: `TimerHeap` Tokenization (`deadline + sequence + WaitToken`), $O(1)$ logical cancellation, and Tombstone memory compaction.
3. **Step 3**: Concurrency Redline Verification (20,000-iteration controlled permutation race probe, ASan/TSan clean).
