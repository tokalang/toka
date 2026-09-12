# YAML reader: owned flat documents

`stdx/serde/yaml` reads the existing supported YAML Core Schema subset
directly into an owned `YamlDocument`. It does not import JsonNode, build a
recursive owning tree, or serialize YAML through JSON text.

## API

- `parse(input: str) -> Result<YamlDocument, YamlError>`
- `YamlDocument::parse(input)` provides the same operation.
- `YamlError` keeps its owned message and one-based line/column diagnostics;
  `message()` returns an owned string copy, with `line()`, `column()` and `to_string()`.

The document owns a text buffer and a flat array of primitive node records.
Text/keys are copied into owned storage; input may be destroyed after parsing.
Builder scratch lines and partial documents are cleaned on failure.

Node IDs are **one-based and document-relative**. Zero means absent/end/invalid.
Use `document.root()`; unlike the JSON document, the YAML root is not necessarily 1.
The old cloning `root(): JsonNode` and recursive variant API are removed.

| Reader | Meaning |
| --- | --- |
| `valid(id)` | ID is in this document's node range |
| `kind(id)` | null=0, bool=1, number=2, string=3, sequence=4, mapping=5; invalid=-1 |
| `first(id)`, `next(id)` | child/sibling ID, or 0 |
| `len(id)` | collection size or string byte length; 0 for other/invalid nodes |
| `find(mapping, key)` | matching value ID, or 0; duplicates are rejected by parsing |
| `text(id)`, `key(id)` | checked text/key view borrowing the document |
| `number(id)`, `boolean(id)` | type-checked `Option<f64>` / `Option<bool>` |

All public text slicing checks offset and length before accessing the buffer.
Do not keep a view after its document dies. IDs are not globally unique.
Mapping lookup is linear. No extra ownership, lifetime or storage-proof
mechanism is required from the compiler.

## Preserved scalar and syntax behavior

- Null spellings: null/Null/NULL and `~`.
- True/false case variants; other plain words such as `yes` remain strings.
- Decimal/hex numbers, floating/exponent forms and the existing f64 rounding behavior.
- Positive/negative infinity and NaN remain **numeric IEEE values**, not strings
  or errors. They are not subjected to JSON finite-number restrictions.
- Single/double quoted strings, the existing escape/Unicode behavior, multiline
  quoted/plain folding, literal/folded block scalars and chomping modes.
- Block/flow sequences and mappings, nested collections, shorthand null values.
- Existing diagnostic/rejection paths for tab indentation, duplicate and merge
  keys, anchors/aliases, tags, explicit keys, directives, multiple documents,
  malformed flow delimiters and invalid continuations are retained.

The existing block-recursion limit is 128. Flow recursion now has an explicit
128-level cap as well; the former unbounded flow recursion is intentionally
closed. A limit failure uses the existing maximum-depth error message.
This is a supported subset, not a claim of full YAML specification coverage.

## Example

```toka
import stdx/serde/yaml::{YamlDocument}
import std/io::{println}

fn main() -> i32 {
    auto parsed = YamlDocument::parse("name: toka\nlimits: [.inf, -.inf, .nan]")
    if parsed.is_err() {
        auto error = parsed.unwrap_err()
        println("{} at {}:{}", error.message().as_str(), error.line(), error.column())
        return 1
    }
    auto document = parsed.unwrap()
    auto name = document.find(document.root(), "name")
    println("App: {}", document.text(name))
    return 0
}
```

Validation is in `tests/pass/g17_stdx_yaml_test.tk` and
`tools/scripts/test_yaml_document.py`: original cases, full tree contents,
nonfinite values, rejection locations, ownership/escape and cleanup stress.
