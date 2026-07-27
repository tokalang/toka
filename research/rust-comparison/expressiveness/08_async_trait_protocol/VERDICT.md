# Verdict: static async trait protocol

## Observed result

The Rust program uses a native `async fn` trait method through a static generic
bound.  Its small executor is implemented with `std` only.  It deliberately
does not test `dyn Fetcher`: Rust native async trait methods have their own
object-safety boundary, so a `dyn` comparison would not establish an advantage.

Before the 1.0 closure, the compiler accepted a concrete trait method spelled
`fn fetch(self) -> async i32`, but it later rejected a started generic
borrowed-receiver call with `E04583`.  That half-supported surface was not an
async-trait contract.

Toka now rejects the trait declaration itself with `E0618`, before it can enter
trait dispatch metadata or a `.tki`.  The adjacent ordinary `fn -> async T`
baseline still compiles and runs, demonstrating that the closure is narrow: it
does not weaken the frozen async-function surface.

## Classification

This is a **current async-interface protocol boundary**.  Rust accepts the
static generic `async fn` trait method in the comparison.  Toka 1.0 now
rejects all async trait declarations consistently with its published scope,
while retaining ordinary async functions.  This is an explicit 1.0 exclusion,
not evidence of a PAL or lifetime-model limitation.

## Boundary still open

An eventual extension must settle owned/`cede` receiver design, borrowed
receiver suspension, dyn dispatch, associated types, object ABI, cancellation,
source-less `.tki` replay, and public cross-module visibility.  Those questions
need an async-interface RFC before any async trait subset can be admitted.
