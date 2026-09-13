# Explicit unsafe writable raw construction

Status: Design authorized; implementation in progress, integration not qualified

Current name-side construction validation:
[permission_position_wip.md](review/permission_position_wip.md).
The earlier [progress report](review/unsafe_raw_construction_progress.md) records
the preceding type-side spelling and is not the current migration result.

## Contract

The authorized declaration spelling is `auto *p# = unsafe (addr as *T)`.
`unsafe { auto *p# = addr as *T }` is equivalent. It requires all three:
the name-side payload-write request, a successfully checked explicit conversion
from semantic Addr to raw, and unsafe context. The binding marker is a request,
not the proof. An unsafe block alone, a marker alone, an ordinary numeric value,
or a readonly raw source does not satisfy this construction contract.

Sema associates the request with the declaration's direct conversion edge.
It must not transmit expected writability into arbitrary nested expressions,
calls, branches or inner casts. CodeGen validates that exact association.

Unnamed handle/reference view types can describe internal capabilities, such as
`&i32#` or `Slot<&i32#>`. Such a type description does not grant authority to a
cast. Named declaration permissions belong on the name/hat, not bare `T#`.
The existing return-signature exception and separate `fn#` protocol remain.
`'T` preserves the complete internal view without changing outer binding,
member or handle-rebind declarations; outer writability cannot upgrade a
readonly inner view. This does not qualify Vec element dependency propagation.

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
- Name-side request plus explicit unsafe Addr construction is a positive case
  (including the former `no_target_write.tk`); test actual write/read/cleanup.
- No unsafe, no explicit Addr conversion, readonly raw, and ordinary integer
  sources remain negative. Unnamed capability descriptions alone are not grants.
- Readonly or frozen origin through Addr and copies/calls/aggregates.
- Known PAL conflict, nullable source and guarded/non-null controls.
- Missing/wrong/rejected/incomplete plan: E0701 and no artifacts.
- Normal/shadow diagnostic parity; existing binding and lifecycle gates.

Only after these gates pass may thread construction sites migrate. No
allocation API redesign, ABI, capture or reference-counting change is included.
This authorization is not acceptance of the complete binding slice.
