# Verdict: lazy consuming iterator composition

## Observed 1.0 boundary and post-1.0 first slice

The Rust program constructs an iterator that owns its input and mutable closure
state, then returns it for later consumption.  This is standard `Iterator`
composition; no external crate or explicit lifetime annotation appears at the
call site.

The Toka baseline is intentionally not a failed generic-callback example.  It
actually runs an eager generic `F: @Callable` algorithm with an exclusive,
stateful callback.  Toka also has borrowed iteration through `next_ref`.

Toka 1.0 did not freeze the *standard consuming/lazy protocol* needed for the
Rust shape: `@Iterable::iter(self)` is a shared entry point, with no consuming
iterator facet, iterator-as-iterable contract, or standard lazy adapter family.
The post-1.0 first slice now provides a separate `@IntoIterable` facet,
`VecIntoIterator<T>`, and concrete `Map<I,F>`. `toka_owned_lazy_map.tk` is the
Toka counterpart: it moves a source and stateful callback into `Map`, then
later obtains `2, 4, 6` through `next(self#)`.

The frozen shared protocol and explicit non-goals are documented in
[`iterator_protocol_closure.md`](../../../../docs/semantic_core/iterator_protocol_closure.md)
and [`callable_protocol_closure.md`](../../../../docs/semantic_core/callable_protocol_closure.md).

## Classification

The former limitation was a **current 1.0 boundary**, not a proof that Toka
could not express lazy stateful composition. The first owned slice reaches
observed parity for this program's owned-flow shape. It does not add borrowed
or lending adapters, consuming `for`, opaque adapter returns, or a general
combinator family; those remain post-1.0 design work.

## Toka-style scope boundary

The implementation deliberately stops at a concrete `Map<I,F>` and explicit
`next` calls. A separate iterator-as-iterable or opaque-adapter-return rule may
later make adapters compose in `for`. Borrowed/lending adapters remain a
separate experiment: their dependency propagation must be demonstrated rather
than assumed. The existing acceptance suite proves source/callback invalidation
and a stateful Toka `counted` equivalent; resource-bearing Vec source elements
remain outside this slice's evidence because of a pre-existing Vec cloneability
limitation.
