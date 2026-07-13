# Toka 1.0 Iterator, Callable, And Async Ergonomics Closure

Status: `Complete`

## Scope

ERG-5 validates composition of the already frozen eager iterator, callable,
ownership, async, and TKI rules. It adds no syntax, lazy adapter family,
consuming iterator, or async iterator.

The accepted 1.0 model is:

- `@Callable` generic algorithms accept shared, exclusive, and consuming
  receiver modes through ordinary Toka morphology;
- `[cede value]` controls capture ownership independently from whether a
  closure is invoked repeatedly or consumed once;
- owned iterator sources and owned closures cross `.start` only through the
  existing cede parameter and cede argument contract; and
- borrowed closure captures remain ordinary PAL dependencies, including when
  returned through a function signature and replayed from `.tki`.

## Implementation Closure

The composition tests exposed three implementation gaps:

- inferred generic arguments were checked once for type deduction and again
  for parameter matching, so `cede value` could report use-after-move at its
  own call site;
- local closure captures were stored as dependency metadata but omitted from
  the set committed as PAL borrows; and
- a function return dependency was applied at the call site but then cleared
  because the generic expression exit did not recognize dependency-carrying
  callable values.

Generic deduction now reuses its checked argument types, local closure
dependencies enter PAL, and `fn`/`dyn fn` values retain dependencies only when
the current expression actually carries them. Move and cede remain explicit
and are still rejected after the source has been borrowed.

## Evidence

- `tests/pass/g09_iterator_closure_async_composition.tk` executes shared,
  exclusive, and consuming generic callbacks. Its detached case transfers a
  `Vec` and mutable closure, suspends inside iteration, resumes, and completes
  with the expected state.
- `tests/fail/generic_cede_inference_use_after.tk` proves inferred generic cede
  consumes once and later use remains `E0438`.
- `tests/fail/closure_local_capture_move_source.tk` proves a local borrowed
  closure blocks moving its source with `E0440`.
- `tests/semantics/tki_replay/cases/ergonomics_002_closure_dependencies`
  proves a returned closure dependency is exported and produces identical
  source/source-less acceptance, rejection, and semantic evidence.
- Verification: 330/330 effective positive tests, 255/255 negative tests,
  1/1 warning test, and 16/16 semantic replay cases.

## Boundary And Handoff

Lazy map/filter adapters, consuming iteration, and async iteration remain
post-1.0. They need separate protocol design and must not be simulated by
weakening shared iteration or hiding transfer.

The audit also observed a local type name colliding with a private helper from
an imported module. That namespace/diagnostic issue is assigned to ERG-6 and
is not treated as an iterator or callable rule change.
