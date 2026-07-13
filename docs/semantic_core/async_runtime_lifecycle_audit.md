# Async Runtime Lifecycle Audit

Status: `Complete`

This audit hardens the runtime implementation of the frozen Toka 1.0 async
surface. It does not add timeout, join combinators, cancellation semantics, or
structured concurrency.

## Scope

The audited paths are `TaskHandle` drop and explicit detach, completed
coroutine-frame destruction, timer and reactor suspension, local resource
cleanup through `llvm.coro.destroy`, background context helpers, and the
pre-existing `TaskGroup.cancel_all` implementation.

## Findings

### Completed detached frames were not reclaimed

`TaskHandle` drop called `__toka_detach_task`, but that function was empty.
Coroutine locals were dropped on normal return, while the allocated coroutine
frame itself had no owner that would destroy it.

The scheduler now records live detached handles. A handle that is already done
is destroyed immediately; a suspended detached handle is destroyed by the
scheduler after completion. `detach_forget` now requires an explicit `cede`
transfer and registers the frame before clearing the caller-visible handle.

### Context helpers retained unowned raw state addresses

Parent-cancellation propagation and timeout helpers previously stored only a
raw address to shared cancellation state in detached coroutines. Dropping the
context and canceler before the helper completed could leave that coroutine
with a dangling address.

The helpers now receive an owned `Canceler` through a `cede` parameter. Its
shared state remains alive for the complete background-task lifetime.

### Forced TaskGroup cancellation was not compositional

Destroying a coroutine suspended directly at a timer or explicit suspension
does execute its local cleanup path. That shallow observation was insufficient.

When an outer task is suspended while awaiting an inner coroutine, destroying
only the outer frame leaves the inner promise's awaiter pointer targeting the
destroyed frame. The inner task can later complete and resume that stale
address. Reactor suspension has the corresponding registration problem.

Correct cancellation therefore requires an explicit cooperative contract,
awaiter unlinking, recursive child ownership, reactor deregistration, and a
defined result/cleanup model. Those are the Post-1.0 semantics already listed
in the freeze decision; they cannot be inferred from `llvm.coro.destroy`.

`TaskGroup`, `spawn_task`, and `get_handle_addr` have consequently been removed
from the 1.0 `std/task` surface. A negative fixture locks that classification.
This removes an unsafe pre-1.0 library experiment; it does not change the
frozen `.await`, `.wait`, or `.start` language model.

### Reactor registration failure was ignored

Async connect, accept, read, and write previously suspended even when the
platform reactor rejected registration. They now return their existing failure
form instead of suspending forever. No new network error type is introduced.

## Evidence

- `g09_async_detached_lifecycle.tk` exercises explicit detached ownership,
  completion, and exact local cleanup.
- `task_group_cancellation_post_1_0.tk` proves the removed experiment is not
  part of the importable 1.0 task surface.
- Existing context, suspension-state, resource-cleanup, async network, HTTP,
  and WebSocket fixtures provide regression coverage.
- Source and same-version interface replay continue to enforce the frozen
  async `cede` handoff rules.

## Stop Decision

This audit stops after normal task/frame ownership and background-state
lifetime are closed and unsafe forced cancellation is removed from the 1.0
surface. Future timeout, cancellation, task groups, async join combinators, or
structured concurrency require a separate design decision and cannot reopen
this implementation audit implicitly.
