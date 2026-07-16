# Module Shape Identity

Status: `Frozen`

## Rule

A named shape is a nominal type identified by its resolved declaration, not by
its unqualified spelling. Two modules may therefore declare shapes with the
same name without sharing layout, members, ownership facts, or type identity.

Lexical lookup selects the declaration visible at the use site. A qualified
module path selects the declaration from that module. Once resolved, the
declaration identity travels with the type through inference, return values,
function parameters, member access, compatibility checks, and lowering; it is
not rebound by the caller's bare-name environment.

## Interface Replay

Same-version `.tki` replay preserves the declaring module boundary. Source and
source-less imports must agree on all of the following:

- constructing and using a local shape when an imported module has a private
  shape with the same spelling;
- accessing values returned as an imported public shape when a local shape has
  the same spelling; and
- rejecting assignment or argument passing between those two nominal types.

When a type mismatch contains the same unqualified spelling on both sides, the
diagnostic qualifies the module names so `expected Counter, got Counter` is not
reported as the explanation.

## Lowering Boundary

The compiler may assign colliding concrete shapes deterministic internal LLVM
names. Those names are implementation details tied to the compiler build and
module input; they are not source names, `.tki` API, or a stable binary ABI.
This rule does not change generic syntax or generic instantiation semantics.

The current evidence closes concrete non-generic shape and enum declarations.
Same-name generic template instantiation/cache isolation is not inferred from
that evidence; it is a separate pending correctness audit (`FZ-3-R02`).

## Evidence

- Positive execution:
  `tests/pass/g09_module_private_shape_isolation.tk`
- Source and source-less replay:
  `tests/semantics/tki_replay/cases/ergonomics_003_shape_identity`
- Stable mismatch diagnostic: `E04571`
- Implementation: `src/Sema/Sema.cpp`, `src/Sema/Sema_Type.cpp`,
  `src/Type.cpp`, `src/CodeGen/CodeGen_Decl.cpp`, and
  `src/CodeGen/CodeGen_Expr.cpp`
- Integration evidence: the unified package smoke compiles `tokafmt`, whose
  root and imported lexer modules intentionally contain same-named `Token` and
  `TokenKind` declarations.

The evidence intentionally tests different struct field layouts and distinct
enum variants so accidental bare-name unification cannot pass as harmless
structural equality.
