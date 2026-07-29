# Typed Todo Goals v1

`toka todo-goals --json path/to/source.tk` and
`tokac --todo-goals=json path/to/source.tk` emit deterministic requirements
for each parsed `todo`. The schema is
[`schemas/toka.todo-goals.v1.schema.json`](../schemas/toka.todo-goals.v1.schema.json).

The command is intentionally an incomplete-program interface: it exits
nonzero, emits no object, executable, TKI, cache entry, or ordinary Allow
evidence. Its JSON is a requirement report, not a successful type check.

Each goal contains a parser-local `id`, its source location, and one status:

- `incomplete`: a complete target contract was already known;
- `underconstrained`: accepting the todo would require inference or candidate
  selection; and
- `unsupported`: the surrounding form would make the todo a place, transfer,
  capability, or other excluded v1 construct.

For `incomplete`, `contract` records the resolved type, direct morphology,
transfer mode, H/P requirement, nullability, and any required dependency. v1
supports only `transfer: "none"`; a `cede` requirement is deliberately
unsupported. A `null` contract never means an unconstrained todo is accepted.

For example:

```toka
fn main() -> i32 {
    auto answer: i32 = todo
    return answer
}
```

emits one `incomplete` goal with a value `i32` contract. An AI can use this
fact to synthesize a candidate, then must replace `todo` and run a normal
check. It must not treat the reported contract as a compiler grant of
ownership, H/P authority, provenance, or publication eligibility.

The ABI gate is `tools/scripts/test_typed_todo_goals.py`. It freezes envelope
shape, deterministic ordering, contract fields, and the separation between
complete requirements and underconstrained/unsupported forms.
