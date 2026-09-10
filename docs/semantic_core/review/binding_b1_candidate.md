# B1 complete bounded candidate — submitted for review, not Accepted

Baseline: `7240711202a80481ddbc3a123da0013f94fad1a0`.
Implementation: `d67f6071` and `4142eebd`, on `impl/binding-b1`.
This closeout adds test-purpose repairs and baseline attribution, not new
compiler semantics. The complete B1 is submitted together; no helper is given
a new acceptance milestone.

## Delivered scope

- Preserve actual coordinates on generated default SourceLoc literals.
- Correct normally validated integer bitwise `~` result classification without
  changing real shared handles. Bind an explicit owning string before retaining
  its view. No whitespace/parser/formatter change or lifetime extension.
- Preserve resolved-call `result!` borrowed-value origins across whole-binding
  replacement, with actual dependency mapping, final validation and rollback.
  Keep owning success/error cleanup exact-once through the existing lowering.

The implementation scope and prior targeted evidence are recorded in
`binding_b1_scope.md`. No library, thread implementation, return rule, protocol,
interface key or ABI change is included in B1.

## Restoring the tests' original purposes

### Method

`noncede_failure_out_of_slice.tk` now invokes the rejected method as an
expression statement. It therefore actually observes the method's legacy
out-of-slice failure semantics (E04510 plus E0438/E0410), without an enclosing
initialization transaction undoing the method's state change.

The added `noncede_failure_binding_rollback.tk` preserves that enclosing
initialization scenario and requires only E04510, with the resource restored.
Both have strict normal/shadow parity; the isolated method also has strict
historical-replay parity. No diagnostic expectation was weakened.

### Indirect fn/dyn fn

- The unique-return closure uses canonical `^cell`, not `cede ^cell`.
- Its plain callable invocation no longer incorrectly spells an explicit cede
  of the resulting temporary. The actual bare `^source` parameter remains the
  rejected operation; the emitted fix must insert `cede` before that hat, and
  applying that parameter fix must compile successfully.
- Multi-argument and nested-source closures bind their consumed scalar values
  before arithmetic. This prevents a return-expression restriction from
  intercepting the intended caller-side handshake/rollback test.
- The consuming-receiver rollback case now declares an actual `cede fn` and
  explicitly consumes the restored receiver in the succeeding control call.
  It no longer tries to read a binding whose initializer already failed.

The original source-hidden factory positive remains in the script, still with
its success/indirect-diagnostic expectation. It is **not** replaced with a
negative or removed. It currently blocks the full gate with
`CallableReturnEnvironmentUnavailable` before the indirect invocation. The
identical producer/hidden-consumer probe fails that way at the exact baseline
too; this broader callable environment integration is not repaired by B1.

### Return matrix

`rebound_reference_unknown.tk` keeps a rebindable handle but removes the
unneeded writable-payload declaration. The raw cast no longer first requests
permission amplification; the test reaches `return &reference` and retains
its original E0455 expectation. Normal/shadow rejection and non-production of
artifacts remain required. The script explicitly rejects interception by
E04661 in this case.

The gate now progresses to an existing Arena integration failure at
`lib/std/arena.tk:51` (`AccessCapabilityMismatch`). The original Arena positive
has not been removed, marked passing, or rewritten to bypass the library. An
independent `72407112` build reproduces the same failure. Later assertions in
that script must not be described as having run after this early failure.

## Separate view-return defect

See `local_view_return_baseline.md` and `b1_view_return_probe.tk`.
Both baseline and B1 accept the same local-owner view return and emit identical
function IR, including owner cleanup before returning the view. This is an
open baseline release-safety issue, not a B1 regression or a safe positive.
No undefined-behavior runtime result is used as evidence and return rules are
not reopened here.

## Final integration methodology

`tools/scripts/compare_binding_b1_integration.py` builds and uses each revision
with its own source library, runs complete PASS/FAIL lists without exclusions
or blessing, and runs plain CTest. It records commands, results, durations,
baseline revision, candidate tracked-diff fingerprint and probe IR hashes.

Versions are serialized: the historical PASS runner uses the shared absolute
directory `/tmp/tokac_tests`. The first unfinished parallel attempt was stopped
after this isolation flaw was discovered. Its partial PASS output is invalid
and is not a baseline, recovery count or completed verification. The probe
comparison was independently reproduced in the valid sequential run.

Authoritative evidence directory: `/private/tmp/toka-b1-final-serial`.
The abandoned partial run is `/private/tmp/toka-b1-final-4142eebd`.
Both complete sequential runs have finished. This is local Debug validation
with LLVM 20.1.8, not four-platform release qualification.

| Gate | Baseline `72407112` | B1 candidate | Change |
| --- | ---: | ---: | --- |
| Compiler and tools build | PASS | PASS | No build regression |
| Complete PASS suite | 315/451 | 318/451 | 3 restored; 0 new failures |
| Complete FAIL suite | 422/473 | 422/473 | Same 51 failing expectations |
| Complete CTest | 61/66 | 65/69 | Method restored; 3 new B1 gates pass |

No original CTest was removed or disabled. The three additions are
`toka_binding_b1_default_args`, `toka_binding_b1_scalar_view`, and
`toka_binding_b1_propagation`. CTest wall times were 609.63s and 667.81s;
process wall times (including runner overhead) are in `metadata.json`.

The restored PASS cases are exactly:

- `g03_bitwise.tk`
- `g03_chain_static.tk`
- `g03_default_args.tk`

All 136 baseline and 133 remaining candidate PASS failures were separately
checked for their frontend diagnostic, without rerunning their runtime suite.
Every one still exits 1 in frontend checking: no remaining failure was
converted to an observed compiler crash or runtime failure. Among the 133
remaining names, 122 retain the same first diagnostic signature; all other
11 move from JSON line 529 (`IncompleteFacts`) to line 546
(`ContradictoryFacts`). These 11 are **not** counted as restored:

`g03_test_dynamic_json.tk`, `g03_test_json_depth.tk`, `g07_serde_test.tk`,
`g07_test_json_scientific.tk`, `g07_test_json_serde.tk`,
`g07_test_json_unicode.tk`, `g08_json_generic_failure_safe.tk`,
`g10_build_hybrid_test.tk`, `g10_test_sentry_benchmark.tk`,
`g16_service_telemetry_test.tk`, `g17_stdx_yaml_test.tk`.

The negative-suite failure names are identical. The only output differences
are generated numeric `__Closure_...` identifiers in three thread implicit-
capture diagnostics. With those identifiers normalized, the full negative
output is identical. No unexpected compilation success or abnormal compiler
exit is reported by either run. The 51 unmet expectations remain unresolved;
none is blessed merely because the compiler still rejects the program.

### Four retained CTest failures

| Target | Candidate blocker | Baseline attribution |
| --- | --- | --- |
| `toka_stage1_indirect_parameter_cede` | Hidden factory result: `CallableReturnEnvironmentUnavailable` | Independently reproduced with the same provider/consumer on `72407112`; original positive retained |
| `toka_stage1_return_matrix` | Arena line 51: `AccessCapabilityMismatch` | Same original Arena positive fails on `72407112`; original positive retained |
| `toka_call_transfer_shadow_m1` | `g09_sync_condvar.tk:39`, `EnvironmentLifetimeUnproven` | Same failing input, location and diagnostic in the full baseline |
| `toka_signature_driven_cede_remaining_routes` | `thread_state_runtime.tk:28`, `EnvironmentLifetimeUnproven` | Same failing input, location and diagnostic in the full baseline |

These targets are still red. Restoring an earlier test's purpose does not
establish that later, formerly masked integrations pass. Neither these failures
nor the local-view return defect are silently attached to the accepted thread
slice or repaired outside B1's scope.

The candidate's compiler/library code is unchanged from `4142eebd` during this
closeout. Test migrations were fixed before the valid complete comparison.
The final local commit also records documentation produced after verification;
it is not an additional behavior change or an acceptance decision.

Evidence files under the authoritative directory:

- `metadata.json`: revisions, tracked test-diff fingerprint, commands/times,
  identical-input view IR hashes, hidden factory and Arena probes.
- `comparison.json`: complete failure-name sets and deltas.
- `frontend-comparison.json`: all 11 changed first diagnostics and 3 recoveries.
- `baseline/pass-first-errors.json`, `candidate/pass-first-errors.json`, and
  their `failed-pass-logs/`: supplemental frontend attribution, not runtime retries.
- `negative-comparison.json` and `negative-output.diff`: exact negative-output differences.
- Per-version `tools-build.log`, `pass.log`, `fail.log`, `ctest.log`.

## Scope and repository boundary

No JSON raw-buffer repair, Arena/raw permission change, networking/container
expansion, receiver/InvokeExpr activation, or thread rework is included.
`freeze/thread-sync-7ef33d9d`, `impl/std-thread-handoff-v1` and `stabilize/rc13`
remain unchanged. No push, PR or Actions run is requested by this candidate.
