# Toka v1.0.0-rc.8 Release Candidate Notes

These notes describe the intended RC8 contents. They are not qualification or
publication evidence. Candidate identity, hosted jobs, asset digests, replay
receipts, and release state belong in the
[RC8 qualification audit](release_audits/v1.0.0-rc.8.md).

## Language and safety changes

- Init/PlaceState projection analysis now distinguishes live, moved,
  maybe-moved, and repopulated places across branches and control-flow exits.
- Handle Grammar admits the qualified borrow-view domain while rejecting
  illegal managed/raw mixing, invalid ordering, excessive depth, type-side
  parameter hats, and root-hatted aliases before LLVM lowering.
- Binding-side handle chains and H/P permission placement are preserved by
  parser, cloning, TKI, generic replay, and CodeGen.
- Unique value initialization, assignment, and return perform a visible direct
  move from `^source`; `cede` remains the explicit consuming parameter/capture
  contract.
- Generic morphology constraints (`soul_only`, `borrow_extendable`, and
  `raw_extendable`) make substitution domains explicit and fail at the
  definition or instantiation boundary instead of silently removing APIs.

## Borrowing and iteration

- Public borrowing verbs use `borrow` / `borrow_mut`; the legacy `get_ref`
  surface is removed. `unsafe_get` is the explicitly raw Vec lookup.
- By-value container lookup requires an appropriate `@Dup` capability rather
  than silently copying resource values.
- `for alias` binds the exact element place. Qualified shared/read Array and
  Vec iteration use canonical `@PlaceIterator`; the compiler-only
  `PlaceOutcomeType` is not storable, constructible, reflectable, or visible to
  `sizeof`.
- Writable non-array alias remains on its qualified RC8 compatibility carrier.
  Existing `for auto`, including `for auto &&x`, remains unchanged.

## Miss lookup pilots

- HashMap, Slab, and HeaderMap include explicit `T | miss` lookup pilots with
  source dependency and PAL protection retained through match arms and
  source-less replay.
- `T | miss` remains distinct from `Option<T>` and is not a general union.

## Migration from RC7

- Move root hats from parameter type sides to binding syntax; formal parameter
  roots remain limited to one handle layer.
- Do not hide a root `^`, `~`, `&`, or `*` behind `alias`; structural children
  such as `Option<^T>` and `Vec<&T>` remain accepted in RC8.
- Replace Vec `get_ref(index)` with `borrow(index)` or `borrow_mut(index)`;
  use `unsafe_get(index)` only for explicit raw-pointer work.
- Add `@Dup` bounds where an API returns an existing container element by
  value; use a borrow API when duplication is not intended.
- Treat `for alias` as non-owning: it cannot be ceded, moved, captured, or
  returned as independent storage.

## Provisional RC8 boundary

RC8 deliberately keeps migration mode A. Structural and borrow-only level-2
forms remain compatibility syntax while real users exercise exact-place alias
iteration. Their final 1.0 storage boundary is not promised by this RC.
Mutable PlaceIterator P2, alias return, projection-place, and broader container
qualification are not part of RC8.

## Interface cache boundary

RC8 uses `.tki` format `3`, compiler-interface key `0.9.9-14`, and required
`place_yield_abi_schema: 1`. RC7 `.tki`, object, and semantic-cache artifacts
must be discarded and rebuilt.

After publication, install RC8 explicitly rather than relying on the stable
release selector:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.8
```
