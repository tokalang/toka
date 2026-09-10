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

## Separate authorization blocker

Resource `HashMap<i32, Token>` instantiation still checks the unbounded existing
`clone` method. Its raw bare-copy initialization fails at the value field even
though the fixture never invokes clone. `lifecycle.tk` is a **failed positive**,
not a negative gate or a skipped success.

Proposed follow-up is to align source-preserving clone with Vec's existing
`@Dup` contract: bound only the clone impl's K/V to Dup and use `.dup()` for
each occupied payload. The automatic reviewer rejected applying this change
because it changes generic admission and copying semantics beyond raw_take
migration. That diff is **not applied**; the import, impl bounds and clone
body remain unchanged. Explicit additional authorization is needed before
that change or an alternative public clone policy is implemented.

The JSON positive now also exposes unproved recursive JsonNode dependencies in
raw_take, parser raw-buffer cleanup, and the same clone path. No corresponding
compiler guards or JSON source are changed. These remain visible blockers.

The scalar gate is deliberately named as a partial gate, not a full HashMap
qualification. Resource/unique/shared exact-once and JSON must pass before
this migration can be presented as a complete candidate.

## Checkpoint validation

- CTest **3/3**, 50.56 seconds: `toka_binding_b5_hashmap_scalar`,
  `toka_binding_b4_enum_copy`, and `toka_stage1_vec_pop`.
- The unchanged `tests/pass/g07_hashmap_resize_test.tk` builds and runs with
  exit 0, checking 2000 inserts, resize lookup preservation and iterator count.
- The new scalar regression covers empty/repeated removal, tombstone
  traversal/reuse, repeated clear/reuse and populated drop. Its IR checks the
  marker store precedes the typed take without intervening calls.
- `lifecycle.tk` still fails in existing clone; it is not included among the
  passing gates. JSON remains failed. No full PASS/FAIL or return-matrix rerun.
- No compiler, Copy, raw_take, interface-key, runtime or thread ref changes.

This is a local **WIP checkpoint**, not a freeze or Accepted candidate.
