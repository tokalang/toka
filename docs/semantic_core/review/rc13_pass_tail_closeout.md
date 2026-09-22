# RC13 appended-check closeout — WIP

Preceding TOML/Template/callable corrections were accepted at `8057bfab` and
the SDK-root routing correction at `99c89a88` (acceptance recorded in `216b2548`).
This is a separate, not-yet-accepted package. E, fn_, pushing and publishing
remain paused; the independent RFC worktree change is preserved.

## Purpose-preserving changes

- TKI cache and replay fixtures use ordinary T syntax. The shared memory-summary
  fixture explicitly declares its global i32 storage and mutable-call intent;
  its global-write effect assertions remain. Both memory-summary and readonly
  gates pass without compiler exceptions.
- Reference Vec tests use the existing consuming `appended` API with the declared
  mutable receiver; no reference-consuming path was enabled.
- Native-build replay uses full copied source SDKs, no outside symlinks, and
  isolated source/cwd roots for both build and process tests. Its qualification
  runner uses the selected TOKAC/TOKA, not a missing checkout-local build.
- `toka preview` now locates the script through the selected source SDK as well
  as the installed SDK layout. It was previously failing before analysis because
  the script was absent, not because two semantic documents disagreed.
- Removed `-> cede T` signatures no longer produce a return-transfer contract.
  The cede evidence test checks the actual `return cede p` plan, exact p identity,
  source invalidation and Return destination. Consumer replay still checks the
  explicit caller-transfer contract and double-consume/use-after-move rejection.
- Two shared-permission negatives now expect the earlier binding-stage
  AccessCapabilityMismatch, with a checked reason string in both source and
  replay modes. A generic missing-cede test checks E04570 before any move rather
  than requiring leaked E0438 state.
- Cache discriminators now construct a valid value on each side of associated-
  type changes, and explicitly narrow an i64 when the consumer returns i32.
  They still compare before/after contracts and actual rejection. Cache: 13/13.
- Static-string wrapper replacement is a positive. Distinct dynamic owner
  replacement negatives retain E0442, for both Wait and await. No overbroad
  wrapper loan was restored.
- Old morphic payload-peeking helpers are replaced by whole-T transport helpers
  with reference/non-reference domains. Concrete consumers read fields, methods
  and indices after obtaining the actual handle. Explicit shared/raw/reference
  copy controls retain content and shared exact-once checks. The unique negative
  reaches a genuine second explicit move in a generic helper (E0438).

## Bounded implementation corrections

- A reference-valued field selected directly from a checked call result uses
  that field's completed formal-to-actual mapping, not sibling dependencies.
  Referents remain storage-level for &T, including &str descriptors. Whole live
  non-consuming arguments, exact resolved field types and existing unknown-
  source exclusions are required for the newly connected field-address case.
  No raw/recursive container independence is inferred.
- Fresh whole unique-owner initialization applies OWN-FLOW-01's local H/P
  rebuild, not the old binding's local P as a permanent ceiling. It is limited
  to direct checked unique bindings; pointee type, represented restrictions,
  aliases, PAL, liveness, overlap and normal validation remain checked. Other
  transfer destinations are not broadened by this change.
- Await already had task-result proof projection. Binding fact consumption now
  includes Await alongside Wait; missing or cancellation-captured proof is not
  manufactured.
- Complete init and successful Outcome init update tracked field state as well
  as whole-place state. Err arms remain uninitialized. Strong alias Outcome
  identity uses the stable alias declaration, not the underlying Packet. The
  dedicated gate checks source/hidden replay, actual execution and Err-arm
  rejection, while retaining all original sidecar/body tamper checks.

## Verification and accounting

Development logs live under `/Users/zhyi/GitDP/tokalang/validation/rc13-tail-*`.
Eight related CTest passed (334.63 seconds) during development. A later dedicated
contract matrix covers descriptor lifetime, unique ceilings/PAL, selected-member
borrows and static/dynamic async sources; final results are recorded below once
the candidate is fixed.

The first appended-check development run restored 21/22 scripts from 13/22.
Semantic replay remained failing (48/56 cases at that snapshot). Subsequent
morphic fixture changes are not backfilled into those numbers. Original logs:
`validation/rc13-pass-tail-closeout-r1/results.json` and per-script logs.

Outstanding replay cases are retained, not blessed: source-hidden callable/task
result qualification, VecIntoIterator result storage, nested raw-backed resource
graphs and additional partial/async paths. An isolated bind-before-return Vec
probe still rejects IncompleteFacts; it was not applied to the SDK or its sealed
byte-contract fingerprint. These must not be called mere spelling migrations.

No new full baseline or package acceptance is claimed at this WIP checkpoint.
