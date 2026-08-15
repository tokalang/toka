# Toka AI Completion Card

**Status:** Living companion for the current supported Toka language surface.
Every SDK release archive freezes the copy that matches that release; this
source-tree card evolves with compiler-verified language changes.

Give this card to a coding agent once at the start of a focused Toka task. It
is a language-context aid, not a substitute for `toka check --json`, public
API contracts, compiler diagnostics, or project tests. Keep supplied imports,
aliases, signatures, and public shapes unless the task explicitly asks to
change them. Prefer the smallest repair that satisfies the compiler diagnostic
and runtime check.

## Binding and mutation

Toka separates a payload (`x`) from a handle/access representation (`&x`,
`*x`, `^x`, `~x`). Do not add a handle sigil merely because a payload must
mutate.

Declare a payload that may be written with `#`, then use the plain name for an
assignment or compound assignment:

```toka
auto total# = 0:i32
total = 1
total += 1
```

For a mutable method call, put `#` on the receiver. This is a local audit mark
for a call that mutates hidden iterator or collection state:

```toka
auto values# = Vec<i32>::new()
values#.push(7)
auto iter# = values.iter()
auto item = iter#.next_ref()
```

Never write `total# = ...`, `done# = ...`, or `return total#`. A declaration
defines H/P authority. In a handle-selecting call or pattern, `#` can only
request already-declared authority; it never upgrades the declaration.

## Mutable parameters and shared fields

A parameter a function writes is declared with `#` (`counter#: Counter`). The
caller explicitly marks the mutable call argument with `#` (`increment(counter#)`);
omitting `#` on a mutable argument emits warning `W0408`. Inside the body, a field
write uses the bare name, while a mutating method call uses `#` on its receiver.

```toka
shape Counter(value#: i32)

fn increment(counter#: Counter) -> Counter {
    counter.value += 1
    return counter
}

auto counter# = Counter(value = 0)
auto next = increment(counter#)
```

Only a field declared with `#` may be changed through a shared view. An
ordinary field remains read-only:

```toka
shape Metrics(hits#: i32, limit: i32)

fn record(metrics: Metrics) {
    metrics.hits += 1
}

auto metrics = Metrics(hits = 0, limit = 3)
auto &shared = &metrics
record(shared)
```

An ordinary parameter is a payload view, not an ownership transfer. The caller
may keep using it unless the signature and call explicitly say `cede`.

## Shape construction and deconstruction punning

For shapes with multiple fields or mixed lists, prefer same-name field punning:

```toka
shape Point(x: i32, y: i32)

auto x = 10
auto y = 20
auto p = Point(x, y)             // Preferred: same-name construction pun
auto Point(x, y) = p             // Preferred: same-name deconstruction pun
auto p2 = Point(x, y = 99)       // Mixed pun and explicit override
auto Point(x, other = .y) = p2   // Mixed pun and explicit renaming
```

Single-field initialization uses the explicit form `Wrapper(value = value)`. Never use positional literals or expressions like `Point(10, 20)`; shape fields are always bound by name or pun.

## Borrowed `Vec` scan

`iter()` borrows a vector. `next_ref()` advances the iterator and returns an
`Option<&T>`, so its iterator receiver must be `iter#`. If an alias is in
scope, match through that alias exactly.

```toka
alias OptRefI32 = Option<&i32>

auto total# = 0:i32
auto iter# = values.iter()
auto done# = false:bool
loop !done {
    auto item = iter#.next_ref()
    match item {
        auto OptRefI32::Some(&value) => {
            auto copied = value:i32
            total += copied
        }
        auto OptRefI32::None => { done = true }
    }
}
```

Use `auto OptRefI32::Some(&value)`, not `auto &value`. Copy a borrowed scalar
with `value:i32` before accumulating or storing it.

## Explicit ownership transfer

`cede` marks a real ownership handoff. When a signature accepts `cede value`,
the caller must call `fn(cede value)` and the body must consume, forward, store,
or return it. For an owned vector that needs mutation, first bind the ceded
payload as mutable, then return it with `cede`.

```toka
fn append_one(cede values: Vec<i32>) -> Vec<i32> {
    auto owned# = cede values
    owned#.push(7)
    return cede owned
}

auto owned# = append_one(cede values)
```

Preserve a supplied `cede` boundary; do not remove it to address an unrelated
mutability error.

## Copy before mutating

A live borrow (`iter()`, `next_ref()`, or `&`) blocks a later mutation of the
same collection. Copy the needed value first, then mutate the collection:

```toka
fn peek_then_append(values#: Vec<i32>) -> i32 {
    auto first = values.get(0)
    values#.push(100)
    return first
}
```

## `Result` propagation

Construct success and failure explicitly as `Result<T, string>::Ok(value)` and
`Result<T, string>::Err(string::from("message"))`. Postfix `!` propagates a
`Result` failure only when the enclosing function returns the compatible error
type.

```toka
fn compute(a: i32, b: i32) -> Result<i32, string> {
    auto quotient = safe_div(a, b)!
    return Result<i32, string>::Ok(quotient + 1)
}
```

`unwrap()` consumes a `Result`; do not query and unwrap the same binding in one
short-circuit expression. First branch on its status, then consume it:

```toka
auto result = parse_decimal(text)
if result.is_err() { return 1 }
auto value = result.unwrap()
```

## String parsing: bytes, not chars

For an owned `string`, use `text.len()` for the byte length. `str.at()` indexes
Unicode scalars, not bytes. A bounded byte lookup is
`text.as_str().as_bytes().at(index).unwrap() as i32`. ASCII decimal bytes are
48 through 57. Reject any other value before using it as a digit.

```toka
auto total# = 0:i32
auto index# = 0:usize
auto bytes = text.as_str().as_bytes()
loop index < text.len() {
    auto code = bytes.at(index).unwrap() as i32
    if code < 48 || code > 57 {
        return Result<i32, string>::Err(string::from("non-digit"))
    }
    auto digit = code - 48
    if total > 214748364 || (total == 214748364 && digit > 7) {
        return Result<i32, string>::Err(string::from("overflow"))
    }
    total = total * 10 + digit
    index = index + 1
}
return Result<i32, string>::Ok(total)
```

Reject an empty input before the loop. The limit check must occur before
`total * 10 + digit` so an `i32` never wraps.

## Borrowed text views

A borrowed text view must declare its dependency in the signature. `str::trim()`
returns such a view; route it with `<-`.

```toka
fn trimmed(text: str) -> str <- text {
    return text.trim()
}
```

## File I/O and owned strings

`std/fs::read_to_string` returns `Result<string, string>`,
`std/fs::write_string_atomic` returns `Result<bool, string>`, and
`std/io::remove_file` returns `bool`. Convert a number to text with
`string::from_int`, and pass `path.clone()` when a consumed path is reused.

```toka
auto content = read_to_string(path.clone())
if content.is_err() {
    return Result<bool, string>::Err(string::from("read failed"))
}
auto text = content.unwrap()
auto label = string::from_int(text.len() as i32)
return write_string_atomic(path.clone(), label.as_str())
```

## Fast diagnostic repairs

| Compiler symptom | First check |
| --- | --- |
| Mutable payload capability needed | Declare `auto name# = ...`; use `name#.method()` for a mutating method |
| Cannot assign to immutable | Declare the binding or parameter with `#`; write through the bare name |
| `#` illegal in an everyday expression | Remove it from assignment, return, and arithmetic; for call arguments, use `#` only when the parameter requires mutable payload access |
| `&value` does not name a variable | Use the complete arm `auto Alias::Some(&value)` |
| Cede obligation incomplete | Forward, store, consume, or `return cede value` on every required path |
| Moved `Result` | Check `is_err()` in its own statement, then consume once with `unwrap()` or a match |
| `u32` versus `char` comparison | Iterate the byte view and compare integer codes 48 through 57, not `'0'` |

Before broad rewrites, preserve already-correct ownership and authority markers
while repairing the reported site. Compiler diagnostics are authoritative for
the current candidate.
