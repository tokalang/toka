# Toka 1.0 Error Propagation Closure

Status: Frozen

This closure keeps errors as ordinary typed return values. It adds no exception
control flow and no universal error object.

## Propagation Contract

Postfix `!` evaluates its operand once and consumes a whole `Result` or
`Option`. The success payload moves into the surrounding expression. A failure
constructs the enclosing function's failure return, unwinds all still-live
locals in reverse lexical order, and then returns. The consumed container and
the moved payload are not dropped again.

A named whole binding is marked moved. Partial-path propagation is a 1.0
conservative rejection because the current local move ledger does not represent
independent field liveness. Binding the field Result to a local makes the
transfer explicit and replayable.

## Error Conversion

`Result<T, E1>!` in a function returning `Result<U, E2>` has exactly two legal
paths:

1. `E1` and `E2` resolve to the same type, so the error moves directly.
2. `E1` implements `@ErrorInto<E2>`, whose `into_error(cede self) -> E2`
   method is called once on the failure path.

There is no conversion-chain search and no fallback to numeric, structural, or
layout compatibility. Sema stores the selected conversion function and both
error types on the propagation node. CodeGen consumes only that decision; it no
longer copies an unmatched union representation into the destination error.

`@ErrorInto` is an ordinary `core/traits` protocol, not a fifth implicit
prelude trait. Parameterized trait bounds are accepted in inline and `where:`
constraints, allowing generic propagation with `E1: @ErrorInto<E2>`.

## Context And Cleanup

`std/error::ErrorContext<E>` owns a message and the original typed source.
`with_context` transforms a Result without type erasure. Context destruction
therefore follows ordinary field cascade and preserves the source error until
the context itself is dropped.

Automatic RAII cleanup always runs on propagation. Explicit fallible cleanup
must be composed by the program before `!` or by matching both Results; 1.0
does not silently replace a primary error with a cleanup error.

## Async And Interfaces

After `.await`, propagation uses the same conversion and cleanup path. Runtime
evidence observes exact source-error and coroutine-frame local drops after a
suspension. Source-backed and source-less consumers select the same imported
conversion implementation and emit the same `ERROR-PROP-001` semantic record.

## Evidence

Complete local gates after this closure are 324/324 positive, 251/251
negative, 1/1 warning, 14/14 source/interface replay, and 12/12 semantic cache
invalidation.

- `tests/pass/g08_error_conversion_protocol.tk`: same-type, concrete, and
  generic one-step conversion; success paths do not call the converter.
- `tests/pass/g09_error_propagation_cleanup_async.tk`: typed context plus exact
  synchronous and post-suspension resource cleanup.
- `tests/fail/error_conversion_missing.tk` and
  `error_conversion_numeric_implicit.tk`: no missing or widening fallback.
- `tests/fail/error_conversion_non_consuming_impl.tk`: the protocol receiver
  cannot be weakened from `cede self`.
- `tests/fail/error_propagation_complex_path.tk`: explicit conservative path
  boundary.
- `tests/fail/main_result_return.tk`: frozen 1.0 entry contract.
- `tests/semantics/tki_replay/cases/error_001_conversion`: conversion and
  rejection replay through source and source-less interfaces.

## Deferred

`dyn error`, chained conversion, throw/catch, cleanup-error precedence, and a
`main -> Result` termination protocol remain post-1.0 design work. None is
required for typed application error enums, contextual errors, generic
propagation, or async code.
