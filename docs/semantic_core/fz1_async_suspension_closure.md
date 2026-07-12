# FZ-1 Async Suspension Closure

Status: `Complete`

`FZ-1` freezes the existing Toka 1.0 async suspension boundary. It adds no
syntax and does not broaden task handoff. The goal is to make the already
public `fn -> async T`, `.await`, `.wait`, and `.start` model explicit,
test-locked, and consistent with PAL, ownership, effects, and `.tki` replay.

## Frozen Contract

- `.await` is valid only inside a function declared `-> async T`.
- `.wait` is a blocking consumer and is rejected inside an async function.
- `.await` consumes a task effect but does not end the current lexical scope or
  reset init, moved, or PAL state.
- Locals needed after suspension remain coroutine-frame state. Branch and loop
  merges, including `break` and `continue`, retain their normal conservative
  analysis across `.await`.
- A borrow active before suspension remains active after resume. Moving,
  ceding, or otherwise invalidating its source is rejected.
- Dependencies carried by an async result remain ordinary declared lifetime
  dependencies after `.await` or `.wait`.
- `.start` remains a detached execution boundary: non-borrowing scalars may
  cross by value, owned shapes/resources require a cede parameter and cede
  argument, and borrowed/raw/dependency-bearing state is rejected.
- Raw pointers remain outside the PAL safe-borrow guarantee.

Richer cancellation, async blocks, parameterized `.start`, structured
concurrency, and new task-handoff syntax remain post-1.0.

## Implementation Closure

`AwaitExpr` and `WaitExpr` had two Sema handling paths. The active path checked
task-handle types but omitted function-context rules, while a later unreachable
path contained the intended context checks. `FZ-1` moves those checks into the
active path and removes the duplicate:

- `.await` outside an async function is rejected in Sema with `E0715` instead
  of reaching the same rejection during CodeGen.
- `.wait` inside an async function is rejected with the dedicated `E04585`
  diagnostic.

No previously valid use of `.await` or non-async `.wait` changes meaning.

## Verification Evidence

- `tests/pass/g09_async_suspension_state.tk` proves frame-local borrow use,
  branch initialization, loop-break initialization, and continue-state
  preservation across `.await`.
- `tests/fail/async_suspension_borrow_move.tk` proves an active borrow survives
  suspension and blocks a later move (`E0440`).
- `tests/fail/async_suspension_branch_move_state.tk` proves branch-moved state
  survives suspension and merge (`E0438`).
- `tests/fail/async_suspension_maybe_unset.tk` proves suspension cannot turn a
  maybe-unset path into an initialized path (`E0410`).
- `tests/fail/async_suspension_continue_move_state.tk` proves suspension before
  a continue backedge does not erase the loop move restriction (`E04501`).
- `tests/fail/async_await_requires_async_function.tk` and
  `tests/fail/async_wait_inside_async.tk` lock the async color/consumer boundary.
- `tests/semantics/tki_replay/cases/async_suspend_001_return_deps` checks
  `.await` and `.wait`, invalidation after consumption, `.start` rejection, and
  source/source-less semantic-evidence equivalence.
- `tests/semantics/tki_replay/cases/async_start_001_cede_handoff` continues to
  prove explicit owned handoff through both sides of the cede contract.

Verification snapshot:

- compiler build: passed;
- focused and full negative suite: 225 passed, 0 failed;
- warning suite: 1 passed, 0 failed;
- semantic evidence: passed;
- source/source-less semantic replay: 9 passed, 0 failed;
- full positive suite: 311 passed, with only the three pre-existing
  environment/runtime network cases recorded as `FZ-3-T01` still failing.

## Decision

`FZ-1` is complete. The current async model is a frozen 1.0 contract rather
than an open suspension-design topic. Further async expressiveness is deferred;
the next closure phase is `FZ-2` semantic combinations and TKI equivalence.

Milestone commit subject: `feat: freeze Toka 1.0 async suspension semantics`.
