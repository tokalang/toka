# G implementation (not Accepted)

Design/authorization checkpoint: `ddb19870`. E is not started. Work remains on
the existing integration branch; no push, PR, tag or release is authorized.

Implemented so far:

- Exact generic-binder recognition records an abstract whole-value formal
  before specialization; the fact survives formal cloning. It is not derived
  from the resolved pointer kind or merely being inside a generic function.
- Direct generic cede/return and inferred local transfer retain full types.
  Checked expression/local facts are re-elaborated, not cloned as runtime roots.
- Direct whole-T borrowing preserves i32/unique/reference storage and the
  additional reference layer. A checked `return &parameter` storage-level fact
  is mapped through the explicit dependency route; it neither invents a route
  nor treats a local descriptor as its referent. Reference-value forwarding is
  separately tested and remains distinct from new descriptor borrowing.
- Whole-slot requests map to the existing H requirement for handle instances,
  separately from the transported T's internal P ceiling. Assignment does not
  implicitly decay such an operand into its payload.
- Lowering uses the caller's real captured slot and full resolved cleanup type
  for replacement, without granting scope ownership of a borrowed parameter.
- Opaque T does not acquire concrete fields or a concrete root selector merely
  because specialization reveals them. Concrete source bindings stay concrete.
- Compiler interface `0.9.9-20` rejects pre-G TKI/cache. Native handoff v1's key,
  layout and algorithm remain unchanged; the compatibility test explicitly
  verifies stale compiler-interface rejection and native link rejection.

The incremental `toka_whole_value_generics` test covers direct/local transfer,
scalar/unique/shared replacement and cleanup, internal writable-shared ceiling,
source-hidden relay, forbidden borrowed moves, payload/owner confusion, P/H
confusion and opaque field/root selection. There are six runtime/parity cases
(including the source-hidden program), eight rejection/parity cases with 16
object/IR no-artifact checks, and three exact caller-storage IR controls.
This is not the full G matrix.

Verification: `g-core-checkpoint.log` records the incremental G suite; the latest
combined selection before the final borrow additions was **6/7 CTests**
(`g-targeted2.log`, 88.98 s). The remaining legacy shared-parameter test is
still red and is not reclassified as a passing negative. The independent
interface/cache/native-link compatibility script passed (`g-interface.log`).

Still to implement before G qualification:

1. Abstract contract propagation through named generic structures, members,
   indices, associated types and call results, with G3's concrete-use boundary.
   The old rigid shape check is not yet removed.
2. Extend the direct borrow controls to generic members/indices, reference-valued
   replacement, aliases, branch/loop state, constraints and unknown facts.
3. Generic quote syntax removal, character-literal preservation, TKI/export
   synchronization and necessary library/interface-domain migrations. Existing
   quote code paths during this WIP are not a permanent dual-mode design.
4. Full targeted G matrix, then one integrated comparison. No full PASS/FAIL
   run has been performed during this implementation checkpoint.

Migration observations: the legacy shared-parameter matrix reads `.value`
directly from opaque T. It needs a valid declared observation contract, not an
exception to G3. Callback/trait trial migrations did not qualify (a Token trait
does not automatically apply to ~Token); those trial edits were removed rather
than relaxing trait or execution-boundary checks. The original matrix remains
as a required migration target. The aggregate-copy test's identical surviving-source content
check now runs in the concrete caller; retain-count and cleanup assertions are
unchanged. General shared ABI tests are not counted green until migrated.

Logs are in `/private/tmp/toka-permission-position.IcQTeC` (`g-*`). The RFC's
uncommitted §9 migration-criteria supplement was added concurrently and is left
separate from the implementation changes.
