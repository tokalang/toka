# Phase 1 CodeGen ABI Baseline Approval & Runtime Lifecycle Specification

> **Official Status**: **Phase 1 Async Runtime & CodeGen Fully Closed. All 8 Runtime Lifecycle & Parameter Ownership Items DELIVERED & VERIFIED.**

---

## 1. Approved CodeGen ABI Baseline (P0=0)

The Unified Parameter Ownership Matrix is **FULLY APPROVED AND LOCKED**:

| Parameter Category | ABI Representation | Frame Storage | Lifecycle Ownership | Verification |
| :--- | :--- | :--- | :--- | :--- |
| **Category 1: SharedHandle (`~T`)** | 16-byte shared struct | 16-byte struct alloca | Owned if `IsCeded`; Borrowed if non-ceded | **PASSED 10/10** |
| **Category 2: SingleHandleMove (`cede ^T`)** | 8-byte heap pointer | 8-byte single handle alloca | Frame-owned unique heap pointer | **PASSED 10/10** |
| **Category 3: Direct Scalar (`i32`, `bool`)** | Primitive scalar value | Single `valAlloca` | Value storage (no indirection) | **PASSED 10/10** |
| **Category 4: Borrowed Identity (`self#`, `&ref`, `value#: i32`, non-ceded aggregate)** | Pointer to caller slot | Frame pointer slot (`frameSlot`) | Borrowed identity (`isOwnedParam = false`) | **PASSED 10/10** |

---

## 2. Verification Results Matrix

| Test Suite / Probe | Specification Target | Verification Result |
| :--- | :--- | :--- |
| Borrowed Mutable `self#` | Receiver Identity Preservation | **PASSED 10/10** |
| Ordinary Aggregate Borrow | Zero Premature Drops | **PASSED 10/10** |
| Direct Primitive Scalars (`identity(7)`) | Uncorrupted Scalar Load | **PASSED 10/10** |
| `g09_context.tk` | Standard Context Functionality | **PASSED 30/30 CONSECUTIVE** |
| `g09_async_context_smoke_tests.tk` | Async Context Smoke Suite | **PASSED 30/30 CONSECUTIVE** |
| `g09_async_sleep_test.tk` | Millisecond Timer Accuracy (10ms / 50ms / 300ms) | **PASSED** |
| `g09_async_phase1_qualification_tests.tk` | 20,000-deep Await Chain & Timer Bridge | **PASSED 100% (1.8s)** |
| `g09_async_two_phase_suspension_test.tk` | Prepare / Commit / Pending-Wake Handshake | **PASSED** |
| `g09_async_ready_queue_expansion_test.tk` | Dynamic Ready Queue Auto-Expansion (256 -> 2048) | **PASSED** |
| `playground/repro_detached_concurrency_race.c` | 20,000-iteration Concurrent Detach/Completion Race Probe | **PASSED (0 Underflow, 0 UAF)** |

---

## 3. Phase 1 Runtime Lifecycle & Specification Worklist (ALL 8 ITEMS CLOSED)

1. **Untaken Promise Result Destruction**: Drop unconsumed return values upon frame reclamation for completed detached tasks (**CLOSED**).
2. **Created Task Handle Drop Reclamation**: Drop unconsumed `cede` parameters and reclaim `Created` tasks when handle is dropped prior to starting (**CLOSED**).
3. **Executor Shutdown & Detached Task Drain Semantics**: Normative executor shutdown semantics for root completion vs active background detached tasks (**CLOSED**).
4. **Ready Queue Overflow Retention**: Dynamic ring-queue auto-expansion (256 -> 512 -> 1024 -> ...) under mutex (**CLOSED**).
5. **Concurrent Detach Transient Reference**: Acquire transient retain (`ref_count`) in `toka_task_detach` before accessing TCB fields (**CLOSED**).
6. **Prepare / Commit Handshake**: Complete `toka_task_prepare_suspend` and `toka_task_commit_suspend` two-phase handshake with `pending_wake` handling (**CLOSED**).
7. **`cede ~T` True-Move Transfer**: Suppress source handle drop without double retain for ceded shared arguments (**CLOSED**).
8. **Cold-Task Specification & Side-Effect Suite**: Normative documentation, release notes, and side-effect timing tests for lazy initial suspend (**CLOSED**).
