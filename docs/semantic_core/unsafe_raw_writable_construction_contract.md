# Explicit unsafe writable raw construction

Status: Design authorized; implementation in progress, integration not qualified

Latest validation and scope blocker:
[unsafe_raw_construction_progress.md](review/unsafe_raw_construction_progress.md).

## Contract

The existing spelling `unsafe (addr as *T#)` is an explicit raw construction.
It requires a normal, successfully checked `as` conversion from semantic `Addr`
and an explicitly writable raw pointee in its target type. An unsafe block may
contain this explicit conversion; the block alone, an ordinary cast without
target P, or a writable destination binding is not a grant.

The capability origin is **UnsafeCallerPrecondition**, not compiler-proven
writable storage. The caller must ensure the address is valid for the intended
access, sufficiently aligned and in bounds, writable, and free of conflicting
access. `Addr` has no pointee permission; this is different from an address
known to originate from a readonly or frozen view.

Known source restrictions and known PAL conflicts remain compiler-enforced.
Conversions to Addr, copies, aggregate carriers, calls and control-flow joins
must not erase them. Unknown external addresses may rely on the caller
precondition; unresolved source-defined provenance must not silently become
an unrestricted external address.

This grants only raw-pointer P. It does not initialize storage, acquire
ownership, create Drop liability, extend a lifetime, establish a safe borrow,
or discharge a cleanup obligation. Existing nullable/nonzero rules remain.
Known nullable provenance is retained; casting through Addr is not a guard.

## Validation and lowering

Sema attaches an exact-edge plan recording source/target types, explicit
conversion/unsafe eligibility and capability origin. Known restrictions are
checked before final qualification. Forward function-return dependencies may
be resolved before CodeGen, but cannot remain unresolved in a qualified plan.
CodeGen validates the carrier and lowers the existing conversion exactly once;
it does not reconstruct provenance or infer permission from the target type.
Missing, rejected, incomplete or mismatched plans produce E0701 without an
object or IR artifact. Fault injection is test-build-only.

Restriction propagation may conservatively retain a known restriction across
uncertain writes or aggregate aliases. Such retention is a rejection boundary,
not permission to invent a stronger source fact. A failed statement must not
commit source/target changes.

## Qualification before thread migration

- Real allocated storage: construction, write, readback and cleanup.
- No unsafe / no explicit target P / destination-only P rejection.
- Readonly or frozen origin through Addr and copies/calls/aggregates.
- Known PAL conflict, nullable source and guarded/non-null controls.
- Missing/wrong/rejected/incomplete plan: E0701 and no artifacts.
- Normal/shadow diagnostic parity; existing binding and lifecycle gates.

Only after these gates pass may thread construction sites migrate. No
allocation API redesign, ABI, capture or reference-counting change is included.
This authorization is not acceptance of the complete binding slice.
