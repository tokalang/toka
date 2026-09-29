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
   keep using their existing PAL loans. Callable values keep using the
   callable-environment capture and replacement protocol.
3. PAL rejects source invalidation, conflicting mutation, mutable borrowing
   and cede while a holder remains live. It tracks each holder separately, so
   retiring one of two holders leaves the other's loan intact.
4. A successful move retires the old holder's loan; the destination inherits
   the source set. A rejected call restores the PAL snapshot. Scope exit
   simulates CodeGen's reverse declaration-order cleanup. It checks each
   source before retiring it, then releases each completed holder's loans.
   Longer-lived holders always constrain a source; a same-scope holder does
   so when its destructor may read the carried borrow. An empty destructor
   body does not count as a read, while nested elements with a potentially
   reading destructor do. Each return, break, continue, pass or propagation
   edge simulates its own unwind from the post-expression PAL state. The
   simulation uses a ledger copy, so the continuing branch retains its
   original loans and later statements cannot justify an earlier exit. A
   projected source is checked for storage invalidation by its resolved
   ownership type: retiring a copied borrowed-view descriptor does not free
   its external referent. Unknown projections remain conservative.
5. `return <- self.external` and the existing return-source checks continue to
   carry the extracted value's external owner. A borrowed element newly made
   through `borrow()` still depends on container storage through its ordinary
   reference loan.

`Vec::with_capacity` explicitly returns an empty initialized prefix, so its
external element set is empty even for a borrowing `T`. A consuming `appended`
call maps its moved receiver's logical external sources through whole-value
assignment; the old Vec slot is not an element owner. Existing `Vec<&T>`
iteration and reference alias operations remain supported, while returning a
Vec that acquired a local `&T` is rejected.

The exact `std/vec` source seal used by the ByteBuffer/TaskResult evidence
path is refreshed for this reviewed constructor-contract change. The
constructor's allocation and initialized-prefix algorithm is unchanged.

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
borrowed field. Same-scope custom-drop cases check bad order at ordinary
block exit, conditional return, loop break/continue and propagation. Good-order
and early-holder-retirement controls run the destructor and check its exact
count; descriptor-only cleanup remains accepted. Each dangerous source must
fail in normal and shadow checks,
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
receiver-effect calls and whole-value assignment; unsafe code is responsible
for its storage preconditions. Such an assignment rejects before consuming its
source.
This package is isolated from the `0.10.0` release line.
