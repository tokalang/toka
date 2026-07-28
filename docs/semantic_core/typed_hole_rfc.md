# Typed Hole v1 RFC

**Status:** adopted post-1.0 language/tooling extension. Phase 1 implements
the reserved `hole` token, distinct AST identity, fail-closed diagnostics, and
the no-emission boundary. Phase 2 passes already-resolved contracts from local
declarations, assignments, boolean conditions, normal calls, and explicit
generic calls. Phase 3 exposes those requirements through the independent
`hole-goals` JSON protocol. Phase 4 verifies that semantic rejection prevents
object/interface output and rejects transfer requirements. Phase 5 implements
the separately versioned, conservative conditional fact slice for bindings,
non-transfer expressions, resolved calls, and `if` value joins; broader
control-flow propagation remains subsequent work.

## Decision

Toka introduces `hole` as an expression-only, reserved keyword for an
incomplete program fragment. It is an editor and AI construction aid, not a
value, a capability source, a resource, or a way to make an incomplete program
compile or publish.

The central invariant is:

```text
hole consumes a complete requirement; it never creates type, provenance,
ownership, H/P authority, or a normal compiler Allow decision.
```

This extension intentionally does not introduce user-written lifetime
annotations, an `any` type, implicit resource transfer, or a permissive build
mode.

## Syntax

```ebnf
PrimaryExpr ::= ... | HoleExpr
HoleExpr    ::= "hole"
```

`hole` is a lexer keyword and an independent `HoleExpr` AST node. It must not
first parse as `VariableExpr("hole")`. Parentheses are transparent:

```toka
auto answer: i32 = hole
auto same: i32 = (hole)
```

The keyword is reserved in every user identifier namespace. A repository audit
found no `.tk`, `.tki`, or `.tke` use of `hole`; reserving it before the 1.0
surface is finalized has a low compatibility cost.

The following spellings are deliberately excluded from v1:

```toka
hole()
hole<T>
hole("reason")
fn hole() {}
shape hole(...)
match value { hole => {} }
```

`_` remains a pattern wildcard/ignore marker. `?` and `??` retain their
nullable and nullable-assertion meanings. In particular, neither is a typed
hole spelling.

## Expected-contract checking

Each syntactic occurrence has a distinct `HoleId`. The checker has two modes:

```text
Γ ⊢ hole ⇐ Requirement
  ↝ Incomplete(HoleId, Requirement)

Γ ⊢ hole ⇒ ?
  ↝ Underconstrained(HoleId)
```

A `Requirement` is already fully determined before `hole` is checked. It may
contain the target type, morphology, transfer mode, H/P permissions,
nullability, effect-related return information, and required return
provenance/dependencies.

The hole may consume such a requirement in these v1 contexts:

```toka
auto value: i32 = hole       // declared target contract
value = hole                 // existing LHS contract
return hole                  // declared return contract
send(hole)                   // only after callee and parameter are unique
Point(x = hole, y = 1)       // declared field contract
if hole { ... }              // bool requirement
loop hole { ... }            // bool requirement
identity<i32>(hole)          // explicit generic argument fixes the target
```

`hole` does not infer, choose, or disambiguate anything. These forms are
underconstrained or unsupported in v1:

```toka
auto value = hole
generic(hole)                // T would be inferred from the hole
overloaded(hole)             // argument would choose a candidate
hole + 1
hole as i32
match hole { ... }
for item in hole { ... }
```

The implementation must pass `bool` explicitly as the expected type when
checking `if` and loop conditions. It must not accidentally reuse an enclosing
result expression's expected type.

## No place, resource, or authority

`HoleExpr` has the following fixed properties:

```text
place(hole)            = false
resource(hole)         = false
provenance(hole)       = none
const_value(hole)      = none
AccessCapability(hole) = ∅
AccessIntent(hole)     = ∅
PermissionFlow(hole)   = RequirementOnly
isLValue(hole)         = false
```

`RequirementOnly` is a distinct `PermissionFlowKind`; it must not fall through
the existing default `Fresh` rvalue path. In particular, a hole may not be
classified as a fresh owned value, an independent unique source, or a writable
payload capability merely because its expected type is known.

Consequently v1 rejects all place-, resource-, provenance-, or
constant-dependent operations:

```toka
&hole
^hole
*hole
hole#
hole?
hole??
hole!
hole.field
hole[index]
hole.method()
hole = value
cede hole
guard hole
```

`guard hole` is excluded even when a boolean condition might seem useful:
Toka guards establish narrowing facts for a traceable nullable path, and a hole
has no path or provenance to narrow.

## Calls, transfer, and returns

For an ordinary resolved parameter, `hole` records the complete parameter
contract as a requirement. It does not mean the parameter has been supplied.

```toka
fn read(value: Data) {}
read(hole)
```

For a `cede` parameter, the hole is rejected rather than treated as an implicit
transfer:

```toka
fn consume(cede value: Resource) {}
consume(hole) // unsupported: requires a real path and explicit transfer
```

This rule is intentionally stricter than ordinary expected-type holes. `cede`
is an observable source invalidation and resource-cleanup operation; a hole
cannot stand for either. `cede hole` is equally invalid.

A borrowing return may describe required provenance without creating it:

```toka
fn borrow(x: Data) -> &Data <- x {
    return hole
}
```

The hole goal may say that an `&Data` derived from `x` is required, but it must
not update ordinary life-dependency state, emit an Allow evidence record, or
pretend the dependency was established. A consuming/resource return is an
unsupported hole context in v1.

## Conditional facts

Typed holes are useful only if surrounding analysis can continue. A declared
binding initialized from a contextual hole may therefore become a temporary
conditional symbol:

```text
x : Data
depends_on = [H0]
complete = false
```

The target model carries the same transitive `conditional_on` set to every
dependent result. The implemented v1 protocol deliberately starts narrower:
it records a typed direct binding and carries that set through direct aliases,
non-transfer expression operands, resolved calls, and `if` value joins.
`cede`, borrowed or provenance-sensitive forms, and other control-flow forms
currently produce no conditional fact rather than a guessed one. The fact
lattice is:

| Status | Meaning |
|---|---|
| `authoritative` | independent of every hole |
| `conditional` | valid only if the listed holes are filled to their requirements |
| `unavailable` | a hole prevented required inference or selection |

Conditional results must never be exported as ordinary semantic evidence
`Allow`, trusted receipts, public API facts, or completed initialization.
Unrelated real errors must still be reported. Diagnostics caused solely by a
hole suppress their downstream cascades.

## Machine protocols and publish boundary

Public Semantic Evidence v1 remains unchanged: its decisions are only
`Allow`, `Reject`, and `ConservativeReject`. Semantic Diff Preview v1 also
remains unchanged. Neither protocol may gain hidden hole fields.

The companion protocol is a new, independently versioned document:

```text
schema:  toka.hole-goals
version: 1
```

Each goal records an id, source location, status, complete expected contract,
and any required provenance/dependencies. Conditional semantic context refers
to `HoleId`s through the separately versioned `toka.conditional-facts`
protocol. Its first slice carries a known hole dependency through direct
bindings, non-transfer expressions, resolved calls, and `if` value joins; a
later Preview v2 may consume both documents. This separation keeps a
requirement from being mistaken for a compiler decision.

Any reachable hole has these hard consequences:

```text
check/build exits nonzero
no object or executable is emitted
no TKI is exported
no reusable compilation cache or trusted receipt is published
no ordinary Allow evidence is signed
diagnostics, hole goals, and conditional editor facts may still be emitted
```

Code generation additionally has a defensive hard rejection for `HoleExpr`,
even though normal front-end diagnostics already prevent reaching it.

## Diagnostics

Each hole receives one primary diagnostic and structured requirement details.
The implementation assigns stable numeric diagnostic identifiers when the
feature lands, with these semantic categories:

- `HOLE-INCOMPLETE`: a complete requirement exists but a build cannot finish;
- `HOLE-UNDERCONSTRAINED`: no complete expected contract exists;
- `HOLE-UNSUPPORTED-CONTEXT`: the context requires a place, resource,
  provenance, constant value, or candidate selection.

Hole-caused follow-up type, morphology, or capability errors are suppressed;
errors independent of the hole are not.

## Implementation order and acceptance evidence

1. Reserve the keyword; add `HoleExpr`; reject forbidden postfix/prefix forms.
2. Add expected-contract checking and `RequirementOnly` zero-capability flow.
3. Add hard CodeGen, TKI, cache, and publish barriers.
4. Add diagnostic cascade suppression and `toka.hole-goals` v1.
5. Add the conservative binding/expression/call conditional-fact protocol
   (implemented).
6. Extend conditional propagation through match, loops, assignments, and
   escaping call returns only with a dedicated dataflow design and evidence
   suite.
7. Only then evaluate labels, type/pattern/declaration holes, generic
   constraints, operators, or any future resource-hole design.

Required tests include contextual assignment, resolved call argument, explicit
generic call, bool condition, underconstrained `auto`, every forbidden
place/resource form, cede parameter rejection, borrowing-return requirement,
no-CodeGen/TKI/cache behavior, conditional propagation, source-less replay
rejection, deterministic hole-goal output, and non-mutation Preview behavior.

## Non-goals

This RFC does not add `any`, gradual typing, implicit `cede`, runtime TODO
semantics, type/pattern/declaration holes, overload inference from holes, or a
long-lived overlay session.
