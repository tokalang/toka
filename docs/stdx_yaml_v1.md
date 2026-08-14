# Toka Standard Library - YAML Reader (`stdx/serde/yaml`)

`stdx/serde/yaml` is a pure Toka, spec-faithful YAML 1.2 Core Schema reader sub-module. It provides zero-dependency YAML parsing into a standard `JsonNode` AST, fully aligned with ecosystem conventions in `stdx/serde/json` and `stdx/serde/toml`.

---

## 1. API Contract & Capabilities

- **Unified API Signature**:
  - `pub fn parse(input: str) -> Result<JsonNode, YamlError>`
  - `YamlDocument::parse(input: str) -> Result<JsonNode, YamlError>`
- **Structured Error Diagnostic**:
  - `YamlError`: Contains `message: string`, `line: usize`, and `column: usize` (1-indexed).
- **Core Schema Value Mapping**:
  - Null: `null`, `Null`, `NULL`, `~` $\rightarrow$ `JsonNode::NullNode(0)`.
  - Boolean: `true`, `false`, `True`, `False` $\rightarrow$ `JsonNode::BoolNode(bool)`.
  - Number: Decimal (`123`, `-456`), Hexadecimal (`0x1A`), Float (`3.14159`), Exponent (`1e3`), Infinities (`.inf`, `-.inf`), NaN (`.nan`) $\rightarrow$ `JsonNode::NumNode(f64)`.
  - String: Quoted (`"..."`, `'...'`) and unquoted scalars $\rightarrow$ `JsonNode::StrNode(string)`.
  - Array: Block sequences (`- item`) and nested flow sequences (`[1, [2, [3]]]`) $\rightarrow$ `JsonNode::ArrayNode(Vec<JsonNode>)`.
  - Object: Block mappings (`key: val`), flow mappings (`{a: 1, b: 2}`), and flow shorthand null values (`{a}` $\rightarrow$ `a: null`) $\rightarrow$ `JsonNode::ObjectNode(HashMap<string, JsonNode>)`.

---

## 2. Escape Sequences & Unicode Support

- **Double-Quoted Escape Sequences**:
  - `\n` $\rightarrow$ newline
  - `\r` $\rightarrow$ carriage return
  - `\t` $\rightarrow$ tab
  - `\"` $\rightarrow$ double quote
  - `\\` $\rightarrow$ backslash
  - `\uXXXX` $\rightarrow$ 4-hex Unicode escape sequence with multi-byte UTF-8 encoding (e.g. `\u00e9` $\rightarrow$ `é`).
- **Single-Quoted Escapes**:
  - `''` $\rightarrow$ `'`.

---

## 3. Strict Rejection Rules with Line & Column Diagnostics

1. **Tab Indentation**: Tabs used for block indentation before non-space characters are rejected with line/column diagnostics (`tab indent is not allowed at line X, column Y`).
2. **Multi-Document Streams**: Multi-document streams (`---`) are rejected (`multi-document YAML is not supported`).
3. **Duplicate Keys**: Duplicate mapping keys within block or flow mapping scopes are rejected (`duplicate key 'X'`).
4. **Merge Keys (`<<`)**: Merge keys are rejected across block, flow, and nested scopes (`merge key '<<' is not supported`).
5. **Anchors & Aliases (`&`, `*`)**: Rejected (`anchors and aliases are not supported`).
6. **Tags (`!`)**: Rejected (`tags are not supported`).
7. **Explicit Keys (`?`)**: Rejected (`explicit key '?' is not supported`).

---

## 4. Example Usage

```toka
import std/io::{println}
import stdx/serde/json::{JsonNode}
import stdx/serde/yaml::{YamlDocument, YamlError}

fn main() -> i32 {
    auto yaml_text = "
name: \"toka-service\"
version: 1.0.0
unicode: \"\\u00e9\"
features: [fast, pure, [static, safe]]
"
    auto parsed = YamlDocument::parse(yaml_text)
    if parsed.is_err() {
        auto err = parsed.unwrap_err()
        println("YAML Error: {} at line {}, col {}", err.message().as_str(), err.line(), err.column())
        return 1
    }

    auto root = parsed.unwrap()
    match root {
        auto JsonNode::ObjectNode(&map) => {
            auto &name_node = map.get_ref(string::from("name")).unwrap()
            match name_node {
                auto JsonNode::StrNode(&s) => println("App: {}", s.as_str()),
                _ => {}
            }
        }
        _ => {}
    }
    return 0
}
```
