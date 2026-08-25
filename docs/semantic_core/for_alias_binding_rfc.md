# For Alias Binding Contract

**Status:** Implemented initial 1.0 slice for stable BorrowIterator element
places; general local alias bindings remain deferred.

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
`for alias`. General local `alias view = place`, consuming `for auto ^x`, and
mutable/exclusive alias bindings require separate qualification.
