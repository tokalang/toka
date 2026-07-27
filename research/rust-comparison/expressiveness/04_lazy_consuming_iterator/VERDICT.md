# Verdict: lazy consuming iterator composition

## Observed current boundary

The Rust program constructs an iterator that owns its input and mutable closure
state, then returns it for later consumption.  This is standard `Iterator`
composition; no external crate or explicit lifetime annotation appears at the
call site.

The Toka baseline is intentionally not a failed generic-callback example.  It
actually runs an eager generic `F: @Callable` algorithm with an exclusive,
stateful callback.  Toka also has borrowed iteration through `next_ref`.

What Toka 1.0 does not freeze is the *standard consuming/lazy protocol* needed
for the Rust shape: `@Iterable::iter(self)` is a shared entry point, and there
is no consuming iterator facet, iterator-as-iterable contract, or standard lazy
adapter family.  This is documented in
[`iterator_protocol_closure.md`](../../../../docs/semantic_core/iterator_protocol_closure.md)
and [`callable_protocol_closure.md`](../../../../docs/semantic_core/callable_protocol_closure.md).

## Classification

This is a **current 1.0 boundary**, not a proof that Toka cannot express lazy
stateful composition.  The particular Rust program is owned flow: it consumes
`I` and moves `count` into the adapter.  It therefore does not itself pose a
borrow-lifetime problem.

## Toka-style design candidate

A post-1.0 design can introduce a consuming source facet such as
`into_iter(cede self)`, a concrete `Map<I, F>` shape owning both iterator and
callable, and `next(self#)` on that shape.  A separate iterator-as-iterable or
opaque-adapter-return rule can make the result compose in `for` and further
adapters.  Borrowed/lending adapters should be a separate experiment: their
dependency propagation must be demonstrated rather than assumed.

The acceptance criterion is a Toka `counted` equivalent whose source and
stateful callback are each consumed exactly once, whose returned adapter can be
consumed later, and whose source/callback reuse is rejected.
