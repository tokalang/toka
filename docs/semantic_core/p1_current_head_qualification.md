# P-1 Current-HEAD Qualification Ledger

Status: **Blocked — `f388fedb7a8d9f70ba49185f4ed2176f297174f1` is not a
qualified P-1 baseline.** This ledger records an audit run, not a release
approval.

## Audit identity

- Revision: `f388fedb7a8d9f70ba49185f4ed2176f297174f1`
- Date: 2026-08-04
- Target: `macos-arm64`
- Source: clean detached worktree; `source_dirty: false`
- Runner: `tools/scripts/release_gate.py --target macos-arm64 --build-dir
  build-p1`

The revision makes two baseline-only repairs found by the first P-1 pass run:

- removes an invalid self-overlap transfer in
  `lib/build/internal/support.tk`; and
- terminates the logically non-fallthrough CSV streaming loop with
  `unreachable` in `lib/stdx/data/csv.tk`.

Neither repair adds language syntax, alters TKI authority, or changes the
async/ownership contract under qualification.

## Current evidence

| Gate | Result at this revision |
|---|---|
| release build | pass |
| release positive suite | **380 passed, 17 failed** |
| semantic replay, source-backed vs source-less | 32 passed, 0 failed |
| TaskHandle Lifecycle Contract v1 | pass |
| Outcome retained-body recheck and coordinate audit | pass |
| `@Encap` Slice 5 TKI v2 audit | pass |
| untrusted unsafe-TKI API revalidation | pass |
| negative diagnostic suite | 319 passed, 0 failed |
| warning suite | 1 passed, 0 failed |
| `g16_init_cleanup_liveness_test.tk` | pass |

The release runner is fail-fast. Its `fail`, `warn`, replay, cache, tooling,
incremental, native-build, QSLite, async, sanitizer, and package stages were
therefore marked `not_run`; the directly relevant fail/warn/replay and P-1
semantic audits above were rerun independently.

The replay result includes
`permission_005_partial_cede_lifecycle`, so the exact/ancestor/descendant/
unprovable existing-destination overlap rule is checked both source-backed and
source-less at this revision. The init cleanup fixture supplies the current
positive evidence that an `uninit` local is not dropped while a live resource
still follows its cleanup path.

## Release blockers

### Loopback capability unavailable in this runner

Thirteen failures are loopback server/client fixtures. Each binds or connects
to `127.0.0.1`; direct probing of the TaskScope result-cancellation fixture
showed that `TcpListener::bind(127.0.0.1:0)` can return an invalid listener in
this sandbox before its TaskScope cancellation path is entered.

- `g09_async_context_redline_test.tk`
- `g09_async_context_timeout_integration_test.tk`
- `g09_async_reactor_tokenization_test.tk`
- `g10_http_phase1_test.tk`
- `g12_stdx_http_client_server_test.tk`
- `g12_stdx_tls_test.tk`
- `g12_stdx_https_wss_test.tk`
- `g12_stdx_websocket_malformed_test.tk`
- `g12_stdx_websocket_test.tk`
- `g13_stdx_net_zero_copy_bench.tk`
- `g16_async_accept_context_test.tk`
- `g16_stdx_http_server_connection_test.tk`
- `g16_task_scope_result_cancel_test.tk`

This is not an approved quarantine. A supported runner that permits loopback
bind/connect must execute those fixtures before P-1 can close. The direct and
nested non-network portions of `g16_task_scope_result_cancel_test.tk` completed
in an instrumented temporary probe; that observation does not replace the
fixture's end-to-end gate.

### Pre-existing runtime failures requiring disposition

Four non-network fixtures still reproduce a signal failure when compiled and
run directly at this revision:

- `g03_test_dynamic_json.tk` (after `Starting parse...`)
- `g07_hashmap_test.tk`
- `g07_std_set.tk` (after its success print, during cleanup)
- `g07_test_stdx_flag.tk`

Their test histories precede the 2026-08-03 P-1 requalification branch. That
proves they were not introduced by the P-1 repairs, but does not make them
safe to ignore. They remain baseline runtime/library blockers until each has a
minimized reproducer and an explicit repair or stable, justified quarantine
decision.

## Decision and next action

P-1 remains open. Do not start the PlaceState Core or another semantic feature
from this evidence.

1. Run the complete release gate from a clean checkout on a supported runner
   with loopback networking enabled.
2. Isolate and assign the four retained runtime crashes; repair them or record
   a stable, justified quarantine with its reproducer and owning subsystem.
3. Rerun every release stage at the exact candidate commit, then replace this
   blocked status with a qualified-HEAD record only if all remaining gates are
   green or explicitly accepted under the P-1 rule.
