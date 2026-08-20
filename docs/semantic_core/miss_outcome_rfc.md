# RFC: Miss Outcome Core

**Status:** Additive P0 implementation with synchronous and async return ABI
coverage.

## Contract

`T | miss` is a distinct semantic type whose concrete live states are
`Hit(T)` and `Miss`. These states are independent from a place's
`Never | Live | Moved` fact. The surface `| miss` spelling is not a general
union operation and does not alias `Option<T>`.

The only outcome introduction points are returns from a function whose
declared result is the same `T | miss`:

```text
return value  : T -> T | miss (Hit)
return miss   :      T | miss (Miss)
```

Calls, arguments, assignments, fields, `cede`, and further returns transport
an already introduced outcome under their ordinary rules. No ordinary
expression may construct a hit or miss state.

`T | miss` has no default value. `uninit:(T | miss)` creates storage in the
PlaceState `Never`; it does not initialize the outcome discriminator. A normal
initialization must receive an already introduced outcome. Reads and matches
require the place to be definitely `Live`.

An exhaustive match distinguishes the `miss` contextual pattern from one
fresh payload binding. Resource cleanup consults the outcome discriminator:
only a live Hit owns and drops `T`.

## Compatibility boundary

This slice does not change `Option<T>` or the runtime representation of legacy
nullable handles and payloads. The safe nullable surface now emits migration
warnings before its separate removal; raw/FFI null remains outside that work.
`T | miss` and `Option<T>` have no implicit conversion. Later migrations must
classify each API by meaning rather than mechanically rewriting types.
