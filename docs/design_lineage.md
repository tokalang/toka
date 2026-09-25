# Design Lineage: Hat Syntax and Payload/Handle Semantics

This document records the evidence currently available for the development of
Toka's hat syntax and payload/handle semantics. It also acknowledges earlier
mechanisms with a close or partially overlapping surface or behavior. It is not
a claim of global novelty, derivation, equivalence, or superiority.

## Evidence Boundary

- **Maintainer account:** Toka's hat syntax and payload/handle model were
  developed independently. During related-work research undertaken in 2026
  while preparing a Toka manuscript, the maintainers first became aware of the
  relevant Cforall and Alusus mechanisms described below.
- **Repository evidence:** The public Git history records the design milestones
  below by their stated dates.
- **Limit:** The repository history does not independently establish the exact
  date on which those mechanisms were first encountered; that discovery context
  is a maintainer account. The entries below are documented milestones, not
  claims about the first private idea or the first invention of a mechanism.

## Current RC13 Rule and Comparison Scope

For a concrete handle binding, a bare use selects its payload view and an
explicit hat can select a handle view or create a borrow, subject to that
operation's permission, lifetime, and ownership checks. Raw, borrowed, unique,
and shared handles participate in this source-level distinction; they do not
have identical transfer or cleanup rules. The public [syntax guide](syntax.md)
specifies the admitted forms.

An abstract whole-value binding of generic type `T` is an important exception
to any slogan that *all* bare names mean payloads: while `T` remains abstract,
the name denotes the complete `T`, even if a later concrete instance has a
handle root. Monomorphization does not reinterpret that source occurrence.
An access whose source contract is already concrete follows the concrete hat
rules.
This RC13 rule is detailed in the
[whole-value generics boundary](semantic_core/whole_value_generics_and_checked_dependency_elision_rfc.md).

The comparisons below concern source expression and assignment rules, not
whether another language uses identical AST nodes, physical storage, or
ownership proofs. An earlier local correspondence remains relevant even when
the complete languages differ. This targeted record is not an exhaustive
priority search or a formal proof of a unique design.

## Documented Toka Timeline

Dates below are commit dates recorded in the public repository. "Earliest"
means the earliest evidence currently located in that history.

- **2025-12-29 — Pointer morphology and point-value duality.**
  Commit [`8329c7f2`](https://github.com/tokalang/toka/commit/8329c7f27fddafe07595815204889d5108131067)
  implemented the rule that ordinary variable expressions refer to objects by
  default while prefixes such as `*`, `^`, and `~` expose identity or handle
  forms. This is the earliest implementation record currently located for the
  core distinction.
- **2026-01-29 — Single Hat Principle enforcement.**
  Commit [`5ce13292`](https://github.com/tokalang/toka/commit/5ce132922d1017e1c4dae1174873fa2e8b347b56)
  introduced the "Single Hat Principle" terminology in compiler-side terminal
  access checks.
- **2026-02-01 — Dedicated Hat Principle documentation.**
  Commit [`c696e598`](https://github.com/tokalang/toka/commit/c696e5988856c59976844a2c10ee1432bcef784c)
  documented bare `p` as the object or payload view and `*p` as access to the
  pointer handle, including the distinction between payload assignment and
  handle rebinding.
- **2026-03-25 — Pointer-morphology representation work.**
  Commit [`7b1783c0`](https://github.com/tokalang/toka/commit/7b1783c0248d4d038ab4e767c2cc07e9aa2b78b0)
  refactored the lexer, type parser, and type representation for the revised
  pointer morphology.
- **2026-06-27 — Public payload/handle framing.**
  Commit [`060aa5bb`](https://github.com/tokalang/toka/commit/060aa5bb371372b464280d3f3ec10244d0ca6cac)
  made the payload/handle distinction explicit in the README and described hat
  syntax as a consequence of the design rather than its goal.

## Related Mechanisms

These references acknowledge precedents for individual mechanisms. They do not
imply that the systems have the same overall model or responsibility split.

### C pointer slots

C pointer types compose recursively. Given `int *p` and `int **pp = &p`,
`**pp` designates the integer target while `*pp` designates the pointer slot
`p`, which can be rebound. Taking the address of such a slot is likewise an
ordinary typed lvalue operation. These storage capabilities and repeated
pointer syntax are not inventions of Toka. See the type, indirection, and
assignment rules in the
[C11 committee draft N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).
Toka's comparison concerns the source convention for selecting a payload or
handle view, not whether C can reach or change the same underlying storage.

### Cforall rebindable references

Cforall's [multi-level references](https://cforall.uwaterloo.ca/features/#RebindableReferences)
are rebindable and automatically dereferenced to their referent. Taking `&r`
can cancel an implicit dereference and expose the reference cell; repeated
`&` can select outward reference levels. Aaron Moss's
[2019 thesis](https://uwspace.uwaterloo.ca/items/2ad9e91b-d4a6-4c13-a44c-92e9fc488093)
documents both `&r = &y` rebinding and `&&r` addressing the reference slot.
This is a substantive earlier counterpart to payload-default access and
explicit selection of handle layers along a pure reference chain, not merely
a resemblance between punctuation marks.

Cforall also retains ordinary C-style pointers, whose bare names denote
pointer values rather than automatically reached payloads. Toka applies its
concrete payload/handle convention across raw, borrowed, unique, and shared
handles, with distinct permissions for each. The languages may reach
corresponding cells in the reference-only case; neither this correspondence
nor a different internal representation proves equivalence or historical
influence between their full models.

### Alusus content-default references

The [Alusus language reference](https://alusus.org/Documents/lang-reference.en.html)
documents `ref[ref[Int]]`, content-default operations irrespective of reference
depth, repeated `~ptr` to expose reference levels, and `~no_deref` when a
reference itself must be changed. This is another close local precedent for
the distinction between content access and reference rebinding. Alusus's raw
pointer and smart-reference operations use other conventions, including
`~cnt` and `.obj` in its
[pointer and smart-reference tutorial](https://alusus.org/Documents/Tutorial.en/7_pointers_references_smart_references.html).
The documented categories therefore do not establish Toka's complete
four-category expression convention.

### Fortran pointer association

Fortran distinguishes [intrinsic assignment](https://www.intel.com/content/www/us/en/docs/fortran-compiler/developer-guide-reference/2024-2/intrinsic-assignment-statements.html)
to an associated pointer's target (`p = value`) from
[pointer assignment](https://www.intel.com/content/www/us/en/docs/fortran-compiler/developer-guide-reference/2024-2/pointer-assignments.html)
that changes association (`p => target`). This is an earlier payload-versus-
association distinction. It uses different assignment forms rather than a
common hat-view syntax across Toka's handle categories.

### Chapel ownership categories

Chapel distinguishes `owned`, `shared`, `borrowed`, and `unmanaged` class
types. Its [class specification](https://chapel-lang.org/docs/language/spec/classes.html)
states that assignment between `owned` values transfers ownership, leaving
the source empty. Thus the four management categories are not themselves
Toka's invention, while Chapel's ordinary owned assignment is not Toka's
concrete payload-assignment convention. `unmanaged` classes also should not
be treated as equivalent to every raw-pointer use.

### Considered alternatives

The [Carbon values and references proposal](https://docs.carbon-lang.dev/proposals/p002006-values-variables-pointers-and-references.html)
explicitly discusses syntax-free dereferencing and a references-only
alternative, then argues against losing the visible marker for non-local
access. It establishes that broad automatic-dereference ideas were considered
elsewhere; it does not specify Toka's four-category hat convention.

## Scope of Toka's Design Claim

Toka does not claim repeated pointer/reference glyphs, recursive pointer or
reference structure, payload-default access, explicit handle selection, or
reference-slot rebinding as first inventions. The documented design emphasis
is the consistent *concrete* payload/handle expression convention across its
four handle categories, combined with ownership, borrowing, rebinding, and
resource contracts. The abstract generic whole-value exception and each
category's access restrictions remain part of that description.

The cited works show substantial local overlaps and different design choices.
They do not establish that Toka derived from them, nor does this limited survey
establish that no earlier language used the same complete combination. This
document does not present an order-only sketch as normative RC13 semantics or
as an independently verified formal theorem.
