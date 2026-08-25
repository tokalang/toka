# Toka 1.0 Iterator Protocol Closure

Status: `Compatibility baseline`; shared/read `for alias` is superseded by
[`place_iterator_p1_rfc.md`](place_iterator_p1_rfc.md).

## Scope

This late 1.0 closure replaces the previous split between a public
`@Iterator` declaration and compiler-recognized `.iter()`/`.next()` method
names. It adds no source syntax. Arrays keep their built-in fixed-layout path;
all other `for` expressions use ordinary imported traits.

The frozen facets are:

- `@Iterable` with associated `Iter` and `iter(self) -> Iter <- self`;
- `@Iterator` with associated `Item` and `next(self#) -> Option<Item>`;
- `@BorrowIterator` with associated `BorrowedItem` and
  `next_ref(self#) -> Option<BorrowedItem> <- self`;
- `@MutableBorrowIterator` with associated `BorrowedItem` and
  `next_mut(self#) -> Option<BorrowedItem> <- self`, selected only by a
  qualified writable `for alias` compatibility path;
- canonical `@PlaceIterator` with exact `Item` and compiler-only
  `next_place(self#) -> __PlaceOutcome<Item> <- self`, selected by qualified
  shared/read `for alias` on Array and Vec.

Only `@Encap`, `@Send`, `@Sync`, and the later `@Callable` protocol are implicit
prelude traits. Iterator facets use normal lexical imports when named in
declarations.

## Safety Closure

- Sema resolves the facets and records the selected `iter` and `next` methods
  on `ForExpr`; CodeGen consumes those resolved declarations instead of
  reconstructing method names.
- Place aliases pass a stable source identity to `iter`/`iter_mut`; value-like
  collections are never copied into a staging slot before the alias carrier
  is created.
- Shared/read alias transport no longer manufactures `ReferenceType<Item>`;
  `@PlaceIterator` transports the exact Item place. Mutable compatibility
  carriers preserve exact element morphology rather than manufacturing
  permission. Vec's `&'T` return uses explicit morphic identity to yield the
  element slot; Sema separately proves pointee P and slot H.
- PAL holds a shared source borrow for the implicit loop cursor. Borrowed loop
  variables inherit the source dependency, and the compiler releases only the
  borrow introduced for that loop.
- Explicit cursor variables now commit dependencies carried by shapes with
  borrowed members. Direct method-call returns participate in the ordinary
  escaping-dependency collector, closing local cursor escape.
- Hidden cursors are registered as scoped values and run their destructor on
  exhaustion, `break`, and function return.
- Vec, HashMap, and HashSet iterators carry explicit borrow-like storage rather
  than relying on untracked raw-pointer lifetime.

## Evidence

- Focused pass: value/reference morphology, post-loop mutation, and observable
  hidden-cursor drop.
- Focused fail: structural protocol rejection, missing `<- self`, loop/source
  mutation, saved-cursor mutation, and local-source escape.
- Same-version source/source-less replay:
  `iterator_001_protocol` and `iterator_002_mutable_alias`; the latter binds
  the mutable associated type, `<- self` dependency, original-place identity,
  and runtime write-through after hiding the provider source.
- Existing Vec, HashMap, HashSet, associated-type, build-system, PAL, and
  control-flow tests remain part of the current commit-bound gates; historical
  test counts are not a current qualification claim.
- Because this closure was explicitly authorized after `v0.9.8-08-RC`, its
  local evidence reopens FZ-5; a fresh four-target RC matrix is required before
  final freeze.

## Deferred Boundary

Toka 1.0 did not add a consuming-loop syntax, consuming iterator facet,
async-iterator facet, or language-provided combinator family. The post-1.0
owned slice adds separate `@IntoIterable` and concrete `Map<I,F>` without
changing shared `@Iterable::iter(self)` or `for`. Consuming-loop syntax,
async/lending iteration, and further combinator families remain separate
design work; they must extend, rather than reinterpret, this synchronous
protocol.
