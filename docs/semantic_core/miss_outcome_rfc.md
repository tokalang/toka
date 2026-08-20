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

`Option<T>` remains an ordinary explicit zero-or-one container and has no
implicit conversion with `T | miss`. Safe nullable payloads/owning handles,
`none`, and nullable postfix/assertion syntax are permanently removed by
E0484-E0487; they have no Sema, PAL, TKI, or CodeGen representation. Physical
zero remains raw-pointer-specific through `nul *T`. Later Option migrations
must classify each API by meaning rather than mechanically rewriting types.
