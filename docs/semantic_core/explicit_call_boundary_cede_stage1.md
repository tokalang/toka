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

## Instance-method parameter slice

**Status:** implementation slice; pending acceptance.

The next route slice applies the same named-source handshake to concrete
parameters of a source-resolved instance method. Generic-value and morphic
method parameters remain outside this slice because borrowed/raw generic
substitutions still use the accepted Stage 0 conservative boundary. The
receiver is not part of the method's argument vector and remains governed by
the pre-existing receiver rules. In particular, this slice does not require a
new caller spelling for a `cede self` receiver.

```toka
auto sink = Sink()
auto value = 42

sink.consume(value)       // E04509: named method argument requires `cede`
sink.consume(cede value)  // accepted; value becomes unavailable
sink.consume(42)          // accepted NoSourcePlace value
```

Method argument groups retain the same pre-mutation snapshot and atomic
rollback guarantee as direct calls. `unsafe` and transparent postfix wrappers
do not hide a named source. The method diagnostic carries the same exact
machine-applicable `cede ` insertion as the direct-call diagnostic.
For a legacy consuming receiver, that snapshot is established before receiver
evaluation and invalidation. If a concrete parameter later rejects the call,
transfers performed by an expression or nested method receiver, the final
receiver, and all arguments return to the pre-call snapshot without changing
the accepted receiver spelling.
The snapshot is initially disarmed unless an argument already contains an
explicit transfer, preserving the pre-existing atomic-call guarantee. It is
also armed after method resolution proves that this slice contains a concrete
`cede` formal. Other ordinary, generic/morphic, dynamic-trait, and
historical-replay failures retain their pre-slice state and diagnostics.

Dynamic-trait dispatch, indirect function values, callable expressions, and
all receiver morphology/spelling work remain outside this slice.

## Dynamic-trait parameter slice

**Status:** implementation slice; pending acceptance.

Concrete parameters selected through `dyn @Trait` dispatch now use the same
caller handshake and transaction rules:

```toka
fn invoke(sink: dyn @Sink) -> i32 {
    auto value = 42
    return sink.take(value)       // E04509
    // return sink.take(cede value)  // accepted; value becomes unavailable
}
```

The method-call snapshot remains captured before receiver evaluation and is
armed when the selected trait declaration proves a concrete `cede` formal.
This includes expression receivers whose evaluation transfers another source.
Explicit argument transfers retain their pre-existing atomic rollback rule.

Generic-value and morphic trait parameters remain outside this slice. Dynamic
receiver spelling and morphology are unchanged, as are indirect function and
callable-expression routes. No Parser, TKI, ABI, interface-key, or evidence
schema change is introduced.

## Indirect function parameter slice

**Status:** implementation slice; pending acceptance.

Resolved `fn` and `dyn fn` values now enforce explicit caller spelling for
their concrete `cede` parameters:

```toka
auto callback = { value => cede value }:fn(cede i32) -> i32
auto value = 42

callback(value)       // E04570
callback(cede value)  // accepted; value becomes unavailable
callback(1)           // accepted NoSourcePlace value
```

The rule covers direct values and exact unique-handle spelling, transparent
`unsafe` wrappers, multi-argument atomic rejection, and source-hidden function
types reconstructed from TKI. `fn` and `dyn fn` share the same parameter
policy. Ordinary Copy parameters remain `KeepLive`, while complete owned
temporaries transfer cleanup liability and are destroyed exactly once.

The callable binding remains an ordinary receiver in this slice. Consuming a
callable expression, general `InvokeExpr`, generic/morphic or indeterminate
formal provenance, and receiver spelling remain outside this activation. No
Parser, TKI, ABI, interface-key, or evidence schema change is introduced.

Indirect-call state is captured before a callable binding can be consumed and
before any argument expression is evaluated. The guard is armed only when the
resolved signature contains an admitted Stage 1 parameter (or when an explicit
argument transfer already requires the historical atomic-call guarantee).
Consequently, a rejected outer call rolls back transfers performed by nested
arguments and by `cede callback(...)`.

For function-typed parameters, the binding symbol also carries a per-parameter
declaration origin derived from the unspecialized `TypeSyntax`. Substitution
does not turn `fn(cede T)` into a source-written concrete `fn(cede i32)`:
generic/morphic origins remain excluded after monomorphization. Missing alias
or declaration provenance fails closed rather than being inferred from the
resolved payload type.

The generic context belongs to the callable declaration, not to its caller. A
locally ascribed concrete `fn(cede i32)` remains concrete inside a generic
function. Conversely, a callable parameter that names an enclosing generic
trait/impl binder remains generic after materialization. Both function and
dynamic-function forms use the same declaration-side classification.
