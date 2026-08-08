# RFC: Expression Uniqueness

**Status:** EU-01 design frozen; EU-02 through EU-05 implemented. EU-06 is
frozen for a later, separate implementation.

**Scope:** This RFC records how Toka applies the principle that one semantic
intent has one semantic core. It permits a concise spelling and a complete
spelling only when they have an explicit containment relation, scope rule, or
canonical presentation. It does not by itself change the implemented language
or the public syntax guide.

## EU-00: How surface pairs are evaluated

**Decision: frozen.**

Equal lowered ASTs alone do not prove that two source forms are an accidental
duplicate. A pair may remain when one form is a structurally contained concise
form of the other, or when the forms deliberately choose different scopes.
This RFC therefore does not treat expression uniqueness as a mechanical
"one AST, one spelling" rule.

The following existing concise/complete pairs are retained for a later,
separate decision; this RFC neither removes nor canonically rewrites them:

- a single inline return-dependency route (`<-`) and a multi-route
  `effects:` block;
- direct import selection (`import path::item`) and braced selection
  (`import path::{item, other}`); and
- an inline generic bound and a `where:` block.

Each later decision must state the containment or scope relation that justifies
retaining both forms, or select one source spelling. It must not infer a change
solely from their current shared lowering path.

## EU-01: Pattern binding provenance

**Decision: frozen.**

`auto` is the explicit introducer for a fresh pattern binding. It may prefix a
whole pattern or an individual nested sub-pattern.

### Keyword rationale: `auto`, not `let`

**Decision: frozen.**

Toka uses `auto` as the single explicit marker that a source form introduces
a fresh binding. This covers initialized declarations, guard bindings,
iteration bindings, and whole or nested match patterns:

```toka
auto value = compute()
guard auto Option<i32>::Some(value) = result else { return 0 }
for auto item in values { use(item) }
match result {
    auto Result<i32, string>::Ok(value) => { pass value }
}
```

This is a surface-language decision, not a claim that `auto` is more powerful
or more rigorous than `let`. Either spelling can denote the same binding and
type rules. `let` was considered because it is familiar to users and code
generators as a binding keyword, but it is not the canonical Toka spelling for
the following reasons:

- `auto` reads as a modifier of a pattern as well as a declaration. In
  `auto Result::Ok(value) => ...`, it marks the identifiers admitted by the
  pattern as fresh without implying that an initializer must follow.
- `let` carries the common declaration-shaped reading `let Pattern = value`.
  Consequently, `let Result::Ok(value) => ...` can visually suggest an
  unfinished declaration: the reader may expect `=` and an initializer before
  the arm body. Toka's explicit fresh-pattern marker must work naturally in
  this position.
- Ordinary Toka `auto` declarations require an initializer. Keeping the same
  word avoids suggesting that `auto value: T` is an uninitialized local
  declaration. A value whose initialization state matters must use an explicit
  representation such as `uninit:T`; it is not made implicit by a bare binding
  declaration.
- Familiarity alone is not a sufficient reason to replace a coherent keyword.
  Tooling and AI code generators can learn the canonical form from the syntax
  guide, formatter output, examples, and diagnostics. Toka therefore gives
  priority to one spelling whose role is consistent in declarations and
  patterns.

This choice does not prohibit future changes to pattern syntax itself. If a
future design removes the need for an explicit fresh-pattern marker, that
design may evaluate its own keyword independently. Until then, `auto` remains
the required spelling; `let` and `var` are rejected rather than accepted as
aliases.

```toka
match result {
    // `value` is a fresh binding: whole-pattern shorthand.
    auto Result::Ok(value) => { pass value }

    // `fresh` is new; `expected` is an existing pattern reference.
    Result::Pair(auto fresh, expected) => { pass fresh }

    // All three names are fresh: tuple destructuring stays concise.
    auto (left, value, right) => { pass value }

    // `expected` is an existing pattern reference, not a new declaration.
    Result::Ok(expected) => { pass expected }
}
```

The two placements are one concise/complete family, not two unrelated binding
mechanisms:

- `auto Pattern` recursively introduces every otherwise-unmarked,
  binding-eligible identifier leaf in `Pattern`. It is the concise form for a
  destructure whose leaves are all fresh.
- `auto name` introduces only that nested leaf. It is the complete form and is
  required for a mixed pattern containing both fresh names and existing
  references.
- An identifier leaf not covered by `auto` is a reference to an already
  resolvable variable. It does **not** silently introduce a variable. An
  unresolved such name must produce a diagnostic that suggests `auto name`
  when a fresh binding was intended.

Consequently, `Result::Pair(auto fresh, expected)` can express a fresh
extraction constrained by a value already in scope without an auxiliary
destructure or an implicit name-meaning heuristic.

An existing-reference leaf is evaluated only after every enclosing constructor,
variant tag, and literal/structural condition has matched. It then constrains
the matched value with ordinary Toka equality:

```toka
// First match `Result::Ok`; then test `payload == expected`.
Result::Ok(expected)
```

The comparison direction is `matchedValue == existingValue`. It uses the same
built-in and `eq`-overload semantics as source-level `==`; it neither creates
a new binding nor substitutes object identity for value equality. A type that
cannot participate in ordinary `==` is not a valid existing-reference pattern.

`auto` affects only binding provenance. A constructor head such as
`Result::Ok` or `Result::Pair` must retain ordinary pattern-constructor name
resolution; `auto` never causes an identifier such as `Some` to be guessed as
an enum variant from the scrutinee type.

The implementation may reject a nested `auto` already covered by an enclosing
`auto Pattern` as redundant, but it must never give that marker a different
binding meaning.

### Non-goals retained for later decisions

EU-01 deliberately does not decide:

- whether enum pattern constructors must always use a qualified
  `Type::Variant` path or may be imported into a pattern namespace;
- wildcard spelling, import shorthand, return-dependency shorthand, or any
  other expression-uniqueness issue.

Those choices must not weaken the frozen distinction: `auto` means fresh
binding; its absence means an existing reference whenever an identifier leaf
is present.

## EU-02: `auto` binding types originate at the initializer

**Decision: frozen.**

`auto` introduces an initialized binding whose type is inferred from its
right-hand-side expression. An `auto` binding must therefore have an
initializer and must not carry a declaration-side type annotation.

```toka
auto count = 0:usize
auto port = raw as u16
auto answer = todo:i32
auto add = { a, b => a + b }: fn(i32, i32) -> i32

// Rejected: an `auto` binding never supplies a type from its left side.
auto count: usize = 0
auto missing: auto = 0
auto uninitialized
```

The two RHS forms have distinct roles:

- `expression: Type` is a type ascription. It establishes `Type` at the
  expression site and supplies that complete expected type recursively to the
  expression. It is the spelling for literal width, `todo`, nullable absence,
  empty containers, generic construction, control-flow expressions, and an
  unannotated closure's callable signature.
- `expression as Type` is an explicit conversion. It is required whenever the
  program requests an actual permitted conversion rather than merely
  constraining an expression's type.

The inferred type of an `auto` binding remains its fixed type for subsequent
assignments. Binding-side handle and permission markers (`&`, `^`, `~`, `#`,
and `nul`) are not declaration-side type annotations and remain where they
are needed to describe the binding's authority.

`uninit` remains the explicit initializer for a local slot that will be
initialized later:

```toka
auto value# = uninit:i32
```

The ascription supplies the slot type; `#` remains a binding-side permission
fact. This is not an exception to the RHS rule.

### Migration and implementation invariant

The implementation must not merely move text from `auto name: T = expression`
to `auto name = expression:T`. A RHS ascription must provide every complete
expected-type flow that the removed declaration type previously provided,
including typed `todo`, nullable and empty aggregate forms, generic
construction, control-flow expressions, and closure parameter/return
injection. An ascribed `uninit` must retain `uninit`'s zero initialization-mask
state and CodeGen's no-store behavior.

Declaration-side annotations remain necessary where no initializer exists by
definition, such as parameters, fields, return contracts, and type
declarations. If a future feature genuinely requires a typed local storage
slot before its initializer (for example a recursive local binding), it must
use a dedicated non-`auto` declaration form proposed separately; it must not
reopen `auto name: Type`.

## EU-03: Ascription and conversion remain distinct in the source AST

**Decision: frozen.**

The semantic distinction in EU-02 must survive parsing, semantic analysis,
TKI export, and source-less replay. `expression: Type` carries an
`Ascription` source kind; `expression as Type` carries a `Conversion` source
kind. An implementation may use distinct AST node classes or one immutable
node with distinct source kinds, but it must not reduce both forms to a
source-identical cast. A serializer must not rewrite an ascription as `as`.

Compiler-inserted coercions are neither user-written ascriptions nor explicit
conversions. They must be represented as an internal semantic operation or
otherwise remain distinguishable from both source forms. This preserves useful
diagnostics, permits the expected-type flow required by EU-02, and keeps TKI
replay from changing a program's declared intent.

## EU-04: Bitwise operations use symbols

**Decision: frozen.**

Toka has one spelling for each bitwise operation:

| Operation | Spelling |
| :--- | :--- |
| AND | `&` |
| OR | `|` |
| XOR | `^` |
| complement | `~` |
| left shift | `<<` |
| right shift | `>>` |

The word aliases `band`, `bor`, `bxor`, `bnot`, `bshl`, and `bshr` are removed,
not retained as compatibility syntax. Binary bitwise operators must be
surrounded by spaces. That lexical boundary distinguishes them from adjacent
handle morphology; `~` remains a prefix form whose operand type distinguishes
integer complement from a shared-handle operation.

## EU-05: Match has one wildcard and no `case` introducer

**Decision: frozen.**

`_` is Toka's sole match wildcard. The former `default` wildcard is removed
and must diagnose with a direct suggestion to use `_`; it has no wildcard
meaning. `case` is not a match-arm introducer and is removed from the reserved
keyword set, so it is again available as an ordinary identifier.

## EU-06: Function result contracts distinguish Unit, ABI `void`, and `never`

**Decision: frozen.**

An ordinary completed action has the Unit result `()` and spells that function
contract by omitting `->`. An ordinary declaration must not additionally use
`-> ()`. ABI no-value is explicitly `-> void` at the `extern` boundary, and
non-completion is explicitly `-> never` for a synchronous ordinary function.
They are three semantic categories, not three spellings for the same result.

The detailed type and implementation-separation requirements are frozen in
`docs/semantic_core/unit_void_never_rfc.md`. The initial `never` surface is
intentionally restricted to synchronous ordinary return contracts; async,
FFI, and arbitrary nested-type uses require separate RFC decisions.

## Implementation acceptance condition

When EU-01 is implemented, parser, AST, semantic analysis, diagnostics,
source-less TKI replay, and the syntax guide must agree on the same frozen
source fact. The implemented EU-02 path does not accept or discard `auto`
merely as optional decoration, and `auto` declaration types do not survive as
an alternate expected-type path.
