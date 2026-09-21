# Anonymous records + task result projection: fixed WIP candidate

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
