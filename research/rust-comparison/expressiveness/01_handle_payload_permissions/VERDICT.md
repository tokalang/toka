# Verdict: handle replacement and payload mutation

## Observed result

Toka accepts three separately declared function contracts:

- `^p#`: mutate the payload only;
- `^#p`: replace the unique-handle slot only;
- `^#p#`: do either.

The two Toka rejection programs demonstrate that writing `#` at the call site
cannot manufacture the missing capability.  This is a compile-time source
contract, not merely a naming convention.

Rust's `&mut Cell` gives a payload-mutation view, while `&mut Box<Cell>` gives
access to the handle slot.  The Rust program proves that both call shapes are
ergonomic and safe.  However, because `Box<T>` implements mutable dereference,
`&mut Box<Cell>` also gives access to `Cell`'s payload.  Rust therefore does
not use the same two-bit binding notation to express a handle-only capability.
A Rust API can instead hide that authority behind a private newtype/module.

## Fair conclusion

Toka's advantage here is local auditability: replacement and payload-write
authority are independently visible in the binding and parameter syntax.
Rust's advantage is that ordinary `&mut` and encapsulation patterns are widely
understood and supported by a large library ecosystem.  This case does not
measure runtime cost, prove a general safety theorem, or claim that Rust cannot
model a handle-only API.

## Improvement opportunity for Toka

The rejection currently reports `E04571` as a morphology mismatch.  The
comparison makes clear that the helpful explanation is capability-oriented:
the call-site marker requested a missing payload or handle authority.  See
[`EXP-001`](../../IMPROVEMENT_BACKLOG.md#exp-001--make-hp-diagnostics-capability-aware)
and [`EXP-002`](../../IMPROVEMENT_BACKLOG.md#exp-002--avoid-misleading-unused-variable-warnings-for-payload-use).
