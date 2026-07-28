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

v1 is a binding slice. It records a typed binding initialized directly from a
`hole`, and carries the same `conditional_on` hole IDs through a direct binding
alias:

```toka
auto answer: i32 = hole
auto forwarded: i32 = answer
```

Both facts are `status: "conditional"` and list the parser-local ID of the
same hole. The compiler does not yet infer conditional facts through arbitrary
operators, calls, aggregate construction, or control-flow joins; omitting such
a fact is conservative and must not be interpreted as authoritative success.

Each record contains the binding name, resolved declared type, conditional hole
IDs, and declaration location. Consumers must replace the hole and run a
normal check before considering any dependent binding complete. The ABI gate
is `tools/scripts/test_typed_hole_conditional_facts.py`.
