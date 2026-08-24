# Lookup-to-Miss Pilot

**Status:** successful bounded pilot; broader migration not yet authorized

## Scope

The pilot adds a non-breaking `HashMap::lookup` surface alongside
`HashMap::try_borrow`:

```toka
lookup(key)      -> &'V | miss
try_borrow(key)  -> Option<&'V>
```

`lookup` is available when `'V` satisfies `borrow_extendable`. It preserves
the stored value's admitted morphology and returns a borrow tied to the map.

## Findings

- Hit/miss matching removes the `is_some` plus `unwrap` sequence for callers
  that branch immediately.
- `Option<&V>` remains the better surface when absence must be stored or
  passed as ordinary data.
- A lookup borrow cannot escape a local map; the pilot retains the E0455
  lifetime boundary.
- Source-hidden TKI replay preserves both `&V | miss` and `return <- map`.
- The pilot exposed and fixed two general outcome bugs: hit-return morphology
  was compared against the wrapper instead of its payload, and reference-valued
  hit patterns bound the outcome's pointer slot instead of its referent.

## Preliminary decision

The feature has real value for immediate lookup control flow. Keep the pilot
API and evaluate one ID-based lookup (for example Slab) before considering a
broader 1.0 migration. Do not replace `Option` mechanically.
