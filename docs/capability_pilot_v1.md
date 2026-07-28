# H/P Call Capability Pilot v1

`toka capabilities --json path/to/source.tk` and
`tokac --capabilities=json path/to/source.tk` emit a narrow, deterministic
explanation of the existing H/P authority check for ordinary function and
extern call arguments. The schema is
[`schemas/toka.capability-pilot.v1.schema.json`](../schemas/toka.capability-pilot.v1.schema.json).

Each record contains two independent axes:

- `handle_rebind` (H): authority to rebind an existing handle identity.
- `payload_write` (P): authority to write the referenced payload.

For a call argument it reports:

```text
declared       direct binding/field declaration ceiling
inferred       current capability after direct flow restrictions
request        the capability requested by the argument spelling
required       the callee parameter signature requirement
granted        the requirement that actually passed this check
independent_cede  whether a whole independent cede root supplies the P case
```

The grant is intentionally not inferred from syntax alone:

```toka
fn overwrite(^p#: Cell) { p.value = 9 }
auto ^#handle_only = new Cell(value = 1)
overwrite(^#handle_only) // P remains denied
```

The record for this call shows `declared.payload_write = false`,
`inferred.payload_write = false`, `required.payload_write = true`, and
`granted.payload_write = false`. A use-site `#` is a request; it cannot create
the declaration capability.

Conversely, a `^p#` binding can supply P but not H. The protocol makes that
distinction directly available to an AI repair loop, instead of requiring it
to reconstruct authority from diagnostic text.

## Scope and non-goals

This is a deliberately narrow pilot, not a second permission system:

- It observes current ordinary function/extern call argument checks only.
- It does not change PAL, type compatibility, `cede`, or the H/P rule.
- Static methods, instance methods, callable/dyn-call paths, assignments, and
  field/index operations remain future extension points. Tools must not infer
  that their absence is an allow decision.
- `declared` is the direct source ceiling; no provenance walk is performed.

`tools/scripts/test_capability_pilot.py` freezes the JSON ABI and verifies an
allow plus both orthogonal denial directions. It also confirms that the SDK
wrapper is byte-identical to direct compiler output and that Public Semantic
Evidence v1 remains a separate protocol.
