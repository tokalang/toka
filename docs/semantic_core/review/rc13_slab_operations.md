# RC13 Slab Operations, Borrow Constraints and Regression Suite Acceptance

Accepted implementation: `f006ce8fd40ee3181a99289d29e66b477e70f074`.

Scope: bounded Slab operations / lookup-remove borrow slice, private Option take and reset protocol, whole-value generic pattern borrow boundaries (scalar, unique, shared, reference four-quadrant matrix), checked-in regression package (`tests/semantics/slab/`), and verified LLVM IR oracles in `tools/scripts/test_slab_chain.py`.

This accepts this slice, not RC13, Phase E, publication, or the remaining baseline failures.

## Acceptance and boundary

The candidate is accepted with no further review iterations required for Slab. Its changes relative to the prior accepted baseline (`8ea9850f` / `44eddb7a`) are:
- `src/Sema/Sema_Expr_Init.cpp`: preserve abstract whole-value generic contract and reference morphology under pattern destructuring without collapsing to inner referent types.
- `src/CodeGen/CodeGen_Expr.cpp`, `src/CodeGen/CodeGen_Stmt.cpp`: bind enum slot addresses directly for reference-valued generic pattern targets, avoiding invalid extra referent loads.
- `tests/semantics/slab/`: 11 checked-in regression fixtures covering lifecycle exact-once destructors, generation reuse, live-loan removal/clear prevention, ended-loan reuse, local escape refusal, read-only mutation refusal, and four-quadrant generic borrow probes.
- `tools/scripts/test_slab_chain.py`: dedicated test gate checking normal/shadow parity, negative error codes and locations, absence of artifacts on error, exact slot zeroing offset proof, reaching unique-store / no-overwrite verification, and synthetic negative IR mutators.
- `CMakeLists.txt`: registration of `toka_slab_chain` in CTest (total 114 tests).

Neither RFC was modified (`docs/semantic_core/whole_value_generics_and_checked_dependency_elision_rfc.md` remains strictly untouched with hash `1b73b0fc46f9...`). No ABI, calling convention, or runtime container-proof rewrites were introduced.

## Verified complete results

All tool targets, PASS, FAIL, and full unfiltered CTest were executed and independently audited:

| Suite | Baseline (`8ea9850f`) | Candidate (`f006ce8f`) | Net Difference | Notes |
| --- | --- | --- | --- | --- |
| PASS (`test_pass.sh`) | 431 / 457 | **433 / 457** | **+2 recovered**, 0 added failures | 623s. Recovered: `g07_slab_test.tk`, `g18_slab_lookup_miss.tk` |
| FAIL (`test_verify_fail.py`) | 471 / 478 | **472 / 478** | **+1 recovered**, 0 added failures | Recovered: `slab_lookup_miss_blocks_remove.tk` |
| Unfiltered CTest | 111 / 113 | **112 / 114** | **+1 new gate**, 0 added failures | 782.52s. New gate `toka_slab_chain` passed (30.95s). Same 2 baseline failures |

Independent reviewer gate verification: `ctest` for `toka_slab_chain`, `toka_for_alias_chain`, and `toka_native_sync_managed_slot` passed **3 / 3 in 99.93s** (and 78.24s in reviewer's independent run). All four synthetic IR counterexamples reject with expected diagnostics.

## Remaining RC13 baseline blockers

1. **FAIL Suite (6 remaining failures)**:
   - `smart_ptr_from_stack.tk`
   - `thread_spawn_copy_non_sync_capture.tk`
   - `thread_spawn_implicit_capture_nested_closure.tk`
   - `thread_spawn_implicit_capture_via_assignment.tk`
   - `thread_spawn_implicit_capture_via_binding.tk`
   - `thread_spawn_non_send_capture.tk`

2. **CTest (2 remaining failures)**:
   - Test 27: `toka_stage1_indirect_parameter_cede` (CallableReturnEnvironmentUnavailable)
   - Test 33: `toka_stage1_return_matrix` (ElementDependenciesUnproven)

3. **PASS Suite (24 remaining failures)**:
   - Remaining unmigrated or unproven test fixtures across BTreeMap, build-hybrid, TOML, Template, and async/closure areas.

## Next work package

Stop adding work to Slab. The next package prioritizes classifying and closing the five `thread_spawn_*` negative tests and their legal positive controls, without reopening the thread runtime protocol. Phase E, push, and release remain paused.
