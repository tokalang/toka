# Phase 1 CodeGen ABI Baseline Approval & Runtime Lifecycle Specification

> **Official Status**: **CodeGen 参数 ABI 与 Borrow/Cede 基线批准，P0=0；Phase 1 runtime lifecycle 仍有 8 项 P1/语义文档工作待闭合。**

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

---

## 3. Phase 1 Runtime Lifecycle & Specification Worklist (8 Items)

1. **Untaken Promise Result Destruction**: Drop unconsumed return values upon frame reclamation for completed detached tasks.
2. **Created Task Handle Drop Reclamation**: Drop unconsumed `cede` parameters and reclaim `Created` tasks when handle is dropped prior to starting.
3. **Executor Shutdown & Detached Task Drain Semantics**: Define normative executor shutdown semantics for root completion vs active background detached tasks.
4. **Ready Queue Overflow Retention**: Ensure `toka_task_start` and continuation wakeups do not lose schedule attempts on queue overflow.
5. **Concurrent Detach Transient Reference**: Acquire transient retain in `toka_task_detach` before accessing TCB fields.
6. **Prepare / Commit Handshake**: Complete `toka_task_prepare_suspend` and `toka_task_commit_suspend` two-phase handshake in `toka_rt.c`.
7. **`cede ~T` True-Move Transfer**: Suppress source handle drop without double retain for ceded shared arguments.
8. **Cold-Task Specification & Side-Effect Suite**: Write normative documentation, release notes, and side-effect timing tests for lazy initial suspend.
