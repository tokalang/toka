# Verdict: static async trait protocol

## Observed result

The Rust program uses a native `async fn` trait method through a static generic
bound.  Its small executor is implemented with `std` only.  It deliberately
does not test `dyn Fetcher`: Rust native async trait methods have their own
object-safety boundary, so a `dyn` comparison would not establish an advantage.

Toka accepts and runs a concrete trait method when it is spelled in Toka's
existing return-async form, `fn fetch(self) -> async i32`.  That is important:
there is no honest claim that Toka merely lacks the parser surface.

The corresponding static generic caller with a borrowed `self`/`worker` path
is rejected with `E04583` when it awaits the trait method.  Toka treats the
async call as an execution boundary and forbids the borrowed receiver from
crossing it.  An owned/`cede` receiver protocol might offer a later safe
subset, but is not assumed by this case.

## Classification

This is a **current async-interface protocol boundary**, with an implemented
but unfrozen surface subset.  `docs/1_0_scope.md` currently lists async traits
as post-1.0, while concrete return-async trait methods compile.  The generic
borrowed-receiver form that Rust accepts is rejected.  Therefore the evidence
does not support either extreme claim: neither "Toka has no async traits" nor
"the async-trait contract is already closed."

## Boundary still open

This evidence does not settle owned/`cede` receiver design, dyn dispatch,
associated types, object ABI, cancellation, source-less `.tki` replay, public
cross-module visibility, or receiver-borrow suspension.  Those questions need
an async-interface RFC before any accepted subset can be advertised or frozen.
