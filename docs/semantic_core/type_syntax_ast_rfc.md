# RFC: Structured `TypeSyntax` AST

**Status:** Implemented (P1).

**Scope:** Parser, source AST, Sema source substitutions, and TKI export.
CodeGen continues to consume the established semantic `Type` / canonical-text
boundary; lowering that boundary is a separate P2 project.

## 1. Decision

Source-level type spellings are represented by immutable shared
`TypeSyntaxPtr` nodes. Every parser-owned type position keeps both the syntax
tree and its canonical text. The text is always derived with
`TypeSyntax::toCanonicalString()`; parser code must not reconstruct it by
concatenating tokens.

`TypeSyntaxPtr` is `shared_ptr<const TypeSyntax>`. Trees may therefore be
shared by cloned AST nodes without exposing mutation through the public AST.
Source locations are retained on every syntax node and on each type or
constant argument.

## 2. Node invariant

The P1 tree covers the current syntax only:

- named types, including `Self`;
- generic applications with distinct type and constant arguments;
- arrays, slices, tuples, and anonymous records;
- `fn` and `dyn fn` type forms;
- `dyn @Trait` objects;
- `T@Trait::Item` associated-type projections;
- morphology (`nul`, `&`, `*`, `^`, `~`, `#`, `cede`) and invalid recovery.

Trait facets and `where` constraints remain specialised grammar. In
particular, `dyn @Trait<...>` is a trait reference, not a generic application
of the enclosing `dyn` node. `ImplHeaderSyntax` likewise owns a `TypeSyntax`
subject and a separately parsed trait facet.

Lowercase `self` is not a type node. It is rejected by the ordinary type-start
rule; no return-signature-specific exception exists. `Self` remains a named
type node.

## 3. Canonical text baseline

Canonical text deliberately preserves the pre-P1 TKI spelling:

```text
Buffer<i32,N_>
[Buffer<i32,N_>;4]
fn(i32,[Self])->cede Result
dyn @Readable<i32>
Buffer<i32,N_>@Readable<i32>::Item
```

There is no TKI format revision and no dual reader. Source-to-TKI and
source-less TKI replay must preserve both this text and the pre-existing
semantic result.

`tests/TypeSyntaxCanonicalization.cpp` is the direct printer matrix for all
node categories, nested morphology, const arguments, recovery, substitution,
and source-range retention. The TKI cache and source-less semantic replay
suites then validate the parser/exporter integration.

## 4. Recovery and semantic boundary

`parseRequiredTypeSyntax()` preserves existing declaration diagnostics. A
nested malformed type becomes `InvalidTypeSyntax`; it does not consume the
delimiter owned by the surrounding grammar. This keeps parser recovery local
and prevents diagnostic cascades.

Sema substitutes source-derived types structurally. If a substitution result
already originates from the legacy semantic `Type` layer, it is represented as
a generated named leaf; the source syntax around it is still traversed
structurally. At the existing semantic and CodeGen boundary, canonical text is
consumed unchanged.

## 5. Non-goals and P2 marker

This RFC adds no tuple semantics, multi-facet `dyn`, associated-type bindings,
or new morphology syntax. It does not change nominal typing, permissions, ABI,
or `.tki` syntax.

P2 may replace the remaining semantic `Type::fromString()` / CodeGen text
boundary with structured lowering. It must retain the canonical text contract
documented here until a separately approved interface-format change.
