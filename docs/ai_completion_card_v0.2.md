# Toka AI Completion Card v0.2

**Status:** Experimental companion for the Toka 1.0 RC language surface.

Use this card with the supplied Toka starter. Keep its imports, aliases,
signatures, and public shapes. Prefer the smallest repair that satisfies the
compiler diagnostic and runtime check.

## Binding and mutation

Toka separates a payload (`x`) from a handle/access representation (`&x`,
`*x`, `^x`, `~x`). Do not add a handle sigil because a payload must mutate.

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

Never write `total# = ...`, `done# = ...`, or `return total#`. `#` belongs on
the declaration and on an explicit mutable method receiver, not on ordinary
expressions.

## Shapes, shared authority, and `cede`

Only a field declared with `#` may be changed through a shared view:

```toka
shape Metrics(hits#: i32, limit: i32)
auto &shared = &metrics
shared.hits += 1
```

An ordinary parameter is a payload view, not an ownership transfer. `cede` is
a real ownership handoff: use it at the declared parameter, call, and return
boundary.

```toka
fn append_one(cede values: Vec<i32>) -> Vec<i32> {
    auto owned# = cede values
    owned#.push(7)
    return cede owned
}

auto owned# = append_one(cede values)
```

Do not remove a supplied `cede` just to address an unrelated mutability error.

## Borrowed Vec scan

`iter()` borrows a vector. `next_ref()` advances the iterator and returns an
`Option<&T>`, so its iterator receiver must be `iter#`. If an alias is in scope,
match through that alias exactly.

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

## Result, string, and checked decimal parsing

Construct success and failure explicitly as `Result<T, string>::Ok(value)` and
`Result<T, string>::Err(string::from("message"))`. Postfix `!` propagates a
`Result` failure only when the enclosing function returns the compatible error
type.

`unwrap()` consumes a `Result`; do not query and unwrap the same binding inside
one short-circuit expression. First branch on the status, then consume it:

```toka
auto result = parse_decimal(text)
if result.is_err() { return 1 }
auto value = result.unwrap()
```

For an owned `string`, use `text.len()` for the byte length. A bounded byte
lookup is `text.as_str().at(index).unwrap() as i32`. ASCII decimal bytes are
48 through 57. Reject any other value before using it as a digit.

```toka
auto total# = 0:i32
auto index# = 0:usize
loop index < text.len() {
    auto code = text.as_str().at(index).unwrap() as i32
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

## Fast diagnostic repairs

| Compiler symptom | First check |
| --- | --- |
| Mutable payload capability needed | Declare `auto name# = ...`; use `name#.method()` for its mutating method |
| `#` illegal in an everyday expression | Remove it from assignment, return, and arithmetic; retain it at declaration/mutable receiver |
| `&value` does not name a variable | Use `auto Alias::Some(&value)` |
| Cede obligation incomplete | Forward, store, consume, or `return cede value` on every required path |
| Moved `Result` | Check `is_err()` in its own statement, then consume once with `unwrap()` or a match |

Before broad rewrites, preserve already-correct ownership and authority markers
while repairing the reported site. Compiler diagnostics are authoritative for
the current candidate.
