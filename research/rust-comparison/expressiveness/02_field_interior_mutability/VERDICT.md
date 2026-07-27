# Verdict: field-level interior mutability

## Observed result

Toka accepts a write to `reads#` through a shared aggregate view and rejects a
write to the ordinary sibling field.  The capability comes from the field
declaration; it is not manufactured by the shared view or a use-site marker.

Rust accepts the same operational shape when `reads` is stored in
`std::cell::Cell<i32>`.  The paired rejection program shows that an ordinary
`i32` cannot be written through `&Counter`.

## Fair conclusion

Toka expresses this choice directly on the field declaration and keeps the
field representation as an ordinary `i32` at source level.  Rust expresses it
through a standard-library wrapper, which has a very explicit semantic name and
extends to richer choices such as `RefCell`, atomics, locks, and custom cells.
Neither example is a thread-safety claim: `field#` and `Cell<T>` are both
separate from synchronization requirements for cross-thread access.

## Improvement opportunity for Toka

The rejection is correct, but its diagnostics currently describe generic
immutability rather than the precise field-level rule.  A note that identifies
the missing `ordinary#` declaration would make the contrast with the accepted
`reads#` write much easier to learn.  See
[`EXP-003`](../../IMPROVEMENT_BACKLOG.md#exp-003--explain-field-level-interior-mutability-failures-in-field-terms).
