# Toka Syntax Guide

This guide describes the public surface syntax of the current Toka implementation. It is intentionally conservative: examples here are limited to forms used by the compiler, standard library, or test suite. Short fragments may omit surrounding declarations, but the syntax shown is meant to match current Toka.

For a short project overview, see the repository [README](../README.md). For historical notes and internal pitfalls, see [syntax_notes_zh.md](syntax_notes_zh.md).

## 1. Core Model

Toka separates two layers that many systems languages collapse into one notation:

| Layer | Meaning | Examples |
| :--- | :--- | :--- |
| Payload / Soul | The object content being read, written, passed, or matched | `x`, `x.field`, `x = value` |
| Handle / Representation | The way an object is reached, owned, borrowed, shared, or rebound | `&x`, `*x`, `^x`, `~x`, `*x = *y` |

Plain names operate on payload. Hats operate on handle identity. This is the key rule behind Toka's pointer and resource syntax.

```toka
auto ^p = new i32(100)
auto value = 10
auto &r = &value
```

In the example above, `p` is the payload view of a unique-owned object, `^p` names the unique owning handle, and `&value` creates a borrow handle.

## 2. Files, Imports, And Entry Point

Toka source files use `.tk`.

Module-location paths follow filesystem-oriented spelling. Path segments may
use kebab-case when they refer to directories or `.tk` file names:

```toka
import std/io::println
import core/types::{usize, Addr}
import ./third-party/http-client as http_client

fn main() -> i32 {
    println("hello")
    return 0
}
```

Hyphens are path-only. Any name newly created inside `.tk` source and entering
Toka's semantic namespace must be a normal identifier: variables, functions,
types, fields, import aliases, import item aliases, and selectable namespaces do
not use kebab-case. Therefore `as http-client`, `http-client::send()`, and
`(package-name = "...")` are invalid. In expression syntax, binary `-` is an
operator and must be surrounded by spaces, as in `a - b`.

The entry point is `main`. Toka 1.0 accepts `i32` or `void` as its return type,
including for async `main`. A `Result`-returning helper must be handled at this
boundary; `main -> Result<...>` is reserved for a future termination protocol.

Comments:

```toka
// line comment
/* block comment */
```

## 3. Bindings, Mutability, And Nullability

Local variables are declared with `auto`.

```toka
auto x = 1
auto y: i64 = 10
auto z = 10:i64
```

An integer literal adopts the unique integer type supplied by its context,
including a function or method parameter, assignment target, return type, or
the typed operand of a comparison. Write an explicit type only when no unique
context exists or when the width is itself part of the intended boundary. A
literal that does not fit the selected type is rejected with `E04598`; Toka
does not silently truncate contextual literals.

`#` on a binding grants mutation authority for that binding.

```toka
auto count# = 0
count = count + 1
```

`#` appears in declarations and explicit mutable method calls. Ordinary reads and assignments use the bare name.

```toka
counter#.inc()
counter = 3
```

### Absence domains

Toka 1.0 intentionally distinguishes three meanings of absence. They may have
isomorphic physical representations, but they are not aliases in the type
system:

| Domain | Surface form | Empty state | Meaning |
| :--- | :--- | :--- | :--- |
| Nullable handle | `nul *p: T`, `nul ^p: T`, `nul ~p: T` | `null` | A handle is present as storage but does not designate an object |
| Nullable payload | `value: T?` | `none` | A payload slot is present but currently contains no payload value |
| Optional result | `result: Option<T>` | `Option<T>::None` | An operation produced no result |

For example:

```toka
auto nul *ptr: i32 = null
auto maybe: i32? = none
auto result: Option<i32> = Option<i32>::None
```

`T?` is not syntax sugar for `Option<T>`, `none` is not
`Option<T>::None`, and a nullable handle is not an `Option` handle. Toka does
not implicitly convert or flatten these domains. Borrow handles (`&`) are not
nullable; use `nul` only with raw, unique, or shared handle forms.

The distinction is observable when an operation can successfully produce a
nullable payload:

```toka
fn lookup(key: str) -> Option<string?>
```

That return type has three semantically distinct states:

| State | Meaning |
| :--- | :--- |
| `Option<string?>::None` | The key was not found |
| `Option<string?>::Some(none)` | The key was found and its stored payload is empty |
| `Option<string?>::Some(value)` | The key was found with a payload value |

Move invalidation is not another `Option` result. A moved-from binding is
tracked by PAL and cannot be observed by matching a public `Moved` variant;
the standard `Option<T>` result domain consists of `Some(T)` and `None`.

Control flow preserves the distinction between storage absence and result
absence. A `??` assertion discharges only the nullable layer selected at that
source position. A hatted guard such as `guard ^node` checks the selected
handle layer, while a bare payload guard such as `guard node` is a path-usability
test: it may prove every nullable handle and payload layer that must be present
to reach that payload safely. Its failure branch deliberately coalesces those
storage-absence causes into “the payload path is unavailable.” Code that needs
to distinguish which layer was empty must guard the handle and payload layers
separately.

Neither guard form crosses a nominal `Option` or `Result` boundary. Postfix `!`
applies only to `Result` and `Option`, as specified in the result-propagation
section, and does not unwrap nullable handles or nullable payloads. A deep
payload guard is therefore a control-flow convenience, not an implicit type
conversion or an `Option` flattening operation.

An implementation may reuse a niche or another compact layout for more than
one absence domain. Layout equality does not create type identity, an implicit
conversion, or a stable cross-language ABI guarantee.

## 4. Hats And Handles

Toka uses hats to expose handle identity:

| Hat | Role |
| :--- | :--- |
| `&` | Borrow / reference handle |
| `*` | Raw pointer handle |
| `^` | Unique owning handle |
| `~` | Shared owning handle |

Examples:

```toka
auto value = 10
auto &r = &value
auto ^owned = new i32(5)
```

Payload assignment and handle rebinding are different operations:

```toka
p = value      // write payload
*p = *q        // rebind a raw pointer handle
```

For a handle binding that itself may be rebound, place `#` after the hat:

```toka
shape Node(
    val: i32,
    nul ^next: Node
)

auto ^#head = new Node(val = 0, ^next = null)
```

The position of `#` is semantic. `^#p`, `*#p`, `~#p`, and `&#p` mark the handle identity as rebindable. `^p#`, `*p#`, `~p#`, and `&p#` keep `#` on the binding name / payload side; they do not grant handle rebinding authority. When both permissions are needed, write both positions, such as `^#p#`.

`$` is the explicit read-only / blocked counterpart. Because ordinary payloads
are read-only by default, `$` is usually omitted and is rejected on ordinary
locals and parameters as redundant. Its purpose is to block inheritance in
places where an outer writable path would otherwise grant permission:
`field$` remains read-only even through `obj#`, and hatted forms such as
`^$p` or `*$p` keep the handle identity non-rebindable even through a writable
parent.

Permission inheritance is layer-local. Writable access to an object payload
can flow into ordinary fields, but a handle field forms an inheritance boundary:
the parent may authorize rebinding the handle identity, while the pointee
payload remains read-only unless the field or binding explicitly carries a
payload-side `#`, such as `^p#` or `*p#`.

## 5. Functions, Parameters, And `cede`

Function parameters are explicitly typed.

```toka
fn add(a: i32, b: i32) -> i32 {
    return a + b
}
```

For ordinary object parameters, Toka uses logical in-place capture. If the function wants the payload view, use forms such as `x: T` or `x#: T`.

This is a source-level semantic rule, not a promise about physical argument
layout. A target ABI may pass scalar values in registers and lower aggregates,
handles, closures, or return storage through different representations. Those
choices do not create an implicit source copy or change PAL's call-borrow
rules. Generated layout and calling convention remain compiler-, target-, and
version-bound rather than a stable Toka 1.x binary ABI.

Use a hat on a parameter only when the function needs the handle itself, for example `*p: T` for a raw handle parameter or `*#p: T` when the callee must be able to rebind that handle.

At a call site, passing that handle itself also uses the hatted view, such as `take(*p)`. A naked `p` remains the payload view.

For PAL, a function call is checked as a simultaneous group of temporary
borrows. Passing `x` to a payload parameter is still a borrow event even though
the source does not spell `&x`: `x: T` creates a temporary shared payload
borrow, while `x#: T` creates a temporary exclusive payload borrow. The whole
argument list is checked together, so `read_twice(x, x)` is valid for read-only
payload parameters, but `mutate_twice(x, x)` and `mutate_and_read(x, x)` are
rejected when either parameter requires exclusive payload access. A `cede`
argument is an invalidating transfer rather than a borrow and conflicts with
any other overlapping argument in the same call.

`cede` is an explicit resource-transfer contract.

```toka
shape Resource(val: i32)

fn keep(cede r: Resource) -> Resource {
    return cede r
}
```

If a parameter is declared `cede`, the caller must pass it with `cede`, and the function body must explicitly complete that transfer by consuming, forwarding, storing, returning, or otherwise ending the resource path. Merely reading the payload does not satisfy the contract. A parameter that was not declared `cede` cannot be ceded inside the function body.

Default arguments are supported. A call that chooses defaults must include `..`.

```toka
fn test_val(x: i32 = 42) -> i32 { return x }

auto a = test_val(..)
```

Borrow-like values that escape a function boundary must declare their dependency
path in the signature. For Toka 1.0 this is required for private and public
functions alike: callers consume the signature, and the callee body must prove
that any escaped borrow comes only from the declared dependency sources. This
keeps `.tki` interfaces and source builds aligned.

Reference returns can use effect routing:

```toka
fn choose(a: i32, b: i32) -> &res: i32
effects:
    &res <- a | b
{
    if a > b {
        return &a
    }
    return &b
}
```

The same rule applies to other borrowed views that cross the boundary, including
`str`, `bytes`, records or shapes containing `&` fields, and closures or async
values that capture borrowed state. The compiler may use local control-flow
analysis inside a function, but a call site never depends on inspecting the
callee body. Ownership and sharing handles such as `^T` and `~T` are not
borrow-like dependencies by themselves; any dependency comes from borrowed state
inside the returned value.

Shape-level header dependencies are not part of the 1.0 syntax. Write the
borrow-like field itself:

```toka
shape RefInt(
    &val: i32
)
```

Do not write `shape RefInt <- val`. The field morphology and the initializer
carry the dependency fact; returned-member dependencies are expressed with
function `effects:` routing.

Toka 1.0 also does not support shape-internal member dependency declarations
such as `&view: i32 <- owner`; the parser rejects them as unsupported. That
relation would make a shape internally self-referential and needs a stable
placement model before it can be safe. Return borrowed views from functions and
declare their dependencies there instead. Shape-level `effects:` blocks are not
part of the shape grammar.

Execution boundaries are stricter than ordinary local calls. For Toka 1.0,
thread/task handoff must not carry hidden borrowed state: a closure passed to
`thread_spawn` cannot implicitly capture outer variables, and `.start` follows
the same rule. A started task may receive non-borrowing scalar arguments by
value. Any shape or resource crossing `.start` must be transferred through a
`cede` parameter and an explicit `cede` call argument; copyable shapes are not
copied implicitly because ordinary object parameters are logical in-place
captures. References, `str`, `bytes`, raw pointers, and task values carrying PAL
dependencies cannot cross `.start`. Async return dependencies such as
`fn f(x: str) -> async str <- x` remain ordinary signature dependencies; they
do not authorize detached tasks to keep an undeclared borrow.

Inside a function declared `-> async T`, `.await` is the source-level
suspending consumer. Using `.await` in a function that is not declared async is
rejected; using the blocking `.wait` consumer inside an async function is also
rejected. Suspension does not end the current scope or reset semantic state.
Locals needed after `.await` remain coroutine-frame state, and init, move, and
PAL borrow facts continue through the suspension point and the surrounding
`if`, `match`, `loop`, `break`, and `continue` merges. A dependency obtained
from an awaited async result remains active after resume, so replacing, moving,
or ceding its source is checked exactly as it is in synchronous code. These
guarantees do not extend PAL to raw pointers, which remain inside the explicit
unsafe/FFI boundary.

### PAL (Path-Anchored Ledger) Static Safety Boundary

For Toka 1.0, PAL is frozen as **Path-Anchored Ledger**: a local, path-based safety checker for the safe language subset. It records borrow, ownership-transfer, and invalidation facts against source-level storage paths, tracking borrowed paths, payload mutation, handle rebinding, resource moves, unset state, and the analysis state produced by `if`, `guard`, `match`, `loop`, `for`, `break`, and `continue`.

The stable contract is governed by four core rules:
1. **Unique ownership is exclusive:** A `^` resource is owned by one valid handle at any time.
2. **Transfer is explicit:** Ownership handoff must be syntactically visible. Direct hatted unique-handle moves are visible transfer syntax; `cede` is required for declared cede contracts and explicit cede handoff paths, and any transfer obligation must be fulfilled.
3. **Borrow validity is protected:** Operations that can invalidate an active borrow (such as moves, `cede`, drops, handle rebinding, or reallocations of the underlying storage) are rejected.
4. **Exclusive mutation requires exclusive permission:** Exclusive/mutable borrows conflict with other overlapping active borrows. A standard immutable borrow is a read-only capability of that borrow view, not a global freeze promise for all storage reachable from the original path. Ordinary payload writes, exclusive mutations, and invalidating operations are classified separately.

Under these rules:
- A function call declares all argument borrows at once; call-site payload
  passing is not invisible to PAL.
- Implicit dereference or borrow-based argument passing may not raise write
  permission: a read-only `&T` view cannot satisfy a `T#` payload parameter.
- A shared borrow blocks invalidating or exclusive mutation of the same path or an overlapping parent / child path.
- Ordinary payload writes do not by themselves count as invalidation, but writing a parent path that would replace storage containing an active borrow remains invalidating and is rejected.
- A mutable borrow blocks both reads and writes through overlapping paths unless the access is proven disjoint.
- Terminal member borrows such as `obj.&field` and `obj.&#field` are recorded
  against the selected member path, just like `&(obj.field)`.
- Moving or `cede`-ing a borrowed resource path is rejected.
- Moving a resource path defined outside a loop from a loop backedge is rejected; this includes paths that reach the backedge through `continue`.
- Interior-mutable fields marked with `#` may be updated through the explicit field rule, but ordinary fields remain protected by the active borrow.

PAL does not infer hidden lifetime relationships across a function boundary.
Escaping borrowed views must be declared in the signature, so source builds and
`.tki` interface builds agree. Raw pointers and `unsafe` code are outside this
safe-borrow guarantee unless wrapped by a safe API whose signature exposes the
required dependencies.

## 6. Shapes, Enums, And Initialization

`shape` defines aggregate data and tagged enum-like variants.

```toka
shape Point(x: i32, y: i32)

shape State(
    On |
    Off |
    ErrCode(i32)
)
```

Shape fields are named. Shape construction uses named arguments.

```toka
auto p = Point(x = 10, y = 20)
```

Shape definitions remain compiler-visible type contracts even when some fields
are private through `@encap`. Interface files must preserve the structural facts
needed for semantic checking, including field morphology, mutability,
nullability, layout-relevant attributes, and borrow-like member types. Visibility
controls user access; it does not erase compiler knowledge.

Fields may have defaults. Use `..` to accept remaining defaults.

```toka
shape Config(host: i32, port: i32 = 80, debug: i32 = 0)

auto cfg = Config(host = 127, ..)
```

Position-based struct initialization is not the public style and is rejected by current diagnostics for shapes that require named fields.

Aliases and new nominal types:

```toka
alias ID = i32
type UserID = i32
```

## 7. Methods, Traits, And Encapsulation

Methods are defined in `impl` blocks.

```toka
shape Rect(w: i32, h: i32)

impl Rect {
    pub fn area(self) -> i32 {
        return self.w * self.h
    }
}
```

Trait implementations use `impl Type@Trait`.

```toka
trait @Shape {
    pub fn area(self) -> i32
}

impl Rect@Shape {
    pub fn area(self) -> i32 {
        return self.w * self.h
    }
}
```

Traits may declare associated types. A plain `type` associated type is stable for the whole trait family on a shape: once `Data@Mapper<i32>::Output` is bound, another `Data@Mapper<bool>` implementation must use the same `Output`. A `per type` associated type is bound per trait instance, so different trait arguments may choose different output types.

```toka
trait @Readable {
    type Item
    pub fn read(self) -> Item
}

shape IntBox(value: i32)

impl IntBox@Readable {
    type Item = i32
    pub fn read(self) -> Item {
        return self.value
    }
}

trait @Slot<K> {
    per type Value
    pub fn get(self) -> Value
}

shape IntSlot(value: i32)

impl IntSlot@Slot<i32> {
    per type Value = i32
    pub fn get(self) -> Value {
        return self.value
    }
}
```

Inside the defining trait or impl block, the associated type name may be used directly in method signatures and local type annotations. Outside the block, use projection syntax: `IntBox@Readable::Item` or `IntSlot@Slot<i32>::Value`.

Dynamic trait objects use `dyn @Trait` in type positions. A concrete value whose type implements the trait may be passed to a parameter expecting `dyn @Trait`; no `&` is needed for ordinary parameter passing because Toka parameters capture in place.

```toka
fn print_area(item: dyn @Shape) -> i32 {
    return item.area()
}

auto rect = Rect(w = 10, h = 20)
auto area = print_area(rect)
```

Method calls through `dyn @Trait` are dynamically dispatched through the trait interface. Outside the defining module, only `pub fn` methods in the trait are callable. The stable trait-object syntax is a single trait facet such as `dyn @Shape`; `dyn @{A, B}` is not part of the current public syntax. Dynamic closures use the separate `dyn fn(...) -> T` syntax.

Not every trait can be used as `dyn @Trait`. The 1.0 rule is that a trait object must erase to a fixed receiver handle and a fixed vtable ABI. Therefore, generic traits, traits with associated types, traits with generic methods, and traits whose method signatures use `Self` outside the receiver position are not valid as `dyn @Trait` in 1.0.

Toka 1.0 also does not support associated-type binding syntax on dynamic trait
objects. Forms such as `dyn @Readable<Item = i32>` are rejected. Use a concrete
generic parameter, a wrapper trait without associated types, or a concrete
adapter until this is designed after 1.0.

Trait bounds must use `@Trait` for a single facet and `@{Trait1, Trait2}` for a trait facet set. The names inside a trait facet set are bare because the leading `@` places the whole set in trait context.

```toka
fn draw_one<T: @Drawable>(item: T) {}
fn draw_and_fly<T: @{Drawable, Flyable}>(item: T) {}
fn convert<T, E1: @ErrorInto<E2>, E2>(value: T) {}
```

Forms such as `T: {Drawable, Flyable}`, `T: {@Drawable, @Flyable}`, and `T: @{@Drawable, @Flyable}` are rejected. `path::{...}` in imports is an import item list, not a trait facet set.

The standard prelude makes exactly four semantic-core traits implicitly
visible: `@encap`, `@Send`, `@Sync`, and `@Callable`. All other trait names use
the ordinary lexical module namespace and must be declared in the current
module or selected by an import. Loading a module does not make its unselected
traits visible.

For declarations with non-trivial constraints, use a `where:` block. Each line is one compile-time constraint. The recommended form matches generic parameter bounds: `T: @Trait` or `T: @{Trait1, Trait2}` means the corresponding trait implementation must exist. The historical form `T impl @Trait` is still accepted for compatibility, but it is not the recommended style.

```toka
fn copy<T>(io: T)
where:
    T: @{Reader, Writer}
{
}

trait @Ord
where:
    Self: @{Eq, PartialOrd}
{
}
```

`@encap` is used for explicit resource and visibility control. Resource-owning shapes should define lifecycle behavior in an `impl Type@encap` block.

Typical lifecycle methods in an `@encap` block include `fn drop(self#)` and `pub fn clone(self) = delete`.

Visibility has two syntax layers:

```toka
pub import std/io::{println}
pub shape Device(
    id: i32,
    secret: i32,
    public_config: i32,
    crate_state: i32,
    uart_state: i32
)
pub trait @Readable {
    pub fn read(self) -> i32
}
```

At declaration level, leading `pub` exports imports, constants, functions, shapes, traits, aliases, and nominal types from the module interface. Omitting `pub` keeps the declaration module-private.

Inside normal `impl` and `trait` blocks, method visibility is written with `pub fn`. A method without `pub` is private to its defining module/interface context.

`@encap` blocks additionally control member visibility. Once a shape has an `@encap` block, its fields are private outside the defining source file unless an `@encap` visibility entry grants access.

```toka
impl Device@encap {
    pub public_config
    pub(crate) crate_state
    pub(os/driver/uart) uart_state

    fn drop(self#) {}
    pub fn clone(self) = delete
}
```

`pub field` exposes selected fields globally. `pub(crate) field` exposes selected fields inside the crate. `pub(path) field` grants access to a module path.

The `path` in `pub(path)` uses the same module-location path grammar as the left side of an `import`, before `::{...}` item selection. Toka has no source-level `mod` declaration; path-scoped visibility is anchored in resolver-normalized import paths rather than raw substring matching or a Rust-style module tree.

For broad data carrier shapes, an `@encap` block may also use wildcard visibility entries:

```toka
impl PublicRecord@encap {
    pub *
    pub * ! secret_key, internal_id
}
```

`pub *` exposes all fields, and `pub * ! field1, field2` exposes all fields except the listed fields. Wildcard entries are usually an alternative to enumerating individual fields, not something to mix casually with narrower grants. The parenthesized `pub(crate)` and `pub(path)` forms are `@encap` member-visibility entries, not top-level declaration modifiers.

## 8. Member Access And Morphic Fields

Normal member access requests the payload view.

```toka
auto x = point.x
point.x = 3
```

For a member whose declared shape is a handle, place the hat at the member name when the handle itself is needed.

```toka
shape Data(^p#: i32)

auto d# = Data(^p = new i32(100))
d.^p = new i32(300) // rebind the member handle
d.p = 500           // write through the payload view
```

Morphic fields preserve handle shape inside generic code. The quote is written on the binding name, not on the type side.

```toka
shape Box<'T>(
    'data: T
)

fn take_identity<'T>(cede box: Box<'T>) -> 'T {
    return cede box.'data
}
```

Use `box.'data` when the generic code must preserve the abstract handle shape. Use `box.data` when the code intentionally requests the payload view.

## 9. Generics

Rigid generic parameters describe payload types.

```toka
shape Box<T>(data: T)
```

Morphic generic parameters preserve handle shape.

```toka
shape Box<'T>('data: T)
```

In binding positions, the quote belongs to the binding name:

```toka
fn id<'T>('x: T) -> 'T {
    return 'x
}
```

The same rule applies to local bindings: write `auto 'local: T = expr`, not `auto local: 'T = expr`.

In pure type positions, write `'T`:

```toka
Vec<'T>
Option<'T>
-> 'T
sizeof('T)
```

## 10. Control Flow

Conditionals do not require parentheses.

```toka
if x > 0 {
    return 1
} else {
    return 0
}
```

Loops:

```toka
loop {
    break
}

loop count < 10 {
    count = count + 1
}

for auto x in [1, 2, 3] {
    println("{}", x)
}
```

Non-array iteration uses three ordinary traits from `core/traits`. They are not
implicit prelude traits:

```toka
trait @Iterable {
    type Iter
    pub fn iter(self) -> Iter <- self
}

trait @Iterator {
    type Item
    pub fn next(self#) -> Option<Item>
}

trait @BorrowIterator {
    type BorrowedItem
    pub fn next_ref(self#) -> Option<BorrowedItem> <- self
}
```

`for auto item in values` requires `values: @Iterable` and its `Iter` type to
implement `@Iterator`. `for auto &item in values` additionally requires
`@BorrowIterator`; further reference morphology, such as `&&item`, is preserved
in `BorrowedItem`. The `iter` and `next_ref` dependencies are mandatory: PAL
keeps the source collection borrowed while an explicit cursor is live and for
the duration of a `for` loop. Mutating, replacing, moving, or ceding the source
during that interval is rejected. The hidden cursor is a normal scoped value
and is dropped on exhaustion, `break`, and function exit.

Value iteration does not implicitly cede the collection or its elements. Its
ownership behavior is exactly the declared `Item` returned by `next`, and the
ordinary copy/resource rules still apply. Toka 1.0 does not define a consuming
iterator or async-iterator protocol.

`while` is not part of the current syntax; use `loop condition { ... }`.

`match` supports literals, ranges, variants, guards, or-patterns, wildcards,
and `default`.

```toka
auto value = match x {
    0 => { pass 10 }
    auto v if v > 0 => { pass v }
    default => { pass -1 }
}
```

For 1.0, enum matches are checked for safe exhaustiveness. Every variant must
be covered by an unguarded exhaustive pattern or by `_` / `default`; guarded
arms refine a case but do not count as exhaustive. For non-enum targets, use an
unguarded wildcard, `default`, or unconditional variable arm. The compiler does
not try to prove full integer, range, or string value-domain coverage.

`pass` yields a value from a block expression.

## 11. Pattern Matching And Destructuring

Named destructuring for shapes:

```toka
shape Point(x: i32, y: i32)

auto p = Point(x = 10, y = 20)
auto Point(a = .x, b = .y) = p
```

Use `..` to elide remaining fields and `_` to ignore a field.

```toka
auto Config(h = .host, ..) = cfg
auto Config(h = .host, _ = .port, d = .debug) = cfg
```

Variant matching:

```toka
match result {
    auto Result<i32, str>::Ok(v) => { pass v }
    auto Result<i32, str>::Err(&e) => { pass 0 }
}
```

Or-patterns use `|`. Every alternative in an or-pattern must bind the exact
same names with compatible types and modifiers, because the arm body sees one
merged binding environment.

```toka
shape OpNode(
    Binary(string, Point) |
    Unary(string, Point) |
    Literal(Point)
)

match node {
    auto OpNode::Binary(&op, pos) | auto OpNode::Unary(&op, pos) => {
        pass pos
    }
    auto OpNode::Literal(pos) => { pass pos }
}
```

When destructuring resource-carrying values, use explicit borrowing in the pattern when the original value must not be moved.

Destructuring declarations are local in Toka 1.0. A module-level global may
bind one value, but a destructuring declaration at module scope is rejected
with `E0744`; destructure that value inside a function instead.

## 12. Closures

Closures use `{ ... => ... }` syntax.

```toka
auto add: fn(i32, i32) -> i32 = { a, b => a + b }
auto inc: fn(i32) -> i32 = { .a + 1 }
auto zero: fn() -> i32 = { => 0 }
```

Capture lists are written at the beginning of the closure body.

```toka
auto f: fn(i32) -> i32 = { [cede env] x => x + env }
auto r = 10
auto g: fn(i32) -> i32 = { [copy ~r] x => x + r }
```

`copy` capture is for copyable values or handles. It cannot duplicate ownership
of resource values; use `cede` to transfer the value, or clone into a separate
value before capturing.

When a closure is converted to `dyn fn`, any captured outer variable must be listed explicitly with `cede` or `copy`. This keeps owned, movable closures from silently storing borrowed references to local state.

Ordinary `fn` closures may use implicit borrow captures while they remain local.
If such a closure escapes, for example by being returned from the current
function, those implicit captures are checked as lifetime dependencies. Use
`[cede ...]` or `[copy ...]` when the escaping closure should own or copy the
captured state instead of borrowing it.

Callable permission follows Toka receiver morphology rather than a family of
nominal `Fn` traits:

| Callable type or receiver | Contract |
| :--- | :--- |
| `fn(A) -> R` / `call(self, ...)` | shared, repeatable invocation |
| `fn#(A) -> R` / `call(self#, ...)` | exclusive, repeatable invocation |
| `cede fn(A) -> R` / `call(cede self, ...)` | consuming invocation |

The compiler infers a closure's least required permission from its body.
Reading captures requires shared invocation, mutating captured state requires
exclusive invocation, and transferring a captured value with `cede` requires
consuming invocation. A `[cede value]` capture only gives the closure ownership;
it does not make the closure consuming unless the body transfers that value.

Binding permission remains separate from callable permission:

```toka
auto counter#: fn#(i32) -> i32 = { [cede state] value =>
    state.value = state.value + value
    state.value
}
auto next = counter#(1)
```

`counter#` on the declaration and call grants exclusive access to the binding;
`fn#(...)` records that the callable requires that access. A consuming callable
is invoked as `cede take()`. If the called value is not consuming, the same
`cede` expression applies only to the returned value and does not consume the
callable.

`@Callable` is the single implicit-prelude callable protocol. User-defined
types implement a `call` method with `self`, `self#`, or `cede self`; values of
the type then support ordinary call syntax. Generic bounds use `F: @Callable`.
Callable receiver mode and returned lifetime dependencies are preserved in
same-version TKI interfaces. Thread callbacks that mutate owned captures use
the exclusive `fn#` contract; detached execution still applies the ordinary
explicit-capture, dependency, and `@Send` rules.

### Result propagation

Postfix `!` consumes a `Result<T, E>` or `Option<T>`. Success moves out the
payload. Failure returns `Err` or `None` after dropping every still-live local
in reverse lexical order. The operand is evaluated exactly once. Propagation
from a whole local binding marks that binding moved; partial paths such as
`holder.result!` are conservatively rejected in 1.0 and should first be bound
to a local value.

For `Result<T, E1>` inside a function returning `Result<U, E2>`, `E1` and `E2`
must be the same resolved type or `E1` must implement the ordinary, explicitly
imported protocol below:

```toka
trait @ErrorInto<Target> {
    pub fn into_error(cede self) -> Target
}
```

The conversion consumes `E1` and runs exactly once on the error path. It is a
single direct conversion: Toka does not search conversion chains and does not
use numeric widening, structural compatibility, or raw representation copying
for errors. Parameterized bounds such as `E1: @ErrorInto<E2>` are valid in
generic declarations and `where:` blocks. The selected implementation and
receiver/return contract are preserved in same-version TKI interfaces.

`std/error::ErrorContext<E>` stores an owned message and the original typed
error. `with_context(cede result, message)` returns a context-bearing Result
without erasing the source. Async `.await!` applies these same conversion,
move, and cleanup rules after resumption. Toka 1.0 has no throw/catch,
automatic conversion chain, universal `dyn error`, or implicit cleanup-error
replacement policy.

### Typed todos for incomplete edits

`todo` is a reserved, expression-only keyword for an incomplete edit. It is
not a value, variable, wildcard, ownership source, or permissive build mode.
Every occurrence has its own diagnostic and keeps the check/build result
nonzero.

```toka
auto answer: i32 = todo      // a complete `i32` requirement is known
if todo {                    // a complete `bool` requirement is known
    return 0
}
```

The compiler records a requirement only when the surrounding context already
determines it: an explicitly typed local binding, assignment to an existing
binding, a boolean condition, an ordinary resolved call parameter, or an
explicitly instantiated generic call. The program remains incomplete and
reports `E04603` in those cases. A context that would need inference, such as
`auto answer = todo` or `identity(todo)`, reports `E04604` instead.

Holes cannot stand for a place, capability, provenance, or transfer. Prefix
and postfix access, member/index access, guards, `cede todo`, and todos passed
to a `cede` parameter are rejected with `E04605`. In particular, a todo never
creates H/P authority or silently transfers a resource.

For editor and AI tooling, request deterministic requirement facts with:

```bash
toka todo-goals --json --check-only path/to/source.tk
# or: tokac --todo-goals=json --check-only path/to/source.tk
```

The output is a requirement-only protocol; it is not ordinary semantic
evidence and must not be treated as compiler approval. A reachable todo emits
no executable, object, TKI, or reusable compilation artifact. See
[Typed Todo v1](typed_todo_goals_v1.md) for the machine-readable schema and
[the RFC](semantic_core/typed_todo_rfc.md) for the complete boundary.

## 13. Strings, Text, And Formatting

String-like values appear in several layers:

| Form | Role |
| :--- | :--- |
| `"..."` | `str` text view |
| `"""..."""` | raw `str` text view |
| `c"..."` | C string literal |
| `string::from("...")` | owned mutable `string` |

Raw `str` literals use a quote fence with three or more double quotes. The
closing fence must contain exactly the same number of quotes as the opening
fence, so a longer fence can contain a shorter quote run:

```toka
auto path = """C:\Users\toka\config.json"""
auto quoted = """"
    Markdown can contain """ without escaping.
    """"
```

Raw literals do not process escapes or interpolation and have the same static,
read-only `str` representation as ordinary text literals. The fence has no
prefix; in particular, `c"""..."""` is not a raw C string.

A single-line raw literal closes on its opening line. A multiline raw literal
starts when only spaces or tabs occur between its opening fence and the next
line break. Its opening line break and the line break immediately before the
closing fence are structural and are not part of the value. The closing fence
must appear on an otherwise blank line; its indentation is removed from every
non-blank content line, while additional indentation is preserved. Blank lines
are normalized to empty lines, and CRLF, CR, and LF source line endings become
`\n` in the resulting `str`.

Formatting uses `{}` placeholders.

```toka
println("x={}, y={}", x, y)
```

Plain `{}` accepts `String` and `str`. Format specifiers for those text forms
are outside the 1.0 surface; `E04547` identifies that exclusion.

Equality between an owned `string` and a `str` view, including a text literal,
uses the owned value's zero-allocation read-only `as_str()` projection. Both
`command == "scan"` and `"scan" == command` compare contents without
allocating, cloning, moving, or consuming `command`.

The same projection is available when a function or method parameter has the
unique expected type `str`, so an owned `string` can be passed directly without
an explicit `.as_str()`. It does not apply to `*string`: obtaining a text view
through a raw pointer remains explicit because raw dereference, provenance, and
nullability are outside PAL's safe-borrow guarantee.

String concatenation with `+` is not part of the public syntax. Build owned strings explicitly with `string` APIs such as `push_str`.

## 14. Unsafe And FFI

External functions use `extern fn`.

```toka
extern fn sleep(seconds: i32) -> i32
extern fn libc_free(*ptr: void) -> void
```

Raw allocation and deallocation are explicit and unsafe.

```toka
shape Node(val: i32)

auto *node = unsafe alloc Node(val = 1)
unsafe free *node
```

For a raw array, the count on `free[count]` is the number of live elements
starting at index zero that must be dropped before the allocation is released.
It is not the allocation capacity. A raw container with a contiguous live
prefix uses `free[len]`; after every live element has been moved elsewhere it
uses `free[0]` to release only the storage. Containers whose live elements are
not a contiguous prefix must drop those elements themselves and then use
`free[0]`.

```toka
auto *buf = unsafe alloc [capacity] Resource
// initialize buf[0..len]
unsafe free [len] *buf

auto *old = unsafe alloc [capacity] Resource
// move every live element from old into replacement storage
unsafe free [0] *old
```

Pointer casts use `as`.

```toka
auto *ptr = addr as *i32
auto raw = *ptr as *void
```

Unsafe code should stay at system and FFI boundaries. Public APIs should avoid exposing raw pointers unless the API is explicitly named as unsafe or raw. Raw pointers are outside PAL's safe-borrow guarantee unless wrapped by a safe library capability.

### Core Runtime Contract

On normal scope exits, including structured control-flow exits and returns,
every still-live owned value is cleaned up exactly once. A successful `cede`
or move transfers that obligation and prevents cleanup of the moved-from path.

`panic` is non-returning process termination in Toka 1.0. It is not a catchable
exception and does not promise stack unwinding or cleanup after the panic
point. Bounds and null checks that fail through the safe runtime use this same
non-returning failure boundary. Raw allocation, foreign calls, and raw-pointer
validity remain obligations of explicit unsafe/FFI code.

## 15. Compatibility Contract

Toka 1.x preserves the source-level meaning of programs in the frozen 1.0
surface. Additive features and releases that relax a conservative rejection
may be source-compatible extensions. A memory-safety or miscompile correction
may reject code that depended on unsound behavior and is recorded as a safety
fix.

Diagnostic codes are not reused for unrelated rules during 1.x. Diagnostic
wording and source highlighting may improve. `.tki`, build-cache formats,
generated object layout, and binary ABI are compiler- and format-version-bound
and do not carry a cross-version compatibility promise.

## 16. Common Mistakes

| Avoid | Use | Reason |
| :--- | :--- | :--- |
| `let x = 1` / `var x = 1` | `auto x = 1` | Toka uses `auto` for local binding declarations |
| `while cond { ... }` | `loop cond { ... }` | Conditional loops use `loop` |
| `for x in iter { ... }` | `for auto x in iter { ... }` | Iteration bindings are explicit |
| `Point(1, 2)` | `Point(x = 1, y = 2)` | Shape initialization is named |
| `*p` to read the payload | `p` | Plain names operate on payload |
| `*p = value` to write payload | `p = value` | Hat assignment operates on the handle |
| `p = q` to rebind pointer identity | `*p = *q` | Rebinding is a handle operation |
| `fn read(info: &Info)` | `fn read(info: Info)` | Hats belong to binding names, and ordinary parameters use the payload view |
| `fn inspect(&info: Info)` when only reading payload | `fn inspect(info: Info)` | A hatted parameter is a handle contract, not a spelling for normal passing |
| `shape Ref <- val (&val: T)` | `shape Ref(&val: T)` | Borrow-like fields carry dependency facts directly; shape header dependencies are removed |
| `fn id<'T>(x: 'T) -> 'T` | `fn id<'T>('x: T) -> 'T` | In binding positions, the quote belongs to the binding name |
| `shape Box<'T>('data: 'T)` | `shape Box<'T>('data: T)` | The field name preserves morphology; the type side remains `T` |
