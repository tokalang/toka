# RFC: Unit, `void`, and `never`

**Status:** Design decisions frozen; implementation pending.

**Scope:** This RFC separates Toka's ordinary no-result value, its ABI-level
no-value representation, and non-completion. It fixes their source-level
meaning before any parser, Sema, CodeGen, or `.tki` change. It does not add
`never` to the language yet.

## 1. Motivation

The current compiler uses the spelling `void` for several unrelated facts:

- a function with no written return contract;
- an explicitly written `-> void` contract;
- LLVM and C-ABI no-value lowering;
- the pointee of raw opaque pointers such as `*void`; and
- internal expression and control-flow paths that have not produced a value.

Those facts must be distinct before Toka can express a callable which cannot
complete normally. In particular, `never` cannot be another spelling for
Unit, nor may it reuse an internal "no value yet" sentinel.

## 2. Frozen semantic categories

### UVN-01: `()` is Unit

`()` is the ordinary Toka Unit type. It has exactly one value and means that
an action completed successfully without producing a domain result. It is a
normal type: it may appear where an ordinary type is meaningful, including a
generic argument or a field, subject to the usual layout rules.

Unit is not an ABI no-value marker and is not an uninhabited type.

### UVN-02: `void` is an ABI and raw-pointer concept

`void` denotes no C/LLVM return value and the opaque pointee in `*void`. It
does not denote the ordinary result of a completed Toka action.

The eventual public boundary is therefore:

```toka
extern fn puts(text: *u8) -> void
extern fn copy(dst: *void, src: *void, size: usize) -> void
```

`void` remains valid at those FFI/raw-pointer boundaries. Normal Toka
declarations, fields, enum payloads, generic arguments, and local variables
must not use it as a substitute for Unit. The detailed migration inventory is
an implementation requirement, not permission to retain an alternate meaning.

### UVN-03: `never` is the bottom, non-completing type

`never` is uninhabited. An expression of type `never` does not produce a
normal value: it may panic, abort, loop forever, or invoke another
non-completing operation. A `never` expression is admissible where any result
type is expected, because control does not continue from it.

Illustrative future code:

```toka
fn fatal(message: string) -> never {
    panic(message)
}

auto port = if valid { parsed } else { fatal("invalid port") }
```

`never` is neither `()` nor `void`. No `return`, `pass`, empty block, or
default value constructs a value of type `never`.

## 3. One source meaning for each function-result intent

### UVN-04: Ordinary Unit-returning functions omit the return arrow

For an ordinary Toka function, an omitted return contract means Unit:

```toka
fn log(message: string) {
    print(message)
}
```

`-> ()` is rejected in an ordinary function declaration. It would repeat the
same completed-action intent with a second spelling. Function type syntax
follows the same rule: an omitted result is Unit.

This rule does not prohibit `()` in ordinary type positions; it only chooses
one function-contract spelling for Unit.

### UVN-05: FFI no-value is explicit

An `extern` function with C/LLVM no-value ABI writes `-> void`. Omitting the
ordinary function return arrow must not silently select an ABI representation.
The implementation must reject an omitted or `-> ()` extern result when the
foreign ABI requires no value, with a direct diagnostic that requests
`-> void`.

### UVN-06: Non-completion is explicit

A synchronous ordinary function that promises not to complete normally writes
`-> never`. It may not use a named return, a return dependency (`<-`), or an
ordinary `return` statement:

```toka
fn fatal(message: string) -> never {
    panic(message)
}
```

Every reachable path must be non-completing. A rejected normal fallthrough is
part of the declaration contract, not a late CodeGen failure.

## 4. Deliberately deferred `never` surface

The first implementation slice permits `never` only as an explicit result
contract of a synchronous ordinary function. It does not yet permit:

- `never` in fields, generic arguments, aliases, or arbitrary type positions;
- `async fn ... -> never`;
- `extern fn ... -> never`;
- named `never` returns or `never <- dependency` contracts; or
- a special `main -> never` convention.

An async declaration returns a task-like object before its body reaches a
completion state, so "the callable never returns" and "the task never
completes normally" are separate contracts. They need a dedicated async RFC.
Likewise, ABI-level non-returning functions require a target/ABI decision,
not an inference from ordinary Toka `never`.

The initial restricted surface establishes the type and flow rules without
silently committing Toka to bottom-type behavior in public generic APIs.

## 5. Required implementation separation

Before accepting `never`, the compiler must separate the following internal
representations:

1. semantic `UnitType`;
2. semantic `VoidType` and LLVM `void` lowering;
3. an explicit expression/control-flow "no produced value" state; and
4. semantic `NeverType` plus a bottom-aware result merge.

In particular, semantic analysis must not use the text `"void"` as an
uninitialized expression result, an empty branch result, a missing enum
payload, or a generic flow sentinel. Source parsing must preserve omitted,
Unit, ABI-void, and never contracts distinctly even while existing CodeGen is
being migrated.

`never` is accepted only after branches can merge a normal result with a
non-completing result without treating either as `void`. Existing unreachable
analysis and all-paths-jump facts may supply the proof, but they are not a
replacement for the type distinction.

## 6. Acceptance gate

The implementation is complete only when all of the following hold:

- ordinary omitted-result functions are represented and lowered as Unit;
- foreign `-> void` declarations preserve their ABI and reject ordinary-unit
  spellings at the FFI boundary;
- `*void` retains opaque-pointer behavior;
- `-> never` rejects every reachable normal completion and accepts every
  verified diverging path;
- a `never` expression type-checks in a branch expecting an ordinary result,
  while Unit and void do not acquire bottom coercions;
- parser diagnostics, Sema, `.tki` export/import, cache invalidation, and
  source-less replay preserve each result category; and
- existing `void`-sentinel use sites are removed or replaced by an explicitly
  named internal state, with tests for empty blocks, branches, enum payloads,
  ordinary returns, FFI returns, and raw pointers.

## 7. Relation to expression uniqueness

This RFC supplies the result-contract rule referenced by EU-06 in
`docs/expression_uniqueness_rfc.md`: function-result omission, `void`, and
`never` remain distinct only because they state distinct semantic categories.
`-> ()` in an ordinary declaration is deliberately excluded as duplicate Unit
spelling.
