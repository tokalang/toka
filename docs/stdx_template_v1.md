# Toka Standard Extension Library - Restricted Go `text/template` Subset Engine (`stdx/text/template`)

`stdx/text/template` is a pure Toka, zero-dependency restricted Go `text/template` subset rendering engine. It enables controlled template preprocessing for configuration files (such as YAML/JSON hooks in webhooks) with a secure, sandboxed execution model.

---

## 1. Core API & Usage

- **Parse & Render**:
  - `Template::parse(input: str) -> Result<Template, TemplateError>`
  - `template.render(context: TemplateContext) -> Result<string, TemplateError>`

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
        println("Template parse error: {} at line {}, col {}", err.message().as_str(), err.line(), err.column())
        return 1
    }

    auto ctx# = TemplateContext::new()
    ctx#.set(cede string::from("hook_id"), cede string::from("deploy-service"))
    ctx#.set(cede string::from("is_prod"), cede string::from("true"))

    auto args_list# = Vec<string>::new()
    args_list#.push(cede string::from("--verbose"))
    args_list#.push(cede string::from("--timeout=30"))
    ctx#.set_list(cede string::from("args"), cede args_list)

    auto rendered = parsed.unwrap().render(ctx).unwrap()
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
6. **Unary Function Call Injection**: Action and pipeline function handlers are defined as single-argument string transformations (`fn(str) -> Result<string, string>`).
7. **Comments**: `{{/* comment text */}}`

---

## 3. Sandboxed Security & Function Injection Boundary

- **Pure Computation Sandbox**: The core `stdx/text/template` engine does NOT embed direct filesystem or environment I/O functions (such as `cat`, `getenv`, or `credential`).
- **Function Injection**: Applications (such as Webhook with `--template`) pass allowed variables or register external function helpers explicitly via `TemplateContext::register_func(name: string, handler: fn(str) -> Result<string, string>)`.
- **Parser Structure Checks**: `Template::parse` performs strict tag balance checking (ensuring every `if` and `range` block has a matching `end` tag and rejecting dangling `else`/`end` tags). Rendering unknown functions or failing registered function calls produces a descriptive `TemplateError`.
