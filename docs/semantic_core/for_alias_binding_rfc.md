# For Alias Binding Contract

**Status:** Core read/write semantics frozen. Shared/read aliases are qualified
for stable `@BorrowIterator` element places. Writable Soul and managed handle
aliases are qualified for fixed arrays; Vec mutable carriers are qualified for
Soul, Unique, and Reference elements. Mutable Vec shared-envelope aliases and
general local alias bindings remain deferred.

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
cannot escape, be stored as an owner, or be consumed, including through a
projection rooted at the alias. Explicit `cede` and intrinsic unique value
moves (`auto ^y = ^x`, `return ^x`, handle assignment, and owning-field
initialization) all fail with E04646. Its PAL loan is anchored to the source
collection/element place and blocks invalidating mutation for the loop
lifetime. The initial slice uses `@BorrowIterator` as the internal
stable-address carrier; iterators without such a carrier reject `for alias`.

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
shared PAL loans; any requested write/rebind establishes an exclusive loan for
the loop lifetime. `alias x#` is qualified over writable fixed-array element
places and `Vec<T>` elements through the separate `iter_mut` /
`@MutableBorrowIterator::next_mut` stable-place carrier. For fixed-array handle
elements, `alias ^x#` requires P on the element's pointee view, while
`alias ^#x` requires H on the containing element slot; neither request creates
the capability it asks for. Vec Unique/Reference elements use the same split:
the carrier returns `&'T`, preserving exact element morphology without adding
`#`; P comes only from the element's existing pointee type, while H comes only
from a writable Vec element slot. Mutable Vec shared-envelope aliases remain
E04645 fail-closed until `Vec<~T>` has a qualified generic envelope-transfer
ABI. Read-only sources, unqualified H/P requests, and containers without a
matching stable-place carrier also fail closed with E04645. General local
`alias view = place` and consuming `for auto ^x` remain separate work.

The parser preserves handle-layer permission requests separately from payload
`#`, so `alias ^x#` and `alias ^#x` cannot collapse into one case. Fixed arrays
and Vec qualify these distinct P/H requests independently. Vec's generic
carrier uses morphic identity (`&'(...)`) to return the address of the exact
element slot; it never infers handle identity from LLVM pointer shape.
