# JSON documents and typed construction

Dynamic JSON uses `Document` from `stdx/serde/json` (or `stdx/serde/json_document`).
The recursive `JsonNode` API has been removed; no compatibility wrapper is provided.

```toka
import stdx/serde/json::{parse_document, to_json}

auto result = parse_document("{\"name\":\"Toka\"}")
if result.is_err() { return 1 }
auto document = result.unwrap()
auto name = document.find(1, "name")
auto text = document.text(name) // borrowed from document
auto output = to_json(document)
```

`parse_document(input)` returns `Result<Document, JsonError>` and consumes the
whole JSON text apart from surrounding whitespace. `JsonError.offset` is a byte
cursor at which parsing failed, not a promised exact offending-character span.
The returned document owns decoded strings, object keys, original number lexemes
and its node array. It does not retain the input. Construction failure destroys
the partially built document; no partial document is published.

The root ID is **1**. **0** means missing/end/invalid. IDs are relative to their
document, not globally unique handles. Node and text storage are private.

| Reader | Result |
| --- | --- |
| `valid(id)` | whether the node belongs to the document's ID range |
| `kind(id)` | null=0, bool=1, number=2, string=3, array=4, object=5; invalid=-1 |
| `first(id)`, `next(id)` | first child / next sibling, or 0 |
| `find(object, key)` | matching value ID, or 0; last duplicate key wins |
| `text(id)` | decoded string or exact number lexeme; empty for other/invalid nodes |
| `key(id)` | decoded member key; empty for nonmembers/invalid nodes |
| `number(id)` | `Option<f64>`; conversion may round or overflow, use `text` for exact digits |
| `boolean(id)` | `Option<bool>` |

Reader text ranges are checked using subtraction before slicing. Text views
depend on the document and cannot outlive it. Construction uses only indices
and offsets across buffer growth. Object lookup is currently linear; this is
an intentional library implementation choice, not a compiler contract.

The parser checks JSON number grammar, UTF-8, control bytes, escape spelling,
hex digits and surrogate pairs. Values deeper than 128 recursive edges reject.
Serialization preserves array/member order and numeric lexemes, and escapes
decoded text. Duplicate members remain in the document/serialized output.

## Typed values

Dynamic documents and generic deserialization are separate APIs. `@ToJson` and
reflection serialization remain available. `deserialize_shape<T>` now requires
an explicit `T@JsonFactory` complete-value constructor; it does **not** fabricate
arbitrary `T` through zeroed storage or inferred reflection defaults.

Scalar/Option/Result factories return `Parsed<T>(value, rest)`; `rest` remains a
view into the actual input. Borrowed scalar `str` retains its input-view contract
(including escape text); owning `string` is decoded into independent storage.

`Vec<T>::parse_complete` and `HashMap<K,V>::parse_complete` instead return
`Complete<Container>(value, consumed)` in a `Result`. The numeric byte count
replaces the stored `rest` view. Their `@FromJson` adapters install the complete
value only after success and derive the remainder from the original input.
They no longer claim the blanket container `@JsonFactory` implementation.
Unproved borrowed-element containers remain rejected; this change does not
establish arbitrary generic container lifetime support. Explicit resource
factories and partial-failure cleanup are tested separately from Document.

Consumers of the removed recursive representation, including the current YAML
adapter, need a separate library migration. They are not silently adapted via
new compiler rules or counted as recovered by JSON tests.
