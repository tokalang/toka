# Verdict: dyn objects with an associated-type binding

## Observed current boundary

The Rust program uses only `std` language facilities.  It fixes
`Readable::Item` to `i32`, then erases the implementation behind
`&dyn Readable<Item = i32>`.  The method has a fixed return type at the call
site and dynamically dispatches through the trait object.

Toka 1.0 rejects the corresponding form with `E0617`.  Its frozen dyn rule
requires a trait to erase to a fixed receiver handle and fixed vtable ABI;
traits with associated types are not object-safe, and associated-type binding
syntax on `dyn @Trait` is outside the 1.0 surface.  This is stated in
[`docs/syntax.md`](../../../../docs/syntax.md) and locked independently by
`tests/fail/dyn_trait_associated_type_binding_syntax.tk`.

## Classification

This is a **current 1.0 boundary**, not a PAL model limitation.  Toka supports
concrete associated-type projection and ordinary generic abstraction.  Its
current alternatives are a concrete generic parameter, a wrapper trait without
an associated type, or a concrete adapter.

## Fair tradeoff

Toka's current restriction keeps dyn-object erasure and its receiver/vtable
ABI deliberately small and uniform.  Rust gains runtime erasure of a trait
family once an associated type is fixed.  This case does not measure dispatch
cost, prove either ABI superior, or establish that Toka needs Rust-style
lifetime syntax to add an explicitly bound dyn-object form later.

## Evidence needed before an extension RFC

An extension needs a separate object-safety and ABI design, including the
representation of associated-type bindings in `.tki`, coercion rules, public
trait visibility, method return lowering, and source-less replay.  Until that
work has a concrete proposal, this case remains a boundary record rather than
an implementation roadmap.
