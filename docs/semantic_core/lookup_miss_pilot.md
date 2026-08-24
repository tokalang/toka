# Lookup-to-Miss Pilot

**Status:** two successful bounded pilots; broad mechanical migration is not
recommended

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
```

The lookup methods are available when the stored type satisfies
`borrow_extendable`. They preserve the stored value's admitted morphology and
return a borrow tied to the container.

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
  `return <- owner` contracts for both pilots.
- The pilot exposed and fixed two general outcome bugs: hit-return morphology
  was compared against the wrapper instead of its payload, and reference-valued
  hit patterns bound the outcome's pointer slot instead of its referent.
- The Slab pilot exposed a third general bug: the expression checker did not
  recognize a reference nested in a miss outcome, discarded the return
  dependency, and therefore failed to register the hit-arm PAL loan.

## Decision after two pilots

`miss` has demonstrated value for lookup operations whose ordinary consumer
branches immediately. It is especially clear for ID/generation lookup, where
staleness is expected control flow rather than a value to retain.

Keep the HashMap and Slab lookup APIs as the qualified 1.0 surface, alongside
their `Option` forms. Migrate additional core lookup APIs only case by case:

- use `T | miss` when absence is locally and immediately matched;
- use `Option<T>` when absence must be stored, returned as data, transformed,
  or passed onward;
- do not rename or replace existing `Option` APIs mechanically.
