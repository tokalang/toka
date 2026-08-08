# Current-HEAD Release Qualification Triage

**Status:** Closed bounded conformance requalification. This document records
the failure inventory opened at `e459febb`, after the frozen P0.4 exact-place
migration, and its current-HEAD closure. It is not a replacement for the
historical thirteen-stage package release gate.

## Gate result

The opening run of `python3 tools/run_conformance.py` completed with **203
passed / 23 failed**. After the two behavior repairs and the verified
diagnostic baseline updates below, the same command completed with **226
passed / 0 failed**.

The closure also includes a green `ctest --test-dir build --output-on-failure`
(9/9). The P0.4 focused matrix remains separately green: delayed-init, Outcome
body recheck, bounded partial-`cede` cleanup, and source-less semantic replay.

## Original failure classes

| Class | Count | Required disposition |
|---|---:|---|
| compile-fail error-code mismatch | 1 | resolved after verifying the type-ascription rule |
| compile-fail source-span mismatch | 20 | resolved after verifying unchanged primary safety codes and exact current coordinates |
| run behavior failure | 2 | resolved before accepting a diagnostic baseline update |

### Original compile-fail error-code mismatch

- `diag_none_is_not_option_none_01`

### Original compile-fail source-span mismatches

- `diag_cede_fixed_array_index_borrow_conflict_01`
- `diag_match_reference_fixed_index_cede_conflict_01`
- `diag_guard_reference_fixed_index_cede_conflict_01`
- `diag_nested_match_reference_field_cede_conflict_01`
- `diag_nested_guard_reference_field_cede_conflict_01`
- `diag_destructure_reference_field_cede_conflict_01`
- `diag_for_reference_fixed_array_cede_conflict_01`
- `diag_static_cede_parameter_requires_explicit_transfer_01`
- `diag_callable_cede_parameter_requires_explicit_transfer_01`
- `diag_cede_unique_field_receiver_custom_drop_rejected_01`
- `diag_cede_fixed_array_index_invalidates_source_01`
- `diag_cede_fixed_array_index_branch_join_invalidates_source_01`
- `diag_cede_fixed_array_index_match_join_invalidates_source_01`
- `diag_cede_dynamic_resource_array_index_rejected_01`
- `diag_cede_direct_field_match_join_invalidates_source_01`
- `diag_cede_direct_field_cast_invalidates_source_01`
- `diag_cede_direct_field_custom_drop_rejected_01`
- `diag_cede_direct_field_cast_custom_drop_rejected_01`
- `diag_aggregate_owned_field_requires_cede_01`
- `diag_aggregate_owned_field_projection_requires_cede_01`

### Original run behavior failures

- `ownership_cede_nullable_member_explicit_nullable_destination_01`
- `std_owned_lazy_iterator_01`

### Resolved during this requalification

- `std_owned_lazy_iterator_01`: the fixture used bare identifier patterns,
  which current Toka treats as references to existing bindings.  Its five
  fresh `Some` payload bindings now spell `auto value`; the focused run test
  passes.  This changes neither iterator nor ownership semantics.
- `ownership_cede_nullable_member_explicit_nullable_destination_01`: the
  nullable-destination check omitted the type ascription nested under the
  `cede` AST node.  `cede source:T?` now admits that explicit nullable
  contract; the corresponding explicit `:Token` negative case still reports
  `E04599`.
- The twenty source-span fixtures retain their original primary safety error
  codes. Their manifest coordinates now name the actual primary diagnostic
  locations verified by direct compilation.
- `diag_none_is_not_option_none_01`: `none:Option<i32>` remains rejected
  because payload `none` is not `Option::None`; `E04606` is the more precise
  current type-ascription mismatch in place of the older declaration-level
  `E0409` expectation.

## Closure evidence

1. Each of the 21 original compile-fail cases was directly compiled to capture
   its primary error code and source coordinate before the manifest changed.
2. The two run failures were repaired and separately re-run; the explicit
   non-null nullable-`cede` negative counterpart still reports `E04599`.
3. The final 226-case conformance manifest and the nine CTest targets are
   green at this revision.

This closure reopens the planned P0 exact-place/permission-flow work. It does
not itself authorize an async/runtime expansion, Outcome surface expansion,
bodyless witness trust, or broader projection admission.
