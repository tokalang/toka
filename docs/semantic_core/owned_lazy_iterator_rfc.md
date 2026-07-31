# RFC: Owned Lazy Iterator Adapters

**Status:** First owned slice implemented post-1.0. Borrowed/lending adapters,
consuming-loop syntax, async iteration, and opaque adapter returns remain
deferred.

**Depends on:** frozen `@Callable`, `cede`, `@Encap`, and synchronous iterator
protocols.

## 1. Problem and boundary

Toka 1.0 supports eager generic algorithms over `F: @Callable`, including
exclusive/stateful callbacks. It also supports shared-entry `@Iterable` and
borrowed `next_ref` iteration.

It does not freeze a standard protocol for a lazy adapter that owns both a
consumed source iterator and mutable callback state, then returns an adapter
for later consumption. This RFC addresses only that **owned** case.

The motivating shape is equivalent to a stateful `map`: source `I` is consumed,
callback `F` is moved into the adapter, and later `next` calls mutate adapter
state. It is not a borrowed/lending iterator proposal.

## 2. Non-goals

- No reinterpretation of `@Iterable::iter(self) -> Iter <- self`.
- No new implicit copy, no hidden `cede`, and no relaxation of callable
  receiver permissions.
- No borrowed lazy adapter, `next_ref` adapter, async iterator, or generator.
- No opaque `impl Iterator` spelling in the first slice.

## 3. Minimal owned protocol

The first slice introduces a separate consuming source facet. Its semantic
shape is:

```text
IntoIterable::into_iter(cede self) -> IntoIter
Map<I, F> owns I and F
Map::next(self#) -> Option<F@Callable::Output>
```

`Map<I, F>` is an ordinary concrete `@Encap` shape. It owns the source and
callable; its exclusive `next(self#)` advances `I` and invokes `F` with the
receiver mode declared by `F`. Its item is the derived
`F@Callable::Output`: every `@Callable` implementation (including a compiler
generated closure) derives that result from the return type of `call`, rather
than duplicating it in source. The first public construction function returns
the concrete `Map<I,F>` type, not an opaque iterator trait object.

```text
source --cede--> Map.source
callback --cede--> Map.mapper
Map::next(self#) mutates cursor/callback state
Map drop releases any unconsumed source/callback state exactly once
```

The entire first model is owned flow. It carries no `<-` dependency and makes
no claim about lending references through a later adapter.

## 4. Authority and lifecycle rules

1. Constructing an adapter requires explicit transfer of the source and every
   non-copy callback capture.
2. After construction, source and callback aliases follow ordinary `cede`
   invalidation rules; they cannot be reused.
3. Calling `next` requires exclusive access to the adapter because its cursor
   and callback may mutate.
4. The callable invocation must preserve the existing `fn` / `fn#` / `cede fn`
   receiver contract. A consuming callable makes the adapter's first use
   consuming or is rejected from the simple repeatable `Map` form.
5. Adapter destruction drops live source/callback fields exactly once, using
   the ordinary `@Encap` cleanup path.

## 5. Interaction with existing `for`

The first slice does not change `for`. It proves the owned adapter with explicit
`next` calls and a concrete return type. A later, separate decision may
introduce either:

- an iterator-as-iterable rule for owned adapters; or
- a distinct consuming-loop form.

That separation avoids silently changing the meaning of the frozen shared
`@Iterable::iter(self)` receiver.

## 6. Implementation slices

1. **Implemented trait/library slice:** `@IntoIterable`, `VecIntoIterator<T>`,
   and concrete `Map<I,F>` over `@Iterator` sources. Generic deduction
   preserves direct ceded source and callable types; `F@Callable::Output` is
   derived from `call` rather than repeated in each implementation.
2. **Implemented ownership evidence:** source/callback invalidation, mutable
   callback state, early adapter drop of a captured resource, nested `Map`,
   and source-less replay.
3. **Deferred composition:** add a second owned adapter only after `Map` can
   be nested without erasing callable receiver mode or cleanup state.
4. **Deferred surface integration:** separately decide consuming `for` or
   iterator-as-iterable behavior.

## 7. Acceptance evidence

The first executable suite must demonstrate:

- a stateful callback whose output changes across successive `next` calls;
- source and callback reuse rejected after construction;
- early adapter drop releases a live callback capture exactly once;
- nested owned `Map` composition;
- compile-fail coverage for shared invocation of a mutable adapter and for a
  consuming callback in the repeatable adapter form;
- source-less replay of the consuming source and callable receiver contracts.

`Vec<T>` now separates its move-only core from clone-dependent operations:
`push`, `pop`, `remove`, growth, and final drop move resource elements without
instantiating their deleted clone method. Copying APIs remain available only
when the element is cloneable. Exact-drop evidence for resource-bearing
sources is covered by the Vec move-only lifecycle conformance fixture.

## 8. Borrowed/lending adapters remain open

A later adapter that yields references or captures borrow-like state must state
how `<-` dependencies flow through adapter construction, `next_ref`, returned
items, nesting, and destruction. It may require stronger dependency notation
or an additional protocol. This RFC neither assumes that need nor treats it
as evidence for user-visible lifetime annotations.
