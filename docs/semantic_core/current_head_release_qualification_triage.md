# Current-HEAD Release Qualification Triage

**Status:** Active bounded requalification. This document records the full
conformance result at `e459febb`, after the frozen P0.4 exact-place migration.
It is a failure inventory, not a decision to alter language behavior or test
expectations.

## Gate result

`python3 tools/run_conformance.py` completed with **203 passed / 23 failed**.
The P0.4 focused matrix remains separately green: CTest, delayed-init, Outcome
body recheck, bounded partial-`cede` cleanup, and source-less semantic replay.
That evidence does not substitute for a current-HEAD release qualification.

## Failure classes

| Class | Count | Required disposition |
|---|---:|---|
| compile-fail error-code mismatch | 1 | identify whether the semantic diagnostic or manifest expectation is wrong |
| compile-fail source-span mismatch | 20 | verify the primary diagnostic code and source coordinate before changing either compiler or manifest |
| run behavior failure | 2 | reduce to the governing semantic rule; no fixture change until the implementation/expectation decision is explicit |

### Compile-fail error-code mismatch

- `diag_none_is_not_option_none_01`

### Compile-fail source-span mismatches

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

### Run behavior failures

- `ownership_cede_nullable_member_explicit_nullable_destination_01`

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

## Execution order

1. Reproduce each class with its one manifest fixture and capture the primary
   diagnostic/running behavior.
2. Resolve the two run behavior failures before accepting any fixture update.
3. Treat a compile-fail fixture as a manifest-only update only after its
   expected safety rule and actual diagnostic code agree; otherwise repair the
   compiler and add a focused regression test.
4. Re-run the affected class after every commit, then the full manifest at the
   candidate revision. Only a green full manifest closes this requalification.

No task in this ledger authorizes a new async/runtime slice, Outcome expansion,
bodyless witness trust, or broader projection admission.
