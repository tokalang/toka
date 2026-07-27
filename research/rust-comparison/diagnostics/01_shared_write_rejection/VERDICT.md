# Verdict: write through a shared view

## Observed result

Both compilers reject writing an ordinary field through a shared view.  In the
current tested versions, Rust reports `E0594` with a direct `&mut` suggestion.
Toka reports `E04572`, `E04573`, and `E0443`, and its stable invariant-specific
signal is `E04573`.

## Fair conclusion

Neither compiler accepting this program would be correct.  Rust's shorter
message is easy to read for ordinary shared-vs-mutable-reference mistakes, but
its `&mut` suggestion is not the desired repair when the programmer wants
field-level interior mutability.  Toka has a more specific authority invariant
and currently emits a cascade, but now adds a field-authority explanation that
identifies why the shared view does not make the sibling writable.

This is evidence for diagnostic refinement, not for changing either language's
write-safety rule.  It is the concrete basis for
[`EXP-003`](../../IMPROVEMENT_BACKLOG.md#exp-003--explain-field-level-interior-mutability-failures-in-field-terms).
