# Container-carried external dependencies in PAL

This follow-up closes the in-function lifetime gap after `self <- val` has
recorded a value's external sources. The earlier receiver-effect and TKI
contracts remain the public boundary; PAL now enforces the mapped sources for
as long as a local complete value carries them.

## Responsibility chain

1. A successful receiver-effect call maps formal sources to actual binding IDs
   and joins them with the receiver's existing external sources. It registers
   shared PAL loans owned by that receiver binding. Unknown sources are not
   converted to an empty set.
2. Whole-value binding and assignment register or replace loans using the
   checked RHS facts. Field writes conservatively retain existing sources.
   A projected borrowed view is rebased through its carrier's external facts;
   an owned field's storage path remains its own source. Direct `&T` values
   keep using their existing PAL loans.
3. PAL rejects source invalidation, conflicting mutation, mutable borrowing
   and cede while a holder remains live. It tracks each holder separately, so
   retiring one of two holders leaves the other's loan intact.
4. A successful move retires the old holder's loan; the destination inherits
   the source set. A rejected call restores the PAL snapshot. Scope exit
   compares lexical scope depth and reverse declaration-order cleanup, then
   releases loans belonging to bindings in that scope.
5. `return <- self.external` and the existing return-source checks continue to
   carry the extracted value's external owner. A borrowed element newly made
   through `borrow()` still depends on container storage through its ordinary
   reference loan.

An unsafe raw-slot transfer is still a library invariant. The compiler does
not prove the allocation, initialized prefix or slot retirement. Internal
raw indexing does not invent a borrow of the slot address; the safe operation
must publish the value dependency through its declared contract.

## Acceptance probes

`test_unsafe_container_boundary_probe.py --mode production` covers the three
reviewed programs: owner cede, shorter owner scope and owner mutation while a
`Vec<str>` is live. It also checks aliases, two holders, branch joins, whole
assignment, hidden enum payloads, consumed-call rollback, extraction after
Vec destruction, and a shape with both owned storage and an unrelated
borrowed field. Each dangerous source must fail in normal and shadow checks,
with no object or LLVM IR output.

Run controls keep the owner live during access; retire the last holder before
owner mutation or destruction; move a holder without losing its source; use an
extracted view after Vec destruction while its owner remains live; and mutate
an unrelated owner or grow the Vec while its borrowed owner stays unchanged.
The existing owning nested-Vec and exact-once cleanup probes remain in the
same test entry point. `test_receiver_poststate_tki.py` checks source-hidden
method and direct-call owner mutation.

Dependency removal after `remove` or `clear` is still conservative. A raw
constructor with no usable external-source fact remains unknown to safe
receiver-effect calls; unsafe code is responsible for its storage preconditions.
This package is isolated from the `0.10.0` release line.
