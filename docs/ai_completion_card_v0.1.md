# Toka AI Completion Card v0.1

**Status:** Experimental companion for the Toka 1.0 RC language surface.

Give this card to a coding agent once at the start of a Toka task. It is a
language-context aid, not a substitute for `toka check --json`, public API
contracts, compiler diagnostics, or project tests. Keep the supplied imports,
aliases, signatures, and public shapes unless the task explicitly asks to
change them.

## Binding and mutation

Toka separates a payload (`x`) from a handle/access representation (`&x`,
`*x`, `^x`, `~x`). Do not add a handle sigil because a payload must mutate.

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

Never write `total# = ...`, `done# = ...`, or `return total#`. `#` belongs on
the declaration and on an explicit mutable method receiver, not on ordinary
expressions.

## Shapes and shared field authority

Only a field declared with `#` may be changed through a shared view. An
ordinary field remains read-only.

```toka
shape Metrics(hits#: i32, limit: i32)

auto metrics = Metrics(hits = 0, limit = 3)
auto &shared = &metrics
shared.hits += 1
// shared.limit += 1  // rejected
```

An ordinary parameter such as `values: Vec<i32>` or `metrics: Metrics` is a
payload view, not an ownership transfer. A caller may keep using it unless the
signature and call explicitly say `cede`.

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

Use `auto OptRefI32::Some(&value)` for this pattern. Do not replace it with
`auto &value`. Copy a borrowed scalar using `value:i32` before accumulating or
storing it. A different computation can replace only the body of the `Some`
branch, for example `if copied > 0 { count += 1 }`.

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

For copied `i32` elements, `Vec::get(index)` reads the value; `push`, `iter`,
and `len` are the common corresponding operations. Preserve a supplied `cede`
boundary; do not remove it to address an unrelated mutability error.

## Fast diagnostic repairs

| Compiler symptom | First check |
| --- | --- |
| A method needs mutable payload capability | Declare `auto name# = ...`; call its mutating method as `name#.method()` |
| `#` illegal in an everyday expression | Remove it from `name# =`, `return name#`, and arithmetic; retain it at declaration/mutable receiver |
| `&value` does not name a variable | Use the complete arm `auto Alias::Some(&value)` |
| Cede obligation is incomplete | Forward, store, consume, or `return cede value` on every required path |

Before broad rewrites, compare the candidate to these exact forms. Compiler
diagnostics are evidence about the current program; preserve already-correct
ownership and authority markers while repairing the reported site.
