# Anonymous records + task/byte result handoff: scoped acceptance

## Accepted package — a08578e8

Implementation: `a08578e8ce62bae6eaabfa1cae00f03ade3de9fc`.
Accepted by incremental review on 2026-09-22, limited to anonymous records,
source-visible task-result projection and byte-owner/result handoff. The reviewer
rechecked the original probes and two CTests (2/2, 185.09 seconds), including the
37-case byte gate. The fixed full-run numbers below were independently reconciled,
not rerun in full by the reviewer. Acceptance record:
`/Users/zhyi/GitDP/tokalang/validation/byte-freshness-acceptance-20260922.P6sSd2/acceptance.md`.

This package is closed; no further features are appended. RC13 is not accepted.
Build plus return-matrix form the next separate library-side delivery. TOML,
Template, source-hidden callable and mutable aggregate extern lowering remain
explicit independent work. E, pushing and publishing stay paused. No freeze ref
is created or moved by this documentation-only acceptance record; the unrelated
RFC remains preserved.

This increment addresses the reviewed stale-receipt resurrection only. The
byte contract and the independent extern ABI work are not reopened.

The operation consumer and task-summary argument consumer now share one lookup:
a still-live receiver/place reads its **current** receipt after all arguments
have been checked. Transparent wrappers and checked field projections do not
permit falling back to the old expression annotation. A successfully checked
cede expression retains its transferred value receipt; an implicitly consuming
receiver can use its saved value only after normal Sema has actually retired
that place. Genuine temporaries remain values, not live receiver slots.
Task-summary fallback cannot bypass this distinction and resurrect the same
expired byte fact through the general independence query.

The original four audit files were checked unchanged in
`validation/rc13-byte-freshness-audit`: nested push, nested resize and sequential
mutation reject with E04661 / TaskResultOriginsUnproven in normal/shadow/object/
LLVM modes, with no object/IR output. None of the malformed programs was run.
The original valid nested control compiled and executed successfully.

Ten regressions extend the existing byte gate from 27 to **37 source cases**:
the nested/sequential refusals, member/alias mutation, valid nesting, a genuinely
new current receipt after nested take, saved cede value with a later argument,
and rejected-call rollback. The rollback gate additionally excludes E0438,
E0410 and E04661 cascades. An attempted opaque Vec extern probe failed before
the target check; it was not counted as evidence and was replaced by the
supported alias route, without changing extern ABI handling.

Four targeted CTests passed **4/4, 299.64 seconds**. The subsequent complete
run used the fixed implementation above, with no tracked-source changes:

| Complete gate | Result | Seconds |
| --- | --- | --- |
| Tools build | passed | 7.36 |
| PASS | 456/459 | 392.27 |
| FAIL | 480/480 | 118.72 |
| Unfiltered CTest | 115/117 | 1398.91 |

Full evidence: `/Users/zhyi/GitDP/tokalang/validation/rc13-byte-freshness-final`.
Compiler SHA-256:
`fd712e1ff42263aba856d6128e5c63a258507a7625dce517b3320e013b3e3072`.
`metadata.json` and `comparison.json` compare directly against the 19d14c4a
run: **no new failures or abnormal exits**, no claimed recoveries, and no new
CTest (the existing gate was expanded). Build/TOML/Template and the two old
CTest failures remain separately open.

No library, raw_take, CodeGen, ABI/TKI or old diagnostic expectation changes
were made in this increment. No push or freeze. The unrelated RFC remains
byte-for-byte unchanged at its previously recorded hash below.

## Previous candidate — 19d14c4a (rejected for stale receipt)

Implementation: `19d14c4a2f05c57a9bd287a04553738b766c381c`.
This completes the approved byte-buffer follow-up to the anonymous-record/task
package. It is a review candidate, not RC13 acceptance. Phase E, pushing and
freezing remain paused.

The finite source-visible byte operation contract checks resolver declaration
identity, concrete u8 specialization, physical layout, normally validated
function instances and compiler-pinned implementation schema seals. Identity
selects the contract; only a checked constructor or a matching current-value
handoff produces a receipt. Neither Drop, a type name, alloc ancestry nor empty
dependencies is used as a stand-alone proof.

Private receipts follow actual values and checked fields through construction,
growth, freeze/thaw/take, result packaging, source-visible return summaries and
async/await/Wait. Summaries keep prerequisites for each caller rather than the
first caller's values. Flow joins require every reachable alternative to qualify;
rejection restores the incoming analysis snapshot. Unmatched writes, raw
exposure, unknown source/extern/indirect calls and descriptor mutations lose
qualification. Missing qualification does not outlaw ordinary local use or
cleanup. It cannot be used to publish a new independent task result.

TCP and TLS read adapters now prepare an initialized byte prefix with the
existing resize operation. Completion explicitly checks both
`bytes_read <= request` and `bytes_read <= capacity` before publishing the
result. Existing runtime completion/cancellation and cleanup paths are unchanged.
No changes to raw_take, CodeGen, ABI/TKI, the two original positive programs,
Copy classification, or old diagnostic expectations were made.

### Verification on the fixed candidate

Full evidence: `/Users/zhyi/GitDP/tokalang/validation/rc13-byte-buffer-final`.
`metadata.json`, `source-manifest.json`, `preserved-worktree.patch`, the four
logs and `comparison.json` identify the exact source and compiler. Every phase
reported **zero tracked-source changes**.

Compiler SHA-256:
`8f846d30c3d59b3f5e9358cd4aca0d463445248ecc5a5a848974100c45cc1f06`.

| Complete gate | Result | Seconds |
| --- | --- | --- |
| Tools build | passed | 3.14 |
| PASS | 456/459 | 352.30 |
| FAIL | 480/480 | 119.03 |
| Unfiltered CTest | 115/117 | 1361.24 |

Against the previous **118d4860** fixed run:

- Restored unchanged positives: `g09_async_owning_payload_drop_regression.tk`
  and `g13_net_buffer_abi_test.tk`.
- Restored CTest: `toka_permission_net_regression`, covering the same network
  buffer failure, not a third independent positive recovery.
- `toka_byte_buffer_results` is a **new** passing CTest, not a recovery.
- No new failures or abnormal exits. PASS still fails Build/TOML/Template;
  CTest still fails `toka_stage1_indirect_parameter_cede` and
  `toka_stage1_return_matrix`.

The byte gate covers **27 source cases**, normal/shadow parity and diagnostic
object/LLVM non-production, plus altered-schema/source-hidden controls. Positive
source cases execute. A separate generated-IR observer instruments only the
byte allocator/drop sites: exactly 13 expected allocations, zero remaining
allocations after taken/unclaimed results, cold-task disposal and early return.
This is allocation accounting for those paths, not a process-wide `leaks` claim.

### Honest boundaries and retained development evidence

The earlier `02f44a7c` full attempt completed tools/PASS/FAIL but its CTest was
**interrupted**, after a read-only probe found that an opaque extern write could
retain the old Bytes receipt. That entry is now invalidated and covered by
owner/task-handle negatives. The partial run remains separately marked in
`validation/rc13-byte-buffer-fixed/INCOMPLETE.md`; it supplies no final CTest
number for this candidate. The temporary over-strict ordinary-call prerequisite
check was also removed after it falsely rejected HTTP/CSV local buffer cleanup;
the final full run above verifies their restoration.

One supplemental probe records a separate limitation: direct mutable aggregate
`extern` lowering produces a ptr/aggregate LLVM signature mismatch. Its positive
control is explicitly **Sema/parity only**, not an object/IR or runtime success.
Reproducer: `validation/rc13-byte-buffer-extern-local-control.tk` under the
repository parent. This ABI path was not repaired or counted as a recovery.

The source-visible contract intentionally does not qualify raw imports, generic
Vec<T>, recursive containers, source-hidden byte receipts or arbitrary callable
environments. Build/TOML/Template and the source-hidden callable deficit remain
independent work. No freeze ref, push, PR or release was performed. The unrelated
whole-value-generics RFC remains uncommitted and byte-for-byte preserved (SHA-256
`1b73b0fc46f9a700e1bdc9d0a0031614e1d9a4b398ce97f6a6423972474d70e2`).

## Previous fixed run — 118d4860 (historical, superseded)

Implementation: `118d48608ff01ea9f22d1afd46795102f47a6884`.
Includes anonymous-record WIP `511964a0` and task-result WIP `2ed69205`.
**Not Accepted / not complete:** four original positives are restored, but two
existing owned-buffer positives now fail the stricter result-origin gate.

## Implemented boundary

Source-visible, normally validated producers and helpers carry private result
witnesses. Independent values, static storage, actual borrowed roots/storage,
and symbolic parameter-result projections remain distinct. A helper's explicit
lifetime ceiling permits a relationship but does not manufacture its source.
Definition summaries keep parameter positions; each caller discharges them with
the current actual witness, never the first caller's values.

Task-witness prerequisites are separate from returned-result origins. Snapshots
carry current-value witnesses and prerequisites; joins preserve identical value
witnesses and union prerequisites, and failed calls restore their incoming state.
Result proof checks occur before call rollback guards exit. Wait captures state
before evaluating its operand. Reference results preserve descriptor/storage
level; task-frame borrows are rejected. Proofs grant no Copy, consumption, Drop,
write permission, or detached-execution permission.

Normal `.start` and plain `.await` forward an already verified result relation
without changing their existing execution checks. An actual checked enum
constructor may prove its live payload independent; this does not classify an
entire enum, container, or recursive type as independent.

`block_on` uses the existing `<- handle` syntax and a whole-T local. Its redundant
raw pump call was removed because `.wait` already starts/pumps the task. No
runtime, native ABI, TKI schema/key, CodeGen result-take implementation, raw_take
rule or global Copy rule was changed. Phase E remains inactive.

## Directed evidence

`toka_task_result_projection` contains 27 cases: scoped external data after task
cleanup, scalar/static/unique/shared/enum results, selected argument/field,
forward/move, cache hit and declaration order, branch preservation/conflict,
mutation, source-hidden refusal, frame/descriptor escape, invalid producers,
rollback after both ordinary validation and result-proof rejection, and the
existing trap for repeated owning-result extraction. Positive cases execute;
negative cases verify diagnostic/parity and object/LLVM non-production. No
dangling-view test is executed.

The existing safety gate includes the three original anonymous-record programs
and its prior Wait/Copy/HTTP controls. The anonymous-record implementation tracks
field-specific static/dynamic origins and original binding identities. Its
normal/shadow/runtime/escape controls remain enabled.

The old shadowed-Wait negative now diagnoses E0455 instead of IncompleteFacts:
the task witness retains the original owner's source, rather than replaying the
later shadowing initializer. It remains a negative with no artifact.

## Complete run

Evidence directory:
`/Users/zhyi/GitDP/tokalang/validation/rc13-record-wait/task-fixed-candidate`.
Compiler SHA-256:
`9c5266c7692640ecb65595d3b1226d019eb7567eb020207b788aa7ebb544168e`.
`metadata.json`, `source-manifest.json`, individual logs and `comparison.json`
record commands, fingerprints, timings and named differences. No tracked source
changed during any of the four commands.

| Gate | Result | Seconds |
| --- | --- | --- |
| Complete tools build | passed | 13.25 |
| Complete PASS | 454/459 | 376.40 |
| Complete FAIL | 480/480 | 132.20 |
| Unfiltered CTest | 113/116 | 1481.31 |

No abnormal exits were detected in the PASS/FAIL comparison. The owning-repeat
fixture deliberately exercises the pre-existing runtime trap and is not counted
as an unexpected crash.

Against the `fcead1a8` full baseline:

- Restored: `g04_anon_records`, `g08_noshared`, `g08_test`,
  `g09_async_wait_syntax`.
- Newly failing: `g09_async_owning_payload_drop_regression` and
  `g13_net_buffer_abi_test`.
- Still failing: `g10_build_hybrid_test`, `g15_stdx_toml_test`,
  `g18_stdx_template_test`.
- CTest retains `toka_stage1_indirect_parameter_cede` and
  `toka_stage1_return_matrix`; `toka_permission_net_regression` newly fails.
- `toka_task_result_projection` is a new passing CTest, not a recovered old test.

## Remaining blocker — no unsupported admission

The owning-payload program now reaches `block_on<Bytes>` at line 115. `Bytes`
contains raw buffer/length/capacity; neither its name nor Drop supplies a current
instance result-origin witness. The network program first fails at line 50 on
`block_on<Result<AsyncReadResult, AsyncIoError>>`: both result/error structures
carry `Vec<u8>` storage. These are new failures relative to the fixed baseline,
not newly declared illegal language programs. Both originals remain unchanged.

The next decision is how to establish or migrate these actual owned-storage
contracts. This candidate does **not** add a Bytes/Vec whitelist, infer
independence from an empty dependency set, or resurrect recursive/container
raw_take admission. The existing Build/TOML/Template raw_take group and
source-hidden callable deficit remain separately open. A generic function type
still does not prove an environment independent.

No freeze ref, push, PR or release was made. The unrelated whole-value-generics
RFC remains uncommitted and unchanged, SHA-256
`1b73b0fc46f9a700e1bdc9d0a0031614e1d9a4b398ce97f6a6423972474d70e2`.
