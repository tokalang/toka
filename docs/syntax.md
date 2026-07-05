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

The entry point is `main`, and it normally returns `i32`.

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

Nullable payload types use `?` on the type side and `none` as the empty payload value.

```toka
auto maybe: i32? = none
```

Nullable handles use the `nul` marker and `null`.

```toka
auto nul *ptr: i32 = null
```

Borrow handles (`&`) are not nullable; use `nul` only with raw, unique, or shared handle forms.

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

## 5. Functions, Parameters, And `cede`

Function parameters are explicitly typed.

```toka
fn add(a: i32, b: i32) -> i32 {
    return a + b
}
```

For ordinary object parameters, Toka uses logical in-place capture. If the function wants the payload view, use forms such as `x: T` or `x#: T`.

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

Execution boundaries are stricter than ordinary local calls. For Toka 1.0,
thread/task handoff must not carry hidden borrowed state: a closure passed to
`thread_spawn` cannot implicitly capture outer variables, and future task
handoff forms must follow the same rule. State that crosses such a boundary
must be made explicit, typically by `[cede ...]` transfer or `[copy ...]` for
copyable data. Async return dependencies such as `fn f(x: str) -> async str <-
x` remain ordinary signature dependencies; they do not authorize detached tasks
to keep an undeclared borrow.

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
- A shared borrow blocks invalidating or exclusive mutation of the same path or an overlapping parent / child path.
- Ordinary payload writes do not by themselves count as invalidation, but writing a parent path that would replace storage containing an active borrow remains invalidating and is rejected.
- A mutable borrow blocks both reads and writes through overlapping paths unless the access is proven disjoint.
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

Not every trait can be used as `dyn @Trait`. The current public rule is that a trait object must erase to a fixed receiver handle and a fixed vtable ABI. Therefore, generic traits, traits with associated types, traits with generic methods, and traits whose method signatures use `Self` outside the receiver position are not currently valid as `dyn @Trait`.

Toka 1.0 also does not support associated-type binding syntax on dynamic trait
objects. Forms such as `dyn @Readable<Item = i32>` are rejected. Use a concrete
generic parameter, a wrapper trait without associated types, or a concrete
adapter until this is designed after 1.0.

Trait bounds must use `@Trait` for a single facet and `@{Trait1, Trait2}` for a trait facet set. The names inside a trait facet set are bare because the leading `@` places the whole set in trait context.

```toka
fn draw_one<T: @Drawable>(item: T) {}
fn draw_and_fly<T: @{Drawable, Flyable}>(item: T) {}
```

Forms such as `T: {Drawable, Flyable}`, `T: {@Drawable, @Flyable}`, and `T: @{@Drawable, @Flyable}` are rejected. `path::{...}` in imports is an import item list, not a trait facet set.

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

## 13. Strings, Text, And Formatting

String-like values appear in several layers:

| Form | Role |
| :--- | :--- |
| `"..."` | `str` text view |
| `c"..."` | C string literal |
| `string::from("...")` | owned mutable `string` |

Formatting uses `{}` placeholders.

```toka
println("x={}, y={}", x, y)
```

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

Pointer casts use `as`.

```toka
auto *ptr = addr as *i32
auto raw = *ptr as *void
```

Unsafe code should stay at system and FFI boundaries. Public APIs should avoid exposing raw pointers unless the API is explicitly named as unsafe or raw. Raw pointers are outside PAL's safe-borrow guarantee unless wrapped by a safe library capability.

## 15. Common Mistakes

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
| `fn id<'T>(x: 'T) -> 'T` | `fn id<'T>('x: T) -> 'T` | In binding positions, the quote belongs to the binding name |
| `shape Box<'T>('data: 'T)` | `shape Box<'T>('data: T)` | The field name preserves morphology; the type side remains `T` |
