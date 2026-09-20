# RC13 Thread Spawn Boundary Captures Review & Acceptance (`83a16292`)

Candidate: `83a1629211300343c066dde3f3d2a9966e2307e2`
Implementation commits: `2377c337`, `83a16292`
Parent: `ae1e595d` (Slab Accepted at `f006ce8f`)

## 1. Scope & Decision

**Accepted**: `83a16292` formally closes the five declared `thread_spawn_*` execution boundary refusal purposes, their paired legal positive controls, the compiler-synthesized declaration origin guards, and their CTest gate wiring.

This package:
- Reconciles the five `tests/fail/thread_spawn_*` fixtures to modern closure syntax without thin-fn (`fn() -> i32`) ascriptions.
- Preserves genuine refusal diagnostics: `E0478` (non-`Sync` copy capture), `E04582` (implicit boundary capture across binding, nesting, and assignment), `E0477` (non-`Send` capture).
- Restricts synthetic closure shape property rules (`hasDrop`, `queryExplicitCedeStage0OwnershipReadOnly`, `queryExplicitCedeStage0CopyProof`) strictly to `ShapeDecl::IsCompilerSynthesized && IsClosureInvoke`, preventing ordinary user shapes named with `__Closure_` prefix from losing drop semantics or triggering `ContradictoryFacts`.
- Pairs each negative purpose with a 1:1 legal positive control in `tests/semantics/std_thread_handoff/` that performs real OS thread spawning, join, result validation, and exact-once drop assertions.
- Wires all five positive controls into the existing `toka_public_thread_responsibility_subset` CTest (`tools/scripts/test_std_thread_handoff.py`).
- Updates `tests/semantics/rc13_negative_purposes/manifest.json` from `blocked` to `repaired` for all five cases (29/31 repaired purposes verified in CTest).

## 2. Five-Case Classification & Diagnostics

| Fixture | Prior Blocker | Root Cause | Target Diagnostic | Resolution |
|---|---|---|---|---|
| `thread_spawn_copy_non_sync_capture.tk` | `E0448` pointer mismatch, `E0476` no capture summary | Obsolete thin-fn ascription, struct pointer init syntax, missing closure CopyProof | `error[E0478]` | Correct `*ptr = *ptr`, remove `:fn() -> i32`; resolve closure shape CopyProof in Sema |
| `thread_spawn_implicit_capture_nested_closure.tk` | `E04606` ascription mismatch, `E0476` no capture summary | Obsolete thin-fn ascription, consuming invoke syntax, missing boundary summary propagation | `error[E04582]` | Invoke with `cede borrowed()`, remove `:fn() -> i32`; propagate `BoundaryImplicitCaptures` across nested closure declarations |
| `thread_spawn_implicit_capture_via_assignment.tk` | `E04606` ascription mismatch, `E0406` ActualInvokeUnproven | Obsolete thin-fn ascription, distinct closure literal types | `error[E04582]` | Reassign isomorphic closure `g = cede f`; propagate and retain boundary capture facts across assignment |
| `thread_spawn_implicit_capture_via_binding.tk` | `E04606` ascription mismatch, `E0476` no capture summary | Obsolete thin-fn ascription, missing closure Stage1 callable facts | `error[E04582]` | Remove `:fn() -> i32`; bridge callable environment facts for synthesized closure shapes in `Stage1BindingTransfer::prepare` |
| `thread_spawn_non_send_capture.tk` | `E04661` IncompleteFacts, `E0476` no capture summary | Obsolete thin-fn ascription, missing closure ownership/drop deduction | `error[E0477]` | Remove `:fn() -> i32`; deduce closure drop and ownership from members and invoke receiver |

## 3. Paired Legal Controls & Gate Wiring

All five paired legal controls run in `tools/scripts/test_std_thread_handoff.py` under CTest `toka_public_thread_responsibility_subset`:
1. `thread_spawn_copy_sync_capture.tk`: tests `@Send + @Sync` type (`SyncToken`) copy-captured across thread boundary; verifies concurrent read, join result 42, and parent source preservation.
2. `thread_spawn_nested_closure.tk`: tests nested closures explicitly transferring `[cede token]` and `[cede inner]`; executes in spawned thread, returns 55, and verifies `drops == 1`.
3. `thread_spawn_binding_capture.tk`: tests named closure binding transferring `[cede token]`; executes in spawned thread, returns 77, and verifies `drops == 1`.
4. `thread_spawn_send_capture.tk`: tests unique `@Send` resource (`SendItem`); executes in spawned thread, returns 88, and verifies `drops == 1`.
5. `thread_spawn_assignment_capture.tk`: tests closure reassignment `g = cede f` followed by thread worker forwarding; executes in spawned thread and returns 99.

## 4. Origin Guarding (`IsCompilerSynthesized`)

To prevent ordinary user-declared shapes named `__Closure_*` from matching closure rules:
- `Sema::hasDrop`: checks `shapeIt->second->IsCompilerSynthesized` and `IsClosureInvoke`. Ordinary shapes named `__Closure_Fake` with explicit drop return `true`.
- `queryExplicitCedeStage0OwnershipReadOnly`: checks `shape->IsCompilerSynthesized && shape->Name.rfind("__Closure_", 0) == 0`.
- `queryExplicitCedeStage0CopyProof`: restricts `isSyntheticClosure` to `shape->IsCompilerSynthesized && shape->Name.rfind("__Closure_", 0) == 0`.
- Verified with `normal_constructor.tk`, `spoof_constructor.tk`, `tests/pass/closure_name_spoof_drop.tk`, and `tests/fail/closure_name_spoof_copy_rejected.tk`.

## 5. Test Evidence

- Full FAIL Suite (`test_verify_fail.py`): **478 / 479 PASS** (only `smart_ptr_from_stack.tk` remaining).
- Related CTest gates: **6 / 6 PASS** (163.21s independent run; 230.79s implementation run):
  - `toka_rc13_negative_purposes`: Passed
  - `toka_channel_storage`: Passed
  - `toka_public_thread_responsibility_subset`: Passed (13 parity fixtures, 27 runtime executions)
  - `toka_native_sync_managed_slot`: Passed
  - `toka_for_alias_chain`: Passed
  - `toka_slab_chain`: Passed
- Historical full PASS (433/457) and full CTest (112/114) baselines remain recorded without backfilling.
- Unrelated RFC (`docs/semantic_core/whole_value_generics_and_checked_dependency_elision_rfc.md`) untouched (SHA-256: `1b73b0fc...`).
