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

### C

C pointer types compose recursively, and repeated pointer declarators and
operators provide the longstanding baseline for multi-level pointer structure.
See the type and declarator rules in the
[C11 committee draft N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).

### Cforall

Cforall documents
[multi-level rebindable references](https://cforall.uwaterloo.ca/features/#RebindableReferences)
such as `&&&`, automatic dereferencing across the declared depth, and explicit
selection of reference levels by cancelling implicit dereferences. This is a
close precedent for repeated reference sigils and multi-level reference
selection.

### Alusus

Alusus's official English reference, published in 2023, documents nested
references such as `ref[ref[Int]]` and operations that apply to the referenced
content regardless of reference depth. It describes `~ptr` as starting from the
content layer and shows repeated `~ptr` when assigning nested-reference
handles, while `~no_deref` suppresses automatic reference following so that
the reference itself can be changed. See the immutable source for
[nested references](https://github.com/Alusus/Alusus/blob/b777761c5b07a51bd785f510c7811e6d3f18adc8/Doc/lang-reference.en.html#L1358-L1371)
and
[`~no_deref`](https://github.com/Alusus/Alusus/blob/b777761c5b07a51bd785f510c7811e6d3f18adc8/Doc/lang-reference.en.html#L1391-L1403).

## Scope of Toka's Design Claim

Toka does not claim repeated pointer/reference glyphs, recursive pointer or
reference structure, automatic payload access, or explicit handle selection as
first inventions. Its design focus is the way hat forms participate together
in payload/handle selection, ownership, borrowing, rebinding, and resource
contracts.
