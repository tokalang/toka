# For Alias Binding Contract

**Status:** Core read/write semantics frozen. The shared/read implementation is
qualified for stable BorrowIterator element places; exclusive H/P paths remain
an implementation qualification task. General local alias bindings remain
deferred.

`for auto` remains source-compatible and always creates a first-class Item
value. That value may itself have handle morphology such as `&T` or `&&T`.
No existing `for auto` spelling is reinterpreted or removed.

`for alias` introduces a lexical second name for the iterator's stable element
place. It creates neither an Item value nor an additional user-visible
reference value:

```toka
for auto &x in refs   // first-class &T value
for auto &&x in refs  // first-class &&T value
for alias &x in refs  // place alias of the stored &T element
```

The pattern after `alias` must exactly equal the element morphology. It may
neither add nor remove a layer:

```toka
Vec<T>   -> for alias x
Vec<&T>  -> for alias &x
Vec<^T>  -> for alias ^x
Vec<~T>  -> for alias ~x
```

An alias has no independent storage, ownership root, or drop obligation. It
cannot escape, be stored, or be ceded as an independent value. Its PAL loan is
anchored to the source collection/element place and blocks invalidating
mutation for the loop lifetime. The initial slice uses `@BorrowIterator` as the
internal stable-address carrier; iterators without such a carrier reject
`for alias`.

Permission intent is written only on the alias pattern; the source expression
does not repeat a `#` suffix:

```toka
for alias x# in values       // request payload write
for alias ^x# in owners      // request pointee-payload write
for alias ^#x in owners      // request element handle-slot rebind
for alias ^#x# in owners     // request both H and P
```

The compiler admits the alias exactly when the requested H/P capability is a
subset of the original element place's effective capability. Container
writability may authorize an ordinary element-slot write or handle-slot
rebind, but never crosses a `^`, `~`, `&`, or `*` boundary to manufacture
pointee-payload authority. Interior mutability and shape inheritance therefore
apply exactly as they do through the original place. Shared aliases establish
shared PAL loans; any requested write/rebind establishes the corresponding
exclusive loan for the loop lifetime. General local `alias view = place` and
consuming `for auto ^x` remain separate work.
