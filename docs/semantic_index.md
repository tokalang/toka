# Semantic index and query protocol

The semantic index is the compiler-owned tooling boundary for Toka 1.0. It is
built after semantic analysis and records resolved symbol identities rather
than reconstructing meaning from tokens. LSP, editor, and AI coding tools
should consume this API instead of implementing a second name resolver.

## Command line

Emit the complete workspace index:

```sh
tokac --semantic-index=json path/to/main.tk
# Equivalent SDK command:
toka index --json path/to/main.tk
```

Run a focused query using zero-based LSP line and character positions:

```sh
tokac --semantic-query definition \
  --query-file path/to/main.tk --line 12 --character 8 path/to/main.tk
```

Supported query names are `symbolAt`, `hover`, `definition`, `references`,
`completion`, `documentSymbols`, `workspaceSymbols`, and `rename`. Rename also
requires `--rename-to NAME`.

Both protocols are deterministic, machine-readable JSON. The full index uses
schema `toka.semantic-index` version 1; focused queries use
`toka.semantic-query` version 1. Paths are canonical absolute paths and source
positions are zero-based, half-open ranges. Symbol IDs are stable for an
unchanged declaration location, kind, name, and container.

## Guarantees

- Calls use the function or method selected by semantic analysis.
- Locals and parameters carry scope-specific identities, including shadowed
  declarations with the same spelling.
- Definition, references, and rename operate on symbol identity.
- Rename rejects invalid identifiers and same-scope conflicts.
- Completion and hover records contain resolved type and signature detail.
- Declarations, fields, variants, traits, aliases, globals, parameters,
  locals, reads, writes, calls, and type uses are represented.

## Declaration contracts

Function, method, extern-function, and field declaration symbols additionally
carry an optional `contract` object. This is the compiler's normalized record
of the declaration's ownership and permission surface; it is not a statement
that an arbitrary use site has gained that permission.

Callable contracts have this shape:

```json
{
  "kind": "callable",
  "effect": "sync | async | wait",
  "variadic": false,
  "parameters": [{
    "name": "p",
    "type": "^Cell#",
    "morphology": "value | unique | shared | reference | raw",
    "flow": "value | unique | shared | borrow | unsafe-raw | cede",
    "payloadWritable": true,
    "payloadBlocked": false,
    "handleRebindable": false,
    "handleBlocked": false,
    "handleNullable": false,
    "payloadNullable": false
  }],
  "return": { "type": "i32", "dependencies": [] }
}
```

`return.dependencies` contains resolved borrow-dependency names when a Toka
function declares them. Extern declarations have the same shape but no
source-level dependency list. A field uses the same morphology, flow, and
permission flags with `"kind": "field"`.

Tools must treat a contract as a declaration capability: a use-site marker
expresses intent and is still constrained by the declared capability and PAL.
This distinction prevents an AI tool from suggesting a `#` marker as though it
could create payload or handle authority. The field is optional so consumers
must tolerate its absence for symbol kinds without a declaration contract.

The index is an internal SDK protocol for Toka 1.x. Consumers must check its
schema and version rather than assuming forward-compatible fields.

## Regression gate

`python3 tools/scripts/test_semantic_index.py` checks deterministic output,
cross-module definition and references, shadowing, safe rename, typed
completion, and document symbols against a checked-in workspace.
`python3 tools/scripts/test_ai_tooling.py` additionally checks deterministic
callable and field contract serialization, including `cede`, H/P permissions,
and async effects.
