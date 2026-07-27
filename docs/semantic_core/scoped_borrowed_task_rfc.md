# RFC: Scoped Borrowed Tasks

**Status:** Proposed post-1.0 extension. No source syntax or runtime behavior
is changed by this document.

**Depends on:** frozen PAL path/dependency rules, async task completion, task
cancellation, and `@Send` / `@Sync` where work crosses OS threads.

## 1. Problem and boundary

Toka 1.0 intentionally treats `.start` and `thread_spawn` as detached
execution boundaries. They cannot carry `&`, `str`, `bytes`, raw pointers, or
values containing PAL dependencies. Owned work crosses only through explicit
transfer.

That rule is correct for detached work. It does not express a different, safe
pattern: a child whose execution is lexically contained in a parent scope and
is known to complete before the parent's borrowed locals are released.

This RFC proposes that pattern. It does **not** propose user-written lifetime
parameters, nor does it weaken detached `.start`.

## 2. Non-goals

- No implicit borrowing across ordinary `.start` or `thread_spawn`.
- No detached scoped child, escaping scoped `TaskHandle`, or background task
  hidden inside a scope-owned aggregate.
- No claim that scope containment provides `@Send`, `@Sync`, atomics, locks,
  or any other cross-thread synchronization property.
- No syntax decision in this RFC. Names such as `scope.spawn_borrowed(...)`
  are explanatory sketches only.

## 3. Core model

Entering a lexical task scope creates an unforgeable compiler-internal **scope
anchor** `S`. A scoped child is registered under `S`, rather than becoming a
detached task.

```text
scoped-child dependency = captures ∪ {S}
```

The compiler may implement this fact with an internal region/anchor identity,
but the source program need not spell a lifetime calculus. PAL continues to
reason about ordinary paths and dependencies; the new fact says that all
dependencies recorded for a scoped child must remain valid through the scope's
completion point.

### Invariants

1. A scoped child cannot outlive its anchor `S`.
2. Normal scope exit waits until every registered child is terminal.
3. Cancellation/error exit first requests child cancellation, then waits until
   every child is terminal and its cleanup has run.
4. A scoped child, its task handle, and any closure/aggregate containing its
   scope dependency cannot return, detach, cede to an outer task, or be stored
   into an escaping location.
5. Borrowed captures are checked against `S`; they are not permitted by a
   special exception to ordinary PAL conflicts.
6. If a child may execute on a different OS thread, its captures additionally
   satisfy the existing `@Send` / `@Sync` requirements. Lexical completion is
   not a data-race proof.

## 4. Runtime contract

The existing `TaskScope` owns runtime references and can cancel/drain children,
but it currently does not carry lexical borrowing semantics. A scoped-task
implementation must add a separate static/runtime handshake:

```text
enter scope S
  register child with S
  run child work
exit scope S
  join normally, or cancel then join on unwinding
  release S only after all children are terminal
```

For async parents, scope exit must be an explicit suspension-capable operation
or be lowered into the parent coroutine's cleanup path. The implementation must
not silently block an executor worker. Synchronous thread scopes may join
synchronously under their own execution contract.

## 5. Static checks

The first compiler slice should reject:

- returning a scoped task or task reference;
- invoking detached `.start` on a scoped child;
- placing a scoped task/dependent closure in an outer variable, heap object,
  global, return value, or detached task capture;
- moving/ceding a parent path while a scoped child borrows it;
- ending the scope with an unjoined child on a path where no cleanup lowering
  proves terminal completion.

It should accept a borrowed child only when the scope completion operation is
on every continuing exit path.

## 6. Implementation slices

1. **Specification-only:** freeze the scope-anchor and non-escape invariants.
2. **Single-executor async slice:** a lexical scope may host children that
   borrow a parent local; normal exit awaits all children; cancellation joins
   before parent cleanup.
3. **Escape diagnostics:** cover return, field storage, closure capture,
   `cede`, detached `.start`, and branch/guard joins.
4. **OS-thread slice:** add scoped threads only after the `@Send` / `@Sync`
   interaction and join behavior have independent evidence.

## 7. Acceptance evidence

The first executable acceptance suite must include:

- a child reading a borrowed parent local and producing a result before scope
  exit;
- parent cleanup after the scope, proving the borrow is no longer live;
- rejected task-handle return, detached start, aggregate escape, and
  parent-move-during-child cases;
- cancellation and panic/error-path cleanup with exactly-once child drop;
- a source-less interface/replay case that preserves the non-escape contract.

## 8. Open questions deliberately deferred

- Exact surface syntax and whether scopes are closures, blocks, or library-led
  constructs.
- Result/error aggregation and sibling cancellation policy.
- Nested scopes, dynamic child creation, and work stealing.
- Scoped tasks that themselves yield borrowed results.

None of these questions justifies weakening the 1.0 detached-boundary rule.
