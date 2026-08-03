# Encap Slice 6: Library And Documentation Migration Audit

Slice 6 makes the library source and current language references match the
positive-capability model introduced by Slices 1–5.

## Library migration

- `lib/` contains no deleted declarations, wildcard `@Encap` grants, or
  legacy `@Clone` / `@Drop` facets.
- `core/traits` declares the empty compiler-recognized `@Copy` and `@Encap`
  markers, plus the explicit `@Dup::dup` capability.
- `Vec<T>` has one unconditional resource policy. Its ordinary APIs no longer
  supply a conditional policy or a negative copy declaration.
- `Duration`, `Instant`, `SystemTime`, `DateTime`, reflection `FieldInfo`,
  logging levels, and LLVM's non-owning handle wrappers are transparent.
- `TaskRef` is an internal `TaskScope` implementation type. It has no public
  duplication or handle-conversion API; the safe `spawn_into` path transfers
  the consumed `TaskHandle` owner reference directly into the scope.
- A raw pointer never becomes a managed-resource fact merely by its
  morphology. The Slice 6 witness uses a raw-handle capsule with an explicit
  lifecycle hook and `@Dup` provider to cover that boundary.
- Every library `@Encap` block now contains only exact field grants and an
  optional drop hook. Ordinary methods, including compatibility-named
  `clone` methods, live in separate ordinary impls; v6 no longer exempts
  trusted library policies or Copy/Dup validation.

An ordinary method whose name is `clone` is not an ownership operation. It is
kept only for API compatibility where it is a normal library call; it does not
provide Copy, Dup, lowering, or trait evidence.

## Current source-language contract

The normative references are [syntax.md](../syntax.md),
[syntax_zh.md](../syntax_zh.md), and the concise semantic entry point
[encap_current_contract.md](encap_current_contract.md). They define the
following boundary:

1. Transparent shapes expose accessible fields and are Copy only when the
   compiler proves their full field graph Copy-safe.
2. A capsule is introduced by one `@Encap` policy with exact field grants and
   an optional private `drop(self#)` hook. The hook is followed by a
   compiler-owned cleanup tail and cannot be called as a normal method.
3. `@Copy` is an empty verified marker. Resource duplication is opt-in
   `@Dup { pub fn dup(self) -> Self }`; it is never selected by ordinary
   assignment, construction, implicit capture, or iterator lowering.
4. Iterator and closure capture preserve the same boundary: a value is copied
   only by a Copy proof, otherwise it is borrowed or explicitly transferred.
   `[dup ...]` is the one explicit closure form that invokes a validated Dup
   provider once; no library-named duplication method is selected implicitly.
5. TKI v2 exports field-graph, policy, Copy, Dup, and custom-drop facts with
   resolver identity. It has no structural lifecycle replay marker.

## Evidence

`tools/scripts/test_encap_slice6_library_audit.py` checks the migrated library
and normative syntax corpus, rejects ordinary methods inside every policy
block, compiles representative core/std/stdx/build modules under v6, then
compiles v6 Copy/Dup/transparent examples and verifies that removed negative
syntax and legacy facets are rejected.
