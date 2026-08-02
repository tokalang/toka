# Toka 1.0 Callable Protocol Closure

Status: `Complete`

## Decision

Toka uses one implicit-prelude `@Callable` protocol. Invocation permission is
not split into `Fn`, `FnMut`, and `FnOnce` traits; it is carried by the existing
receiver morphology:

- `call(self, ...)` and `fn(...)` are shared and repeatable;
- `call(self#, ...)` and `fn#(...)` require exclusive repeatable access;
- `call(cede self, ...)` and `cede fn(...)` consume the callable.

The `#` in `fn#(...)` is callable capability. A binding still needs its own
`#` to grant exclusive access, for example `auto f# = { => 0 }:fn#()` and
`f#()`.
These facts are represented independently in the type checker.

## Closure Inference And Ownership

Closure bodies infer the least receiver permission that can execute them:

- capture reads remain shared;
- assignment, writable arguments, or `self#` methods rooted at a capture make
  the closure exclusive;
- a `cede` operation rooted at a capture makes the closure consuming.

Capture ownership and invocation permission are separate. Explicit cede
capture moves storage into the environment even when the captured name is not
read by the body. It becomes consuming only when invocation transfers that
storage.

For consuming invocation, CodeGen installs one drop-live record per owned
capture. A moved capture disables only its own record; all remaining captures
are cleaned on every normal return path. The consumed source closure is not
dropped again.

## Generic And User Callables

Closure shapes implement `@Callable` automatically. A user shape becomes
callable only through an explicit `impl Type@Callable` containing `call`; a
same-named inherent method alone is rejected with `E04593`.

Generic functions may constrain `F: @Callable`. Concrete user callables retain
their receiver contract through specialization. Erased `fn`, `fn#`, and
`cede fn` values carry the same mode in their function type, so returned values
and interface imports do not silently fall back to shared invocation.
`F@Callable::Output` is derived from the return type of `call`; a callable
implementation never repeats that result metadata manually.

## Composition Closure

- iterator algorithms can invoke shared or exclusive callbacks under the same
  PAL access rules used by ordinary methods;
- `thread_spawn` accepts an exclusive `fn#` callback and invokes it through a
  writable binding, matching callbacks that mutate moved synchronization
  handles;
- detached execution retains the existing explicit-capture, dependency, and
  `@Send` boundary checks;
- generic callable bodies preserve `f#(...)` in emitted TKI, and `fn#` return
  signatures replay source-less.

## Evidence

- Positive execution: `tests/pass/g08_callable_protocol.tk` covers shared,
  exclusive, consuming, generic, user-defined, iterator, and exact-drop paths.
- Existing dynamic closure coverage now declares and calls mutable environments
  through `dyn fn#`.
- Negative diagnostics: `E04590` shared call of an exclusive callable,
  `E04591` missing consuming invocation, `E04592` immutable exclusive binding,
  and `E04593` missing protocol conformance.
- Same-version source/source-less replay:
  `tests/semantics/tki_replay/cases/callable_001_modes`.
- Thread callback execution: synchronization and MPSC tests use the exclusive
  callback contract.
- Complete local gates after this closure: 322/322 positive, 246/246 negative,
  1/1 warning, and 13/13 semantic replay cases.

## Deferred Boundary

The callable protocol is sufficient for eager generic algorithms and callbacks
used while traversing an existing collection. The post-1.0 owned slice adds
lazy `Map<I,F>` through the separate `@IntoIterable` protocol; it does not
change the frozen shared `@Iterable::iter(self)` or hide mutation behind shared
invocation. Borrowed/lending adapters, a filter family, consuming `for`, and
iterator-as-iterable behavior remain separately deferred.

This closure changes the frozen public surface after `v0.9.8-08-RC`. It does
not create a new RC or tag; FZ-5 remains `InProgress` until a later explicitly
authorized candidate is cut and the complete platform matrix is rerun.
