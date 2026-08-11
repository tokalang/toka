# AI Authoring Friction Baseline v1

This is a small executable baseline for source forms that AI-assisted Toka
authors repeatedly confuse. It is a diagnostic and documentation boundary,
not a language-design proposal and not an AI-model benchmark.

The checked corpus is
[`tests/tooling/ai_authoring_friction.v1.json`](../tests/tooling/ai_authoring_friction.v1.json).
Each fixture must reject through one `toka.diagnostics` v2 object and carries
two semantically distinct repair directions. Run it after building:

```sh
python3 tools/scripts/test_ai_authoring_friction.py --build-dir build
```

The four v1 dimensions are:

| Dimension | Stable rule |
| --- | --- |
| Declaration vs assignment | An assignment cannot widen the authority of an existing binding. |
| Handle vs payload | Handle rebinding and payload writing are independent H/P permissions. |
| Call-site request vs declaration | A `#` request at a call site cannot grant authority absent from the argument declaration. |
| `Option` borrowed pattern | A binding matched from `Option<&T>` remains a read-only borrowed view; pattern `#` cannot amplify it. |

The deliberately different repair directions matter: changing an API
declaration, passing an appropriately authorized view, and preserving a
read-only operation are not interchangeable automated edits. Tools should
inspect the declaration contract, then use `toka check --json` to validate the
chosen direction.

## Scope and follow-up

This baseline freezes recurring user-visible mistakes without adding grammar,
PAL rules, CodeGen behavior, TKI fields, or a new public semantic schema. It
does **not** claim that the current diagnostics fully explain every H/P or
pattern capability fact.

After RC3, the same four cases are the seed corpus for a measured authoring
study: fix a model revision, prompt/context, tool budget, and tasks; record
repair rounds before and after structured explanation improvements. Only if
those results justify it should Toka consider a separate RFC for new capability
facts or surface syntax.
