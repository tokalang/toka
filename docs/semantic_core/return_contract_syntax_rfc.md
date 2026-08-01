# RFC: Structured Return Contracts

**Status:** Implemented (P2a).

**Scope:** Function and extern declaration parsing, source AST, TKI export,
and the legacy Sema/CodeGen compatibility cache. This RFC changes no source or
TKI syntax.

## 1. Decision

Each function and extern declaration owns one `ReturnContractSyntax`:

```text
ReturnContractSyntax = {
  HasArrow, TypeSyntax, canonical Type,
  BindingName, BindingPrefix,
  Effect,
  Routes: [ReturnDependencyRouteSyntax]
}
```

Each route has a structured target (`return` or the declared binding, with an
optional member) and structured source paths (root, members, reference marker,
and source ranges). The parser does not represent dependencies as text while
parsing.

## 2. Unified route parsing

Inline dependency syntax and `effects:` now share one route parser:

```toka
fn view(data: Data) -> &i32 <- data

fn split(left: Data, right: Data) -> result: (left: &i32, right: &i32)
effects:
    result.left <- left.value
    result.right <- right.value
```

The inline form supplies an implicit `return` target. The `effects:` form
parses an explicit target, including the named-return binding when present.
Both forms then use identical source-path and route construction logic.

## 3. Compatibility invariant

`FunctionDecl::{ReturnType, ReturnTypeSyntax, Effect, LifeDependencies,
MemberDependencies}` remain derived caches for existing Sema and CodeGen
consumers. They are populated from `ReturnContractSyntax` at parse time and
kept in sync when generic or associated-type substitution changes the return
type.

TKI export consumes the structured contract and deliberately retains its
existing canonical presentation: a named return target is emitted as `return`
in an `effects:` block, as it was before P2a. No `.tki` version change or
dual-format reader is introduced.

## 4. Validation

`return_contract_001_structured` performs source-to-TKI-to-source-less replay
for inline dependencies, named bindings, and member routes. Existing effect,
TKI cache, and semantic replay suites remain the compatibility gate.
