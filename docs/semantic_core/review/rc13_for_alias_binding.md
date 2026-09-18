# RC13 for-alias binding and cleanup chain acceptance

Accepted implementation: `8ea9850f876c3c6e52cf2ed3965f8dad9b7d3405`.

Scope: the reviewed for-alias place/slot binding, handle identity permissions, array iterator targets, array borrow provenance, preservation of original parameter identity under lexical shadowing, cascading drop of overwritten unique/shared payload slots on rebind, checked-in audit fixtures 1-11, parser token-underflow fix (`Parser_Stmt.cpp` / `Parser.cpp`), and test runner process-level diagnostics (`test_binding_b1_scalar_view.py`, `test_b6_recorded_slots.py`).

This accepts this slice, not RC13, Phase E, publication, or the remaining baseline failures.

## Acceptance and boundary

The candidate is accepted without further review iterations. Its changes relative to the prior accepted baseline (`7477d4f7` / `e361b202`) are:
- `src/Sema/Sema_Expr_Call.cpp`: for-alias place alias slot binding, permission separation, array iterator targets, unique/shared drop cascade.
- `src/Sema/Sema_Stmt.cpp`: parameter provenance verification prioritized over inner lexical shadowing (`isParamDependency`).
- `src/Sema/ExplicitCedePlan.cpp`, `src/CodeGen/CodeGen_Expr.cpp`, `src/Parser/Parser_Expr.cpp`: slot handling and cleanup coordination.
- `src/Parser/Parser_Stmt.cpp`, `src/Parser/Parser.cpp`: removal of invalid `previous().Kind` lookbehind at token 0, and underflow guard in `previous()` / `peekAt()`.
- `tests/semantics/for_alias/`: audit probes 1-11 (including `saved_parameter_shadow.tk`, `saved_parameter_control.tk`, `shadowed_return.tk`, `renamed_return.tk`, `unique_cleanup_trace.tk`, `rebind_cleanup.tk`).
- `tools/scripts/test_for_alias_chain.py`: dedicated gate covering normal/shadow parity, negative error codes/lines, artifact absence, and positive execution.
- `tools/scripts/test_binding_b1_scalar_view.py`, `tools/scripts/test_b6_recorded_slots.py`: child process tracking with PID, return code, signal, and stdout/stderr reporting.

Neither RFC was modified (`docs/semantic_core/for_alias_binding_rfc.md` remains strictly frozen; `docs/semantic_core/whole_value_generics_and_checked_dependency_elision_rfc.md` remains unstaged and unmodified). No ABI, calling convention, or stack frame layout changes were introduced.

## SIGBUS root cause and resolution

In `Parser::parseVariableDecl(bool)` (`src/Parser/Parser_Stmt.cpp:32`), an `else if (previous().Kind != TokenType::KwAuto)` condition executed when `m_Pos == 0` for files beginning with a top-level `auto` declaration (such as `tests/semantics/binding_b1/scalar_and_shared.tk` and `tests/semantics/b6/recorded_slot_take.tk`). This accessed `m_Tokens[-1]`.

On 64-bit ARM macOS, `sizeof(Token) == 56` (`0x38`). When `m_Tokens.data()` was allocated at the start of a newly mapped virtual memory region (e.g., `0xbf8400000` or `0x9fa400000`), `0xbf8400000 - 0x38 = 0xbf83fffc8` landed in an unmapped guard page, causing `EXC_BAD_ACCESS (SIGBUS / KERN_PROTECTION_FAILURE)` at `ldr w8, [x0]`.

This historical bug originated in `v0.4.0` (`22f4d951`) and surfaced under high CTest concurrency. It was fixed by replacing the branch with `else { match(TokenType::KwAuto); }` and hardening `Parser::previous()` and `Parser::peekAt()` against underflow.

## Historical run separation

In accordance with strict audit accounting:
- **Historical pre-fix full CTest**: **109 / 113 Passed, 4 Failed** (`toka_stage1_indirect_parameter_cede`, `toka_stage1_return_matrix`, `toka_binding_b1_scalar_view`, `toka_b6_recorded_slots`).
- **Historical pre-fix isolated rerun**: **2 / 2 Passed** (`toka_binding_b1_scalar_view`, `toka_b6_recorded_slots`).
These results remain archived in `/Users/zhyi/GitDP/tokalang/validation/for-alias-final-review-20260918.sHNXaK/` and are not backfilled into each other.

## Verified post-fix complete results

All tool targets, PASS, FAIL, and full unfiltered CTest were executed on `8ea9850f`:

| Suite | Baseline (`7477d4f7`) | Candidate (`8ea9850f`) | Net Difference | Notes |
| --- | --- | --- | --- | --- |
| PASS (`test_pass.py`) | 425 / 457 | **431 / 457** | **+6 recovered**, 0 added failures | 105.57s. Recovered: 4x `g08_for_alias_*`, `for_mutable_binding`, `g03_diagnostic_test` |
| FAIL (`test_verify_fail.py`) | 470 / 478 | **471 / 478** | **+1 recovered**, 0 added failures | 114.73s. Recovered: `for_alias_shape_field_ceiling.tk` |
| Unfiltered CTest | 110 / 112 | **111 / 113** | **+1 new gate**, 0 added failures | 1132.70s. New gate: `toka_for_alias_chain` passed (43.39s). B1 (13.93s) and B6 (52.96s) passed cleanly in full concurrency. Same 2 baseline failures |

Independent reviewer gate check: `ctest -j2` for B1, B6, and for-alias passed **3 / 3 in 56.52s**.

## Remaining RC13 baseline blockers

1. **FAIL Suite (7 remaining failures)**:
   - `slab_lookup_miss_blocks_remove.tk` (Target of next work package)
   - `smart_ptr_from_stack.tk`
   - `thread_spawn_copy_non_sync_capture.tk`
   - `thread_spawn_implicit_capture_nested_closure.tk`
   - `thread_spawn_implicit_capture_via_assignment.tk`
   - `thread_spawn_implicit_capture_via_binding.tk`
   - `thread_spawn_non_send_capture.tk`

2. **CTest (2 remaining failures)**:
   - Test 27: `toka_stage1_indirect_parameter_cede`
   - Test 33: `toka_stage1_return_matrix`

3. **PASS Suite (26 remaining failures)**:
   - Includes Slab-related tests (`g07_slab_test.tk`, `g18_slab_lookup_miss.tk`).

## Non-blocking test harness maintenance

The new `Popen.communicate(timeout=...)` wrappers in `test_binding_b1_scalar_view.py` and `test_b6_recorded_slots.py` should be updated in routine maintenance to terminate and reap child processes when `TimeoutExpired` occurs. This does not reopen the for-alias acceptance.
