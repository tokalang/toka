# Explicit `cede` Stage 1: ordinary-parameter activation

**Status:** implementation slice; pending acceptance.

**Stage 0 authority baseline:**
`e9f78bb91e70592719e4587b743a875f609ef137`.

This slice activates the caller-spelling rule for ordinary parameters whose
selected formal is declared `cede`:

```toka
fn consume(cede value: i32) {}

auto value = 42
consume(value)       // E04570: named source requires explicit `cede`
consume(cede value)  // accepted; value is unavailable afterward
consume(42)          // accepted; the literal has no source place
```

The rule is independent of `Copy`. Passing a named `Copy` source with `cede`
still invalidates that source. Passing the same source to an ordinary formal
uses `CopyValue + KeepLive`. A complete, independently owned temporary with no
source place remains eligible for bare transfer; the compiler transfers its
cleanup liability without inventing user syntax.

Direct functions, generic direct functions, extern declarations, and static
functions are in this slice. Instance-method, dynamic-trait, indirect-call,
callable-expression, and receiver activation remain outside this slice.

Multi-argument calls remain atomic. If any named source is missing `cede`, the
whole call is rejected before any argument source is invalidated. The frozen
Stage 0 transaction records this as `MissingCedeForNamedSource`,
`NoStateChange`, and `commit_allowed=false`.

Generic deduction and specialization validation are part of the same argument
transaction. A deduction conflict, invalid specialization, or incomplete body
qualification restores the pre-call `AnalysisState`; rejected calls cannot
leave an explicit argument moved or uninitialized.
The same rollback applies to compiler-generated `TemplateOrigin` instances:
an `Invalid`/`Unchecked` mangled cache hit, or an incomplete required body
qualification, rejects independently of whether audit evidence is enabled.

Transparent evaluation wrappers do not erase source identity. For example,
`consume(unsafe value)` still names `value` and therefore requires explicit
`cede`; `consume(cede unsafe value)` performs the destructive read.

`E04570` carries a machine-applicable insertion of `cede ` at the exact actual
expression, preserving any payload or handle spelling that follows it.

The source migration follows the same rule. Single-use named values are passed
with `cede`; retry loops construct a fresh `NoSourcePlace` address temporary
for each attempt. Public forwarding functions declare their address parameter
`cede` when they transfer that exact parameter onward.

The testing-only `--stage1-legacy-ordinary-cede` option, or
`TOKA_STAGE1_LEGACY_REPLAY=1`, preserves the historical implicit-transfer
behavior only for frozen Stage 0 evidence tests. It is not a source-language
compatibility mode and is not documented as a user-facing compiler option.

This slice does not change Parser syntax, TKI, ABI, the compiler-interface key,
return semantics, receiver semantics, callable-expression syntax, or the
frozen Stage 0 evidence schemas. It also does not remove the legacy lowering
path; that cleanup remains Stage 3 work after behavior qualification.
