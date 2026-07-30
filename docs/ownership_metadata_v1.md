# Internal Value Ownership Metadata v1

Status: **Implemented compiler-internal contract**

This document freezes an implementation boundary, not a source-language
feature. Toka gains no ownership annotation, modifier, inference output, or
new accepted spelling from this work.

## Rule

Every resolved `Type` exposes one internal `ValueOwnership` classification:

- `Trivial`: copying produces no cleanup responsibility;
- `BorrowedView`: the value observes storage owned elsewhere;
- `SharedHandle`: copying follows the existing shared-handle path;
- `Owned`: an existing storage value must be explicitly transferred before it
  initializes a fresh cleanup-owning field.

The current classifier is `Type::valueOwnership(Sema*)`. Pointer and fixed
array cases are derived structurally. Resolved shapes use their declared
lifecycle facts. The compiler's two intrinsic owner types, `string` and
`Bytes`, are also represented here because their buffer cleanup is lowered by
the runtime path rather than by the general `Sema::hasDrop()` query. Borrowed
core views such as `str`, `bytes`, and `cstr` classify as `BorrowedView`;
non-owning scalar-like runtime types remain `Trivial`.

## Consequence

Aggregate initialization asks only:

```text
member type requires explicit ownership transfer?
```

It no longer carries an ad-hoc list of owner/view names. An existing lvalue
source—local, field projection, or fixed-array element—therefore requires
`cede` exactly when the destination type is `Owned`:

```toka
shape Message(body: string)

auto text = string::from("hello")
auto message = Message(body = cede text)
```

Fresh constructors and call results remain valid without `cede`; they have no
prior storage owner to invalidate. Shared, raw, and borrowed values retain
their pre-existing rules.

An explicit whole-record spread transfer carries the same fact to every
resource-bearing field synthesized by that spread:

```toka
auto replacement = Record(new_id = 2, cede original.*)
```

This is one explicit ownership boundary, not an implicit exception.  The
semantic expansion preserves it as a `cede` for each synthesized owned member,
so later named-field validation and lowering cannot mistake the transfer for a
copy.

## Non-goals

- This does not change structural-drop lowering or `@encap drop` semantics.
- It does not add public reflection over ownership classes.
- It does not decide future `cede` re-rooting or permission-flow rules.

## Evidence

- `aggregate_owned_field_requires_cede.tk` and
  `aggregate_owned_field_projection_requires_cede.tk` reject an implicit copy
  of an owned local or projection;
- the matching `*_cede_exactly_once.tk` cases execute exactly one cleanup;
- the projection case proves that the decision applies to an access path, not
  only a bare variable name.
- `g08_spread_nominal.tk` proves that `cede record.*` transfers synthesized
  owning fields without weakening the ordinary named-field rule.
