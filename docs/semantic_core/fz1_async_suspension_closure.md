# FZ-1 Async Suspension Closure & Cold-Task Execution Semantics

Status: `Phase 1 Async Suspension & Coroutine Lifetime Closure Complete`

`FZ-1` freezes the existing Toka 1.0 async suspension boundary and cold-task execution lifecycle. It adds no syntax and does not broaden task handoff. The goal is to make the already public `fn -> async T`, `.await`, `.wait`, and `.start` model explicit, test-locked, and consistent with PAL, ownership, effects, and `.tki` replay.

## Frozen Contract

- `.await` is valid only inside a function declared `-> async T`.
- `.wait` is a blocking consumer and is rejected inside an async function.
- `.await` consumes a task effect but does not end the current lexical scope or reset init, moved, or PAL state.
- Locals needed after suspension remain coroutine-frame state. Branch and loop merges, including `break` and `continue`, retain their normal conservative analysis across `.await`.
- A borrow active before suspension remains active after resume. Moving, ceding, or otherwise invalidating its source is rejected.
- Dependencies carried by an async result remain ordinary declared lifetime dependencies after `.await` or `.wait`.
- `.start` remains a detached execution boundary: non-borrowing scalars may cross by value, owned shapes/resources require a cede parameter and cede argument, and borrowed/raw/dependency-bearing state is rejected.
- Raw pointers remain outside the PAL safe-borrow guarantee.
- Async trait declarations are outside the frozen 1.0 surface. A trait method
  declared `-> async T` is rejected with `E0618` before trait dispatch metadata
  or a `.tki` can be produced; ordinary `fn -> async T` remains supported.

---

## Cold-Task Execution Timing

1. **Lazy Creation (Cold Task)**:
   - Calling an `async fn` constructs a `TaskHandle` holding a coroutine in the `Created` state.
   - The coroutine body is **NOT** executed during the function call. Initial suspend occurs before the first statement of the coroutine function body executes.

2. **Explicit Activation Boundary**:
   - A `Created` task transitions to running/scheduled exactly once upon `.await`, `.wait`, or `.start`.
   - `.start` enqueues the task into the runtime ready queue for execution by the executor. It **NEVER** executes the coroutine function body inline on the caller's stack frame.
   - Repeated calls to `.start` on an already started task are idempotent no-ops and will not re-trigger task body execution.
   - Continuation completion callbacks do **NOT** inline re-enter the calling function's stack frame.

---

## Task Handle Lifetime & Executor Shutdown

1. **Unstarted Handle Drop (`Created` State)** (Delivered & Verified in Step 3):
   - Dropping a `TaskHandle` in the `Created` state reclaims the coroutine frame and drops any frame-owned parameters (such as `cede` arguments).
   - Dropping an unstarted `TaskHandle` **NEVER** implicitly starts or schedules the task.

2. **Started Handle Drop (`Running` / `Suspended` State)**:
   - Dropping a `TaskHandle` for a task that has already been started (`.start`, `.await`, `.wait`) transfers ownership of the task to the runtime executor (detaching the task).
   - The detached task continues running until completion.

3. **Executor Shutdown**:
   - The runtime executor remains active until all runtime-owned detached tasks reach a completed state.
   - A permanent background detached task (e.g. infinite event loop) keeps `block_on` running until completion. Toka Phase 1 does not inject implicit task cancellation during executor shutdown.

---

## Implementation Closure

`AwaitExpr` and `WaitExpr` context rules and initial suspend guarantees:

- `.await` outside an async function is rejected in Sema with `E0715`.
- `.wait` inside an async function is rejected with `E04585`.
- Cold-task initial suspend returns a `Created` task handle directly without running body statements.

---

## Verification Evidence

- `tests/pass/g09_async_cold_task_semantics.tk` verifies cold creation side-effects (0 before start/block_on), non-inlined `.start` execution, idempotent `.start` calls, and side-effect execution upon `block_on` activation.
- `tests/pass/g09_async_suspension_state.tk` proves frame-local borrow use across `.await`.
- `tests/fail/trait_async_method.tk` and
  `tests/fail/trait_async_method_cross_module.tk` lock the local and imported
  async-trait exclusion with `E0618`.
- `tests/pass/g09_async_phase1_qualification_tests.tk` proves 20,000-deep await chain, timer bridge, and completion-before-registration.
- `tests/pass/g09_async_created_drop_reclaim_test.tk` verifies unstarted Created-handle parameter reclamation without executing the task body (`body_run_count == 0`, `global_drop_count == 1`).
