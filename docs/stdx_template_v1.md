# Toka Standard Extension Library - Restricted Go `text/template` Subset Engine (`stdx/text/template`)

`stdx/text/template` is a pure Toka, zero-dependency restricted Go `text/template` subset rendering engine. It enables controlled template preprocessing for configuration files (such as YAML/JSON hooks in webhooks) with a secure, sandboxed execution model.

---

## 1. Core API & Usage

- **Parse & Render**:
  - `Template::parse(input: str) -> Result<Template, TemplateError>`
  - `template.render(context#: TemplateContext) -> Result<string, TemplateError>`
  - `template.render_with(context#: TemplateContext, dispatcher: fn(str, str) -> Result<string, string>) -> Result<string, TemplateError>`

### Example

```toka
import std/io::{println}
import std/vec::{Vec}
import stdx/text/template::{Template, TemplateContext}

fn main() -> i32 {
    auto tpl_source = "
id: {{ .hook_id }}
command: {{ if .is_prod }}/deploy/prod.sh{{ else }}/deploy/dev.sh{{ end }}
args: {{ range .args }}[{{ . }}] {{ end }}
"
    auto parsed = Template::parse(tpl_source)
    if parsed.is_err() {
        auto err = parsed.unwrap_err()
        println("Template parse error: {} at line {}, col {}", err.message.as_str(), err.line, err.column)
        return 1
    }

    auto ctx# = TemplateContext::new()
    ctx#.set(string::from("hook_id"), string::from("deploy-service"))
    ctx#.set(string::from("is_prod"), string::from("true"))

    auto args_list# = Vec<string>::new()
    args_list#.push(string::from("--verbose"))
    args_list#.push(string::from("--timeout=30"))
    ctx#.set_list(string::from("args"), cede args_list)

    auto rendered = parsed.unwrap().render(ctx#).unwrap()
    println("Rendered Output:\n{}", rendered.as_str())
    return 0
}
```

---

## 2. Supported Actions & Syntaxes

1. **Variables & Field Dereferences**: `{{ .name }}`, `{{ .user.role }}`
2. **Current Item Reference**: `{{ . }}` inside range loops
3. **Conditionals**: `{{ if .condition }} ... {{ else }} ... {{ end }}`
4. **Range Loops**: `{{ range .items }} ... {{ end }}`
5. **Pipelines**: `{{ .value | lower }}`, `{{ .value | upper }}`, `{{ .value | trim }}`
6. **Unary Function Call Injection**: Each action/pipeline has one string argument. A render-time dispatcher receives its function name and argument (`fn(str, str) -> Result<string, string>`).
7. **Comments**: `{{/* comment text */}}`

---

## 3. Sandboxed Security & Function Injection Boundary

- **Pure Computation Sandbox**: The core `stdx/text/template` engine does NOT embed direct filesystem or environment I/O functions (such as `cat`, `getenv`, or `credential`).
- **Function Injection**: Applications enable owned names with `context#.enable_func(string::from("echo"))`, then call `render_with(context#, dispatcher)`. This replaces the removed `register_func` API. The dispatcher is passed for this render, never stored in the context. Its first argument is the enabled function name; the second is the action/pipeline argument. A callback may explicitly perform application I/O, but the library grants none on its own.
- **Missing Dispatcher**: `render(context#)` supports builtins; encountering an enabled custom function returns `TemplateError`, not success. Enabling a name alone does not provide an implementation. `render_with` still rejects names not enabled by the context and propagates callback errors.
- **Parser Structure Checks**: `Template::parse` performs strict tag balance checking (ensuring every `if` and `range` block has a matching `end` tag and rejecting dangling `else`/`end` tags). Rendering unknown functions or failing registered function calls produces a descriptive `TemplateError`.

```toka
import core/result::{Result}
import stdx/text/template::{Template, TemplateContext}

fn invoke_custom(name: str, arg: str) -> Result<string, string> {
    if name.equals("echo") { return Result<string, string>::Ok(string::from(arg)) }
    return Result<string, string>::Err(string::from("unsupported custom function"))
}
fn main() -> i32 {
    auto ctx# = TemplateContext::new()
    ctx#.enable_func(string::from("echo"))
    auto handler = { name, arg => invoke_custom(name, arg) }: fn(str, str) -> Result<string, string>
    auto template = Template::parse("{{ echo \"hello\" }}").unwrap()
    assert(template.render(ctx#).is_err(), "no dispatcher supplied")
    assert(template.render_with(ctx#, handler).unwrap().as_str().equals("hello"), "actual dispatcher")
    return 0
}
```

List storage is flat owned key/value data, with an empty-list marker. Lookup and
replacement scan entries; `set_list` copies input strings and consumes the input
vector. `get_list` returns an independent owned vector. These are intentional
storage/copy costs, not constant-time indexing guarantees.
