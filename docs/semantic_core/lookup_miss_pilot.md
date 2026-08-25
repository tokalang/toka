# Lookup-to-Miss Pilot

**Status:** three successful bounded pilots and one rejected candidate; broad
mechanical migration is not recommended

## Scope

The pilots add non-breaking lookup surfaces alongside the existing
`Option`-returning APIs:

```toka
HashMap::lookup(key)       -> &'V | miss
HashMap::try_borrow(key)   -> Option<&'V>

Slab::lookup(id)           -> &'T | miss
Slab::lookup_mut(id)       -> &'T# | miss
Slab::get(id)              -> Option<&'T>
Slab::get_mut(id)          -> Option<&'T#>

HeaderMap::lookup(key)     -> &string | miss
HeaderMap::get(key)        -> Option<string>
```

Generic lookup methods require the stored type to satisfy `borrow_extendable`.
All three APIs return a borrow tied to the container; generic APIs also
preserve the stored value's admitted morphology.

## Findings

- Hit/miss matching removes the `is_some` plus `unwrap` sequence for callers
  that branch immediately.
- `Option<&V>` remains the better surface when absence must be stored or
  passed as ordinary data.
- A lookup borrow cannot escape a local map; the pilot retains the E0455
  lifetime boundary.
- Slab's stale generation is naturally an immediate `miss`: an old ID misses
  after slot reuse while the replacement ID hits. Mutable hits preserve `&T#`.
- A matched Slab borrow blocks removal of the borrowed entry/container in the
  hit arm (E0441).
- Source-hidden TKI replay preserves `&T | miss`, `&T# | miss`, and their
  `return <- owner` contracts across all three accepted pilots.
- HeaderMap demonstrates a real non-generic API migration: request and response
  parsing now use borrowed lookup results for immediate branching without
  allocating an `Option<string>` and copying the header value.
- The pilot exposed and fixed two general outcome bugs: hit-return morphology
  was compared against the wrapper instead of its payload, and reference-valued
  hit patterns bound the outcome's pointer slot instead of its referent.
- The Slab pilot exposed a third general bug: the expression checker did not
  recognize a reference nested in a miss outcome, discarded the return
  dependency, and therefore failed to register the hit-arm PAL loan.
- BTreeMap was deliberately rejected as a borrowed-lookup candidate. Its nodes
  are connected through raw `Addr` values, so a local `*BTreeNode` cannot prove
  that a returned `&V` is rooted in the map; E0455 correctly rejects the escape.
  An unsafe cast would hide the provenance break rather than repair it. Its
  existing by-value `get` is therefore restricted to `V: @Dup` and performs an
  explicit `dup`; it is not a substitute for a future provenance-safe borrow.

## Decision after the bounded pilots

`miss` has demonstrated value for lookup operations whose ordinary consumer
branches immediately. It is especially clear for ID/generation lookup, where
staleness is expected control flow rather than a value to retain.

Keep the HashMap, Slab, and HeaderMap lookup APIs as the qualified 1.0 surface,
alongside their `Option` forms. Migrate additional core lookup APIs only case
by case:

- use `T | miss` when absence is locally and immediately matched;
- use `Option<T>` when absence must be stored, returned as data, transformed,
  or passed onward;
- do not rename or replace existing `Option` APIs mechanically.
