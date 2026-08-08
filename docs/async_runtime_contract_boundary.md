# Toka Async Runtime Contract Boundary (Pre-ABI)

**Status:** Frozen ownership ledger for the bounded async runtime baseline. This
document is intentionally a pre-ABI boundary: it names what the compiler may
rely on today, but does not promise a third-party runtime ABI or full TCB
conformance.

**Authority:** The transition and cleanup properties remain governed by
[`async_runtime_tcb_rfc.md`](async_runtime_tcb_rfc.md). The currently
implemented evidence and its limits are recorded in
[`async_runtime_p5_spec.md`](async_runtime_p5_spec.md). This ledger only
separates those semantic obligations from the default scheduler implementation.

## 1. Purpose and non-goal

Toka needs one reference/default runtime while its compiler-generated
coroutines, cancellation, result transfer, and cleanup protocol are being
validated end to end. Its global ready queue, mutex, timer/reactor loop, and
host pumping policy are useful parts of that implementation; they are not
language semantics.

This boundary prevents those two concerns from becoming accidentally
inseparable. It does **not** extract an executor trait, permit an external
runtime, alter scheduling behaviour, or claim that the current bounded
baseline completes the TCB RFC.

## 2. Ownership ledger

| Tier | Current owner | Contract status | Examples |
|---|---|---|---|
| Compiler/semantic runtime | compiler and `toka_rt.c` together | provisional internal contract | task/frame creation, start/wake admission, prepare/commit/abort suspension, wait-token invalidation, terminal publication, result claim/disposal, cancellation request, and retained release ordering |
| Default executor | `toka_rt.c` plus `core/task` and `std/task` driver code | implementation-private | global ready queue, `toka_task_pop_ready`, queue capacity/count probes, dequeue/resume loop, timer queue, reactor polling, and `__toka_spawn_blocking` |
| Test-only observability | native probes and test builds | never a production contract | queue counts, forced queue-publication preemption, raw slot inspection, and direct state-machine probes |

The first row means that a backend may not weaken the bounded properties
already recorded in P5: terminal publication has one winner; cold cleanup
precedes canceled publication; result ownership is claimed once; aborted
suspension has no active registration; and a covered queued epoch receives one
publication. It does **not** freeze the current C symbol list, token shape, or
memory layout as a public ABI.

The second row may change its data structures, queue discipline, thread model,
timer implementation, reactor, or host integration without changing Toka
source semantics. In particular, no library or package may infer a safety
property from ready-queue capacity, ordering, or global-mutex implementation.
`HostEventSource` remains in this tier: it supplies a bounded synchronous host
pump and has no authority to publish a task terminal state or choose task
cancellation semantics.

## 3. Quarantined compiler exception

`src/CodeGen/CodeGen_Decl.cpp` contains one deliberate 0.x exception. When an
`async main` has no `std/task` driver, its fallback emits a default-runner loop
that calls `toka_task_pop_ready`, resumes the dequeued coroutine, clears the
current task, and releases the queue retain. The fallback is needed to execute
an immediately-ready root without silently importing `std/task`.

This is a compiler implementation of the **default runner**, not an
alternative semantic path and not a stable runtime interface. Ordinary async
function, `.await`, cancellation, and completion lowering must not directly
consume a ready-queue item. `tools/scripts/test_async_runtime_boundary.py`
enforces that `CodeGen_Decl.cpp` is the sole CodeGen owner of the
`toka_task_pop_ready` symbol, and that the documented fallback remains present.

The exception is deliberately recorded rather than refactored now. Moving
`coro_resume` into a generic C runner would itself require a compiler/runtime
resume-shim ABI; inventing that ABI before the semantic contract is qualified
would only move the coupling to a less visible place.

## 4. Future extraction gate

External or replaceable runtime backends remain deferred. Before that work can
begin, a dedicated RFC must provide all of the following:

1. an opaque, versioned compiler/runtime contract rather than direct reuse of
   the current C entry points or TCB/frame layout;
2. a conformance suite for terminal, result, cancellation, wake, cleanup, and
   retained-lifetime behaviour independent of default queue ordering;
3. a `DefaultRootRunner`-style adapter that absorbs the quarantined `async
   main` fallback; and
4. a decision on the remaining TCB gates needed by the proposed backend
   surface, especially frame access and retained lifetime validation.

Until then, `toka_rt.c` is the sole official reference/default runtime. This
is a boundary against premature ABI freezing, not a promise that every future
runtime must resemble its queue or reactor.

## 5. Verification

Run:

```sh
python3 tools/scripts/test_async_runtime_boundary.py
```

The source-level gate is intentionally narrow. It validates the ownership
boundary and the one compiler exception; it is not evidence of runtime
correctness, which remains at the P5 native test gates.
