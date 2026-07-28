# Typed Hole Conditional Facts v1

`toka conditional-facts --json path/to/source.tk` and
`tokac --conditional-facts=json path/to/source.tk` expose editor-only facts
for bindings that depend on an incomplete typed hole. The schema is
[`schemas/toka.conditional-facts.v1.schema.json`](../schemas/toka.conditional-facts.v1.schema.json).

This is intentionally separate from both Public Semantic Evidence and
`toka.hole-goals`. A conditional fact is neither a completed initialization
nor an `Allow` decision. The command keeps the compilation failure caused by
the hole and never enables code generation, TKI export, cache publication, or
trusted evidence.

v1 records a typed binding initialized directly from a `hole`, and carries the
same `conditional_on` hole IDs through direct aliases, non-transfer expression
operands, and resolved call arguments:

```toka
auto answer: i32 = hole
auto forwarded: i32 = answer
auto doubled: i32 = forwarded + forwarded
auto observed: i32 = identity(doubled)
```

Both facts are `status: "conditional"` and list the parser-local ID of the
same hole. `cede`, borrowed/provenance-sensitive forms, and control-flow joins
remain excluded except for an `if` expression's value arms. A non-comptime
`if` unions the dependencies of both value arms; a comptime `if` visits only
its selected arm. The compiler otherwise omits a fact rather than guessing
ownership, provenance, or branch reachability. Such omission must not be
interpreted as authoritative success.

Each record contains the binding name, resolved declared type, conditional hole
IDs, and declaration location. Consumers must replace the hole and run a
normal check before considering any dependent binding complete. The ABI gate
is `tools/scripts/test_typed_hole_conditional_facts.py`.
