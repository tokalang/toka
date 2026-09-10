# HashMap raw-element migration

Base: `93216ae610f46981afcf6ec75dea40c30e5aecfa`.
Status: implementation in progress, not Accepted.

## Responsibility inventory

Only `meta[i] == 1` authorizes the container implementation to take an
initialized key/value pair. Empty (0) and tombstone (2) slots are not read.
Key/value Vec lengths remain zero: their own drop frees storage, not elements.
The metadata Vec owns its initialized primitive prefix normally.

| Operation | Retired source | Recipient | Remainder / allocation cleanup |
| --- | --- | --- | --- |
| drop | Each occupied pair becomes tombstone before take | Local key/value cleanup | All other occupied slots still visited; Vec drops free buffers only |
| resize | Old occupied pair becomes tombstone before take | Locals, then new key/value slot; publish new meta only after both writes | New metadata describes relocated pairs; old zero-length Vecs free buffers |
| remove | Matching occupied pair becomes tombstone; decrement len once | Local key cleanup; value returned in Some | Other slots unchanged; None never takes anything |
| clear | Each occupied pair retired before take, then metadata becomes empty | Local key/value cleanup | Tombstones reset; len becomes zero; buffers retained for reuse |

The private raw-formal bridge checks the base and prepares the index before
the metadata retirement store. The take interval contains no allocation,
user-code invocation, retain, destructor or suspension. A tombstone marks the
whole pair retired; during sequential key/value extraction the implementation
still owes the pending other element. It must extract both before later user
operations. The bridge is an unsafe container implementation detail, not a
proof that arbitrary raw storage is initialized. Its second use on the value
does not reinterpret the already-retired metadata as a fresh source proof.

No compiler/Copy/raw_take rule is relaxed. Unknown element dependencies remain
rejected. Zero-filled storage is not treated as initialized. JSON and the
return-matrix integration remain positive recovery targets.

The marker is passed as its full raw handle (`*marker#: u8`), not as a plain
mutable value argument pointing at a raw-pointer binding slot. The first
prototype used the latter and failed repeated removal; IR showed a byte store
into the pointer-binding slot rather than the metadata byte. The library-only
raw-handle adaptation fixes that and the repeated-removal regression is kept.
No compiler addressing/ABI change is included.

## Clone authorization and implementation

At the first checkpoint, resource `HashMap<i32, Token>` instantiation checked
the unbounded existing `clone` method and failed at bare raw payload copying
even though the fixture never invoked clone. This blocker is now closed:
`lifecycle.tk` builds and runs, including 200 non-Dup resources, resize,
remove/repeated remove, clear/repeated clear, reuse and exact-once drop.

The user explicitly authorized aligning source-preserving clone with Vec's existing
`@Dup` contract: bound only the clone impl's K/V to Dup and use `.dup()` for
each occupied payload. The automatic reviewer rejected applying this change
because it changes generic admission and copying semantics beyond raw_take
migration. After that explicit additional authorization, the new implementation
imports `@Dup`, bounds only the clone impl, and duplicates each occupied key and
value. The HashMap type and all ordinary operations remain unconstrained.

Target metadata starts empty. Both duplication results are local owned values
before either is stored; occupied is published only after both stores. Empty
and tombstone metadata is preserved without reading their payloads. Source
metadata, length and slots are never written. Dup returns a value, not a
recoverable error result; no new unwinding or cleanup-on-fatal promise is added.
For a normal return, the target owns each completed pair and its existing
HashMap drop path cleans it. A fatal during allocation/dup retains the existing
process-termination semantics; this is not a new generic exception mechanism.

The JSON positive now exposes unproved recursive JsonNode dependencies in
raw_take and parser raw-buffer cleanup; the clone failure is gone. No corresponding
compiler guards or JSON source are changed. These remain visible blockers.

The scalar gate is deliberately named as a partial gate, not a full HashMap
qualification. The new clone gate independently checks the now-working resource
subset. Direct managed element types and JSON still prevent declaring the
whole HashMap migration complete.

## Current directed coverage

`toka_binding_b5_hashmap_clone` checks four runtime/parity fixtures:

- `lifecycle.tk`: NonDup resource ordinary use, including resize and exact-once;
- `clone.tk`: Copy values and explicit Dup keys/resources; empty, tombstone,
  resize, all copied contents, source preservation and exact destructor counts;
- `strings.tk`: owning key/value clones remain usable independently;
- `owned_handles.tk`: records containing unique/shared resources move through
  resize/remove/clear/drop with exact-once cleanup and a surviving shared owner.

Two NonDup clone negatives (key and value independently) require only E0417,
normal/shadow parity, and object/IR absence. Removing only the forbidden clone
line must produce a working ordinary container, not another error.

`handles.tk` remains a failed **direct managed-element positive**, distinct
from the passing owning-record controls. Current blockers include a morphic
helper-result binding mismatch and iterator `*^Token` / `&^Token` view handling.
Neither is hidden by assigning Copy/Dup or changing raw/managed rules.

## Checkpoint validation

- CTest **3/3**, 50.56 seconds: `toka_binding_b5_hashmap_scalar`,
  `toka_binding_b4_enum_copy`, and `toka_stage1_vec_pop`.
- The unchanged `tests/pass/g07_hashmap_resize_test.tk` builds and runs with
  exit 0, checking 2000 inserts, resize lookup preservation and iterator count.
- The new scalar regression covers empty/repeated removal, tombstone
  traversal/reuse, repeated clear/reuse and populated drop. Its IR checks the
  marker store precedes the typed take without intervening calls.
- At that checkpoint `lifecycle.tk` still failed in clone (now fixed as above).
  JSON remains failed. No full PASS/FAIL or return-matrix rerun.
- No compiler, Copy, raw_take, interface-key, runtime or thread ref changes.

This is a local **WIP checkpoint**, not a freeze or Accepted candidate.

## Authorized clone increment: final results

Final CTest **5/5**, **102.87 seconds**: B5 clone, B5 scalar, B4 enum Copy,
Vec pop and raw_take frontend/runtime/fault gates. The clone target passed its
four runtime/parity cases and both clone-only negative/control pairs. No full
PASS/FAIL suite was run; no oracle was changed to bless a failing positive.

Continued JSON diagnosis after clone:

1. The old HashMap bare-copy clone errors have disappeared.
2. `JsonNode::ArrayNode(Vec<JsonNode>)` and
   `JsonNode::ObjectNode(HashMap<string, JsonNode>)` contain internal raw
   storage. The current raw_take structural traversal rejects raw fields and
   recursive cycles; it does not carry an owning-container storage witness.
   The failure is `ElementDependenciesUnproven` in HashMap's bridge and Vec's
   existing bridge, not another enum Copy bug.
3. Parser cleanup at JSON lines 621/640 still uses `cede k_buf[0]`. The parser
   allocates and zeroes a generic K before calling its mutating parse method.
   Zero fill is not an initialization proof, and a mutation method's type alone
   does not certify a fresh independent owning result. Any migration must
   account separately for initialization, parse success/failure and cleanup.

No change to raw_take admission, no canonical-name exemption for JsonNode,
and no new generic parser initialization contract has been made. Further JSON
recovery cannot be reported as a mechanical completion of clone; these proof
and parser obligations remain explicit. The direct managed-element failed
positive is also retained, not substituted by the passing owning-record test.
