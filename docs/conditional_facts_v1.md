# Typed Todo Conditional Facts v1

`toka conditional-facts --json path/to/source.tk` and
`tokac --conditional-facts=json path/to/source.tk` expose editor-only facts
for bindings that depend on an incomplete typed todo. The schema is
[`schemas/toka.conditional-facts.v1.schema.json`](../schemas/toka.conditional-facts.v1.schema.json).

This is intentionally separate from both Public Semantic Evidence and
`toka.todo-goals`. A conditional fact is neither a completed initialization
nor an `Allow` decision. The command keeps the compilation failure caused by
the todo and never enables code generation, TKI export, cache publication, or
trusted evidence.

v1 records a typed binding initialized directly from a `todo`, and carries the
same `conditional_on` todo IDs through direct aliases, non-transfer expression
operands, and resolved call arguments:

```toka
auto answer = todo:i32
auto forwarded = answer:i32
auto doubled = (forwarded + forwarded):i32
auto observed = identity(doubled):i32
```

Both facts are `status: "conditional"` and list the parser-local ID of the
same todo. `cede`, borrowed/provenance-sensitive forms, and control-flow joins
remain excluded except for value-producing `if` and `match` arms. A
non-comptime `if` unions the dependencies of both value arms; a comptime `if`
visits only its selected arm. A `match` unions its target, arm guards, and arm
body values, but does not infer dependencies for pattern-local bindings. The
compiler otherwise omits a fact rather than guessing ownership, provenance, or
branch reachability. Such omission must not be interpreted as authoritative
success.

A direct assignment to a whole local binding updates its internal dependency
state, so that a later declaration initialized from the binding is conditional
as well. v1 does not emit a separate assignment record: the wire format has no
temporal overwrite field. Member, index, dereference, compound-assignment, and
loop/escaping-call propagation remain outside this slice. A regular `if`
merges its reachable whole-binding states by union.

Each record contains the binding name, resolved declared type, conditional todo
IDs, and declaration location. Consumers must replace the todo and run a
normal check before considering any dependent binding complete. The ABI gate
is `tools/scripts/test_typed_todo_conditional_facts.py`.
