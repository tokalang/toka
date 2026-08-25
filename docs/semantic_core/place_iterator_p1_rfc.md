# Place Iterator P1 RFC

**Status:** P1 shared/read Array + Vec slice implemented; qualification
receipts are commit-bound.

## 1. Objective

P1 proves one fact:

> `for alias` transports the iterator's exact element place without creating
> a semantic `&Item` value.

The decisive qualification case is:

```toka
Vec<&T>
for alias &x in refs { ... }
```

The alias binding has morphology `&T`. P1 must not construct `&&T`,
`Option<&&T>`, an alias alloca, ownership, or a drop obligation on this path.

## 2. Compatibility Boundary

P1 uses migration mode A:

```text
for auto       keeps @Iterator / @BorrowIterator
for auto &&x   keeps the existing level-2 compatibility path
for alias      begins migration to @PlaceIterator
```

P1 does not prohibit first-class or structural managed level-2 types. The
`E0496` structural-storage proposal remains deferred until every public
`@BorrowIterator` carrier that can form `Option<&&T>` has been migrated or
removed.

## 3. Semantic Kind

`__PlaceOutcome<Item>` is a compiler semantic kind, not a normal source type:

```text
__PlaceOutcome<Item>
    Exhausted
    Hit(place<Item>)
```

It only transports an exact place. It does not grant or transform:

- H/P capability;
- shape permission inheritance;
- interior mutability;
- shared/exclusive pointee access;
- ownership;
- arbitrary provenance.

Those remain governed by `BindingPermission`, ordinary access-capability
resolution, and PAL.

### 3.1 Placement restrictions

`__PlaceOutcome<T>` is admitted only as the exact result kind of the
compiler-recognized `@PlaceIterator::next_place` lang-item method. It cannot:

- initialize a local value;
- appear as a field, array element, generic argument, alias target, ordinary
  function result, `Option`, or `Result` payload;
- be captured by a closure or async frame;
- be consumed by an ordinary source `CallExpr`;
- be named or constructed by ordinary application code.

Only compiler-generated `for alias` lowering may consume it in P1.

## 4. P1 Protocol

The logical core facet is:

```toka
trait @PlaceIterator {
    type Item
    pub fn next_place(self#) -> __PlaceOutcome<Item> <- self
}
```

The exact surface spelling of the builtin result kind is confined to the core
lang-item declaration and implementations qualified by the compiler. P1 adds
no ordinary application syntax or lexer token.

Two restricted compiler intrinsics construct the result:

```toka
__place_end<Item>()
__place_hit<Item>('(place_expr))
```

`'(place_expr)` selects the exact morphology. It does not prove addressability,
origin, dependency, stability, or permission. The compiler validates those
facts independently.

### 4.1 P1 constructor verifier

P1 accepts only compiler array paths and the standard-library Vec direct-place
implementation. A safe `__place_hit` operand must be:

- an addressable place;
- exact-morphology compatible with `Item`;
- rooted in the iterator's `self` storage or its declared source dependency;
- neither a local temporary nor a call result;
- stable under the fixed epoch contract below.

Third-party safe verification and unchecked constructors are not part of P1.

## 5. Fixed Loan Contract

Every implementation obeys the same two compiler-managed loans:

```text
SourceStabilityLoan
    cursor acquisition -> cursor drop

YieldedPlaceLoan
    Hit -> next advance or cursor drop
```

They are not implementation-configurable TKI fields.

Compiler-generated control flow is ordered as:

```text
acquire source loan
create cursor
loop:
    advance
    acquire yielded-place loan
    execute body
    release yielded-place loan
drop cursor
release source loan
```

`continue` releases the yielded-place loan before advancing. `break` and
`return` release it before cursor unwinding. The cursor may not advance while
its previous yielded-place loan is live.

## 6. Capability Boundary

The place carrier does not contain an H/P capability enum. Once the exact
element place is bound, existing Sema rules decide every operation.

The future mutable-slot selection formula is frozen for P2/P3 but not
implemented in P1:

```text
RequiresMutableSlot(pattern)
    = (root is Soul AND SoulWritable)
      OR root HandleIdentityRebindable
```

Consequently:

```text
alias x#       future mutable-slot lane
alias ^#x      future mutable-slot lane
alias &#x       future mutable-slot lane
alias ~#x      future mutable-slot lane

alias ^x#      shared slot carrier; ordinary pointee-P/PAL check
alias &x#      shared slot carrier; ordinary referent-P/PAL check
alias ~x#      shared slot carrier; remains fail-closed in the first
               qualified handle slice
```

P1 migrates shared/read alias only. Existing writable alias paths remain on
their compatibility carrier until P2/P3 and are not evidence for P1 purity.

## 7. Item Identity

`@PlaceIterator::Item` is the collection's exact element morphology. It never
adds a transport reference:

```text
Element T    -> Item T
Element &T   -> Item &T
Element ^T   -> Item ^T
Element ~T   -> Item ~T
```

Future shared and mutable cursor facets must expose the same canonical Item
identity even when the cursor types differ.

## 8. TKI Contract

TKI records only fixed protocol facts:

```text
facet identity: @PlaceIterator
canonical Item identity
source dependency
place-yield ABI schema version
```

The importer restores the global epoch law from the schema version. It does
not accept custom epoch, capability, or stability fields in P1.

The first place-yield ABI slice retains interface format v3 and compiler
interface provenance `0.9.9-14`, but requires independent
`place_yield_abi_schema: 1` metadata. Older `0.9.9-14` interfaces without this
schema fail closed without renaming unrelated toolchain nominal identities.

Source-backed and source-hidden compilation must agree on:

- `BindingMode::PlaceAlias`;
- exact element morphology;
- selected iterator facet and method;
- dependency root;
- absence of `ReferenceType<Item>` transport.

## 9. ABI Boundary

P1 freezes logical fields, not one target-independent byte layout:

```text
exhausted/hit state
place carrier address
schema identity
```

Target padding, metadata, and future provenance witnesses remain ABI-schema
concerns. CodeGen consumes the place address directly and registers the alias
symbol against it. It creates no user-visible reference value or independent
storage.

## 10. Vertical Slice

P1 qualifies:

- fixed arrays as a direct semantic oracle;
- Vec as the protocol, generic, TKI, and ABI proof.

HashMap, `EntryRef`, projection-place, arbitrary third-party containers,
mutable place protocols, unchecked constructors, local alias, alias return,
and managed level-2 removal are explicitly out of scope.

## 11. Acceptance Gates

P1 is complete only when all of the following hold:

1. `Vec<&T> + for alias &x` binds semantic `&T`.
2. The PlaceIterator path forms no semantic `&&T` or `Option<&&T>`.
3. Emitted TKI contains the place facet and exact `Item = &T`, not an
   `&Item` carrier.
4. CodeGen consumes the place address directly.
5. The alias has no alloca, ownership, retain/release, or drop obligation.
6. Source-backed and source-hidden replay agree.
7. `for auto &&x` remains green on the old BorrowIterator compatibility path.
8. Direct source calls cannot save `__PlaceOutcome`.
9. Existing writable alias behavior is unchanged by P1.
10. Handle Grammar admission/lowering gates remain zero-violation.

## 12. Deferred Sequence

After P1:

```text
P2  mutable source acquisition and root-Soul alias x#
P3  target-aware ^x# / ^#x / &x# / &#x migration; ~x# rejected
P4  third-party verifier, unsafe constructor, projection-place, HashMap
```

Only after every alias carrier has migrated may the project reconsider mode B
and structural managed level-2 rejection.

## 13. Implemented Qualification Slice

The P1 implementation binds shared/read Vec alias iteration to
`@PlaceIterator::next_place`. Its LLVM carrier is the logical pair
`{hit-tag, Addr}`; CodeGen converts the address directly into the alias symbol's
storage identity and does not construct `ReferenceType<Item>`.

The retained qualification anchors are:

- `g08_for_alias_place_iterator_vec_ref.tk` for `Vec<&i32>` runtime behavior;
- `place_outcome_direct_call_forbidden.tk` for the ordinary-call boundary;
- `iterator_003_alias_body` for generic `Vec<&T>`, TKI body round-trip,
  source-hidden replay, `next_place` selection, and direct place-address IR;
- the pre-existing writable Vec alias tests, which remain on the compatibility
  carrier until P2/P3;
- `g07_for_iterators.tk`, which keeps `for auto &&x` on `@BorrowIterator`.
