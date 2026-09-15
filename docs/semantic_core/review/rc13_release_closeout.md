# RC13 release closeout after G

G remains accepted at `4005a68e`, recorded by `e3102120`. E is not started.
This log does not revise the G RFC or replace full-suite results with targeted runs.

## Fixed full baseline at 291db6cb

Tested implementation: `291db6cbed2ba80d4a57ec7777914eacef6d56c7`.
Tools build, PASS, FAIL and CTest ran once in sequence; all tracked-file
fingerprints were unchanged at every stage boundary. The pre-existing other-worker
RFC diff was preserved (SHA-256 recorded in the machine-readable report).
No implementation or expected-diagnostic changes were made during qualification.

Comparison is to the retained complete run at `13b907bc`, **not** G's starting
revision or an extrapolation from incremental tests.

| Suite | Previous full run | This full run | Recovered failures | New failures |
| --- | --- | --- | --- | --- |
| PASS | 388/457 | 401/457 | 13 | 0 |
| FAIL | 444/478 | 445/478 | 1 | 0 |
| CTest | 96/99 | 97/100 | 0 | 0 |

CTest adds `toka_context_receiver_handoff`; its higher numerator is not a
recovered old CTest failure. Remaining CTest failures are exactly
`toka_stage1_indirect_parameter_cede`, `toka_stage1_return_matrix`, and
`toka_binding_b2_arena`. No compiler/runtime signal or abnormal-exit event was
found in the completed current-suite logs. Full tools build passed (17.85 s);
PASS took 303.71 s, FAIL 142.18 s, CTest 1028.37 s (outer runner timing).

Recovered PASS names:

- `g09_context.tk`
- `g10_async_http_server_test.tk`
- `g10_http_empty_header_value.tk`
- `g10_http_phase1_test.tk`
- `g10_net_http_server_test.tk`
- `g10_websocket.tk`
- `g12_stdx_http_client_server_test.tk`
- `g12_stdx_https_wss_test.tk`
- `g12_stdx_websocket_malformed_test.tk`
- `g12_stdx_websocket_test.tk`
- `g13_stdx_net_zero_copy_bench.tk`
- `g16_stdx_http_server_connection_test.tk`
- `g18_header_map_lookup_miss.tk`

Recovered FAIL name: `header_map_lookup_miss_blocks_insert.tk`.
All 56 remaining PASS names and 33 remaining FAIL names, plus first-error probes,
are preserved in [the full comparison](rc13_baseline_291db6cb.json).
Raw logs and source manifest: `/private/tmp/toka-rc13-baseline.NrSLyz`.

One bounded leaks recheck distinguished the program from the inspection tool:
the receiver program alone exited 0 in 0.01 s. `leaks --atExit` reported failure
to acquire a task port for both `/usr/bin/true` and the receiver program; the
latter was limited to 15 s and its test process group was terminated on timeout.
Leak freedom remains **unverified**, not zero-leak and not an observed program
exit hang.

Fresh first-error priorities (counts are blocked tests, not promised recoveries):

1. Vec `ReferenceBindingSelectorUnavailable`: 7 tests. Select this as the next
   bounded batch; inspect the actual reference-element contract before changing
   library interfaces or compiler behavior.
2. Vec `ElementDependenciesUnproven`: 5 tests, a separate dependency question.
3. BTreeMap `RouteIneligible`: 3 tests.
4. Slab `AccessCapabilityMismatch`: 2 tests.

No new batch was implemented during this baseline run. E, G freeze, and push/
release restrictions remain unchanged.

## Reference domains and the seven Vec cases (post-baseline candidate)

Accepted by independent incremental review at `79534cf1`: core CTest 2/2,
27.68 s; additional local-then-external referent escape rejects E0455 with no
artifact, and the legal external-source control runs. This accepts only the
two domains/seven-case batch, not a refreshed full baseline.

User-authorized extension: `reference_only` and `non_reference` classify only
known legal top-level reference/non-reference types. Unknown/unresolved types,
unresolved component types, const subjects and illegal handle chains cannot
gain admission by negation. Classification grants no Copy, ownership, dependency,
Drop, initialization, or raw_take proof. Existing constrained impl selection is
reused; there is no lazy-body or overload-system rewrite.

Interface key changes from `0.9.9-21` to `0.9.9-22`, with format/native ABI
unchanged. Both constraints use the existing generic-bound source/TKI exporter
and semantic identity paths. Tests cover source-hidden agreement and rejection
of old interfaces, plus nominal values, unique/shared/raw, borrowed-field shapes,
transparent scalar aliases beneath references and intersected bounds.

The initial minimal Vec constructor failed while eagerly checking unused
insert/push/pop/set implementations. Consumption, removal, growth, resize and
owned iteration now have a `non_reference` impl domain. Constructors and borrowed
iteration remain available for references. No reference consumption check was
removed. Domain-specific methods remain rejected when explicitly selected
outside their declared domain.

For references, the new API is `values = values#.appended(&owner)`.
It requires **both** `reference_only` and the independently checked `@Copy`,
consumes the old container, copies only the reference descriptor under existing
permissions/PAL, and returns the container with `self | val` dependencies.
Growth copies live descriptors, releases old storage with free[0], and never
takes ownership of referents. Existing non-reference push/insert behavior stays
unchanged. The seven tests migrate their reference pushes only; no runtime
assertions were removed. The payload-borrow fixture now spells `&&payload`,
matching the existing non-collapsing `&T` rule for T = &i32.

Two intermediate append implementations were withdrawn after a local-escape
probe compiled: in-place append had no receiver-write dependency channel, and
the first returning version exposed an existing assignment classifier gap.
Materialized shapes have empty GenericArgs and keep their arguments on
Decl::InstantiationArgs. Return checking inspected both, while assignment's
isBorrowLikeType ignored the latter and discarded carried dependencies.
Assignment now reads the same instance arguments. This preserves dependencies
from the existing return contract, without inventing referent facts or a new
receiver-write protocol. The escape probe now rejects with E0455.

Final targeted results (not a full baseline replacement):

- Seven original Vec cases compile and run: `g07_for_iterators`,
  `g08_for_alias_binding`, `g08_for_alias_generic_clone`,
  `g08_for_alias_place_iterator_vec_ref`, `g08_handle_grammar_parser_matrix`,
  `g08_handle_grammar_valid_matrix`, `g08_vec_payload_borrow_views`.
- Multi-owner 12-element growth retains content; disposing of the reference
  storage leaves both owners usable and they subsequently drop exactly once.
- Local escape, readonly writes, repeated container consumption, lost alias
  morphology and raw interface misuse all reject with no object, normal/shadow
  equal. The latter two existing fixtures were migrated to reach their original
  target diagnostics E04643/E0621, without changing expectations.
- Final combined domain/Vec/iterator/shared-aggregate/Vec-pop gates: 5/5,
  102.72 s. The two new domain/Vec gates account for the seven recovered cases,
  growth/cleanup and refusal checks; do not add those cases again to a full total.
- G targeted gate: 1/1, 122.94 s. Interface compatibility script also passed
  stale-TKI no-artifact checks, stale-cache source fallback/runtime, source-hidden
  rejection and old-runtime link rejection. No full PASS/FAIL rerun is claimed.

The five ElementDependenciesUnproven cases remain out of scope. No raw_take,
native ABI, capture or reference-count algorithm changes are included.
E remains unstarted; other-worker RFC edits are not part of this candidate.

## Five ElementDependenciesUnproven cases

Current-revision check-only rerun confirms all five originally still reached
`vec_pop_raw_tail<T>` at vec.tk:35 before the CSV migration below. Independent
probes use the same field representations (not claims of identical declaration
identity). A constructor-only Vec probe is sufficient to trigger the rejection:
its matching impl eagerly checks pop even when the program never calls it.

| Cases | Concrete element types implicated by source and isolated probes | Missing proof |
| --- | --- | --- |
| g14_stdx_csv_test, g14_stdx_csv_corpus_test | Vec<string> inside Vec<Vec<string>> | Inner buffer is raw storage; the raw parameter bridge has no allocation/slot value certificate for that owner |
| g10_build_hybrid_test | ModuleSnapshot, RebuildModuleInfo (HashMap value vectors) | dependencies/dirty_deps contain Vec<string>; same opaque inner storage issue |
| g15_stdx_toml_test | TomlValue and TomlEntry | Recursive ArrayValue(Vec<TomlValue>), no closed per-instance element dependency proof |
| g18_stdx_template_test | Vec<string> in list-map values; FuncHandler in function vector | Opaque nested storage; function pointer category also rejected by the current raw_take dependency predicate |

Vec<string> alone and Vec<TemplateNode> (string + i32) are passing controls.
These findings do not establish an actual-source propagation regression and
do not prove that every opaque owner is dependency-free. The bridge receives
a raw parameter, not a local allocation with matching recorded writes, so its
recorded-slot fallback is unavailable. No raw_take, E, or compiler changes are
made in this batch.

### CSV library migration checkpoint (not accepted)

`CsvRecords` replaces the nested row-owner vector with owned Vec<string> fields
and Vec<usize> row boundaries. `parse_records`/`write_records` use CsvRecords;
read_record/write_record retain their single-row Vec<string> API. Owned rows
are drained using the already-supported pop<string>; storing each row in reverse
physical order permits moves without cloning or a new raw operation. Public
field access and extraction preserve logical row/column order.

The original corpus case compiles and runs. Additional tests cover a parsed
document outliving its owned input, row order, single-row extraction, 100 rounds
of success/partial failure, and refusal to return a field reference into a local
document (E0455; object/IR absent). These are not a leak-freedom measurement.
Parser scanning, RFC4180 content checks and error-position assertions are retained.
The registered `toka_csv_flat_records` CTest passed 1/1 in 25.36 s; this includes
the original corpus, the new owned-input/row-order loop and escape refusals.

The complete g14_stdx_csv_test **is still failed**, now at the existing BufIO
make<File> unique initialization (bufio.tk:29/165, IncompleteFacts), before its
streaming path can run. Do not count eliminating the outer-Vec first error as
this test's recovery. Only the corpus case is a restored original PASS target.
BufIO, Build, TOML and Template are not silently fixed or reclassified here.
Probe logs: `/private/tmp/toka-element-deps.ODDq3g`.

### CSV streaming: isolated compiler candidate, not applied

Baseline `9f010b7426e4349a9b38ec3ad9d1b8652baeaae2`.
Real File probes independently reproduce BufferedReader<File>::make and
BufferedWriter<File>::make rejection. Calling only Reader::new still checks
the unused make method and fails. A smaller non-generic `FileBox(file:File)`
with `new FileBox(file=cede file)` also rejects IncompleteFacts; neither CSV
nor the 4096-byte array is necessary. The equivalent value FileBox compiles
and runs; a scalar unique Box also compiles and runs. All probes use File::open,
with no fd/unsafe substitute or debugger.

Facts path: new has UniqueOwner/NoSourcePlace, but payload type-only dependency
inspection sees File's opaque handle and retains Structural/IncompleteFacts.
The constructor's existing field transfers are not used to qualify the outer
new value. No evidence supports changing every unique value to dependency-free.

The full proposed patch is [csv_new_initializer_candidate.patch](csv_new_initializer_candidate.patch).
It prepares unpublished AggregateMember plans only after normal validation,
against the original snapshot and exact initializer/declaration; every explicit
field must be admitted and dependency-complete/free, and the pure whole-group
gate must admit before qualifying the new temporary. It excludes array-new,
missing/default/spread fields and all unproved dependent fields. Existing source,
PAL, morphology, permission, group, cleanup and final validation checks remain.
This is a proposed production admission change, not merely reporting facts.

Auto-review rejected applying that compiler diff as outside the current library
repair scope. It is saved **unapplied and unbuilt** for explicit review; no
alternative bypass was attempted. Required validation if authorized: real File
new/make and original streaming tests, duplicate source, borrowed/raw payload,
permission/lifetime mismatch, missing/invalid plans without artifact, and exact
cleanup controls. Probe directory: `/private/tmp/toka-csv-stream.TTV0ps`.

## Shape inference crash recovery

The unchanged `tests/pass/g09_context.tk` previously terminated with SIGSEGV
after five Option return/discard diagnostics. Inferred shape lookup used a
permission-decorated display string as a declaration-map key, and continued
with a null declaration. Resolution now retains the resolved nominal declaration;
missing materialization returns unknown rather than dereferencing null.

After the repair, the same five diagnostics remain at the start, followed by
additional Vec and Context diagnostics: this is not a new earlier rejection
masking the crash. The original source is retained and is still a failed PASS
target, not a negative test. Normal/shadow check-only, object and IR invocations
terminate normally and rejected invocations leave no artifact. The gate permits
future successful compilation instead of freezing the present diagnostics.

`toka_negative_harness_termination` also runs valid inferred, explicitly typed,
and writable expected-type `Box` construction. Latest targeted rerun: 1/1,
17.26 seconds. Earlier current-patch G + termination gate: 2/2, 138.67 seconds.
No full-suite result is claimed for this change.

## Network temporary-result migration

HTTP/WebSocket remove outer `cede` from call/unwrap temporary results, retaining
named-source transfers inside them. Four original controls compile and run:
`g10_http_empty_header_value`, `g18_header_map_lookup_miss`,
`g12_stdx_websocket_malformed_test`, `g12_stdx_websocket_test`.
Their normal/shadow check-only return codes and stderr also match.
The reproducible runner is `tools/scripts/test_release_temporary_results.py`.
The complete HTTP/WebSocket first-error union from the retained triage contains
12 PASS cases including HeaderMap (rather than the initially quoted 11).
All 12 compile and run with exit 0 after migrating two thread callers and the
read-only certificate path bindings. Runtime logs confirm real HTTP/WS/TLS
execution, not an unsupported-network skip. Thread callers still use native
threads and now check spawn, join and worker results. The certificate helper
takes `const char *`; writable pointers were unnecessary.

The 12 cases are explicitly listed in the runner. Initial library-only changes
restored 9/12; the three caller migrations restore the remaining 3/12. Four
controls and the three changed callers have separately checked normal/shadow
parity. This is a targeted cluster result, not a new full PASS/FAIL/CTest run.
Local checkpoints: crash recovery `9bd79b16`, library temporaries `492ee129`.

The remaining Vec cluster includes `ReferenceBindingSelectorUnavailable` on
reference-valued parameters. Do not infer a raw-element migration from a stale
line number alone. Context's concrete failing instances will be investigated
after the network batch, without Option/Vec/raw_take exemptions.

## Context receiver handoff repair

`tests/semantics/context_release/cancel_chain.tk` preserves
`with_cancel<BackgroundContext>`, the actual `Vec<Sender<bool>>` state,
`Option<Receiver<bool>>` returned by `done()`, async start/block_on, cancellation
error, closed-channel receive and repeated cancellation. Normal/shadow checks
match and the compiled program exits 0. The original `test_cancel` extracted
without changing its body also compiles and runs under start/block_on.
No library/compiler changes were required for this cancellation path.

Timeout isolation retains workspace identity. Creating the timeout and obtaining
its receiver pass separately. Starting a worker with that receiver reproduces
the Option return/discard errors and downstream unresolved Vec morphology; simply
removing the await does not resolve them. Starting a worker with the direct
`channel<bool>()` receiver is a passing control. Explicit Receiver<bool> annotation
and a trivial worker body do not resolve the Context-derived receiver failure.
Those preliminary controls did not use the same outer binding writability.
The same-worker three-route matrix corrects that: direct channel, function-returned
Option/unwrap and Context-returned receiver all failed with a writable `rx#`.
The first wrong lookup is `canImplicitlyPassToCede`: its Drop query used the
permission-decorated display string (`Receiver_M_bool#`) instead of the resolved
nominal identity (`Receiver_M_bool`). Missing that cache/map key caused property
analysis to fall back to the generic template and check unresolved T bodies.
The repair resolves the complete type and uses its soul name for this lookup;
it does not change permission, Send, dependency, Drop or raw_take policy.

After repair, all three original writable-binding routes compile and run. The
worker receives a closed notification and exits. Context cancels twice. The
gate also rejects a second consuming handoff of the same binding with E0438
and no object output. Both call and non-call shadow match normal diagnostics.

| Boundary | Observed facts in this matrix |
| --- | --- |
| Return/wrap | Full Option<Receiver<bool>> value; result carries cleanup |
| unwrap | Full Receiver<bool> result; no external dependency roots |
| binding | Receiver<bool># retains nominal instance and current write view |
| worker argument | OwnedValue, MoveOwned, InvalidateSubtree, CalleeAssumesLiability; dependency complete |
| start | StartHandoff, explicit consuming formal/source, no escaping dependency |

Original unmodified `g09_context` now compiles and runs with exit 0, including
timeout (20 ms in the observed run), parent/child cancellation and value query.
This is targeted recovery, not a full-suite rerun. Tests are retained in
`test_context_receiver_handoff.py`. Final three-route plus original-program
CTest: 1/1, 26.02 seconds.
Ordinary caller, direct signature and termination regression gates: 3/3,
31.34 seconds. Other signature-driven method/static, callable/indirect,
multi-argument atomic and remaining-route gates: 4/4, 42.39 seconds.
The termination test's successful-shadow branch was corrected:
main.cpp intentionally forces shadow to check-only, so it must not demand an
artifact even if -c/--emit-llvm was supplied. Normal compilation still must
produce one; rejected compilation must not.

Additional leaks-at-exit inspection timed out; no zero-leak claim is made.
No source library changes or container exemptions were needed.

Discarded lead: a temporary nested-match probe without workspace identity had an
empty destination identity. With proper workspace registration it passes. This
was a probe-environment difference, not proof of a Context assignment regression.

## Tightened new initializer implementation (WIP)

The authorized initializer patch is now applied with pre-expansion provenance.
InitStructExpr captures its original member names before Sema can expand spread
or inject defaults, and clone preserves that record. The new route checks that
record, exact nominal type, explicit complete field coverage, original snapshot,
individual dependency-complete field plans and whole-group admission. It
**re-prepares and revalidates** field plans; it does not claim to retrieve a
previously persisted complete group. No CodeGen ownership reconstruction was added.

Direct new FileBox and an owning File payload with a destructor counter run;
the counter reaches one only after the box is dropped. Defaults, elision,
generic default completion, duplicate source, borrowed/raw fields, permission
and type mismatch are rejected without object/IR. Default/type rejection restores
the source File (no subsequent E0438/E0410). Ordinary scalar defaults and value
spread remain valid. New-expression spread currently stops at the Parser; this
is explicitly not counted as exercising the Sema spread gate. A native AST test
checks that post-expansion clones preserve original default/spread markers.

The original CSV corpus and full streaming test now run. Additional real File
tests pass >4096-byte quoted/multiline records, repeated EOF, size-limit errors,
try_open failure and reads from an explicitly closed File. File::open is
intentionally fatal on missing files; the first error-control probe incorrectly
used it and was corrected to the recoverable API, not fixed in the library.

**Still pending:** direct callers binding the result of Reader/Writer make<File>
remain IncompleteFacts. The callee's new is now qualified, but its returning
unique result has no caller-visible independence evidence. A proposed private
constructor-certificate return forwarding extension was rejected by auto-review
as broader admission work and was not applied. No return certificate fields or
return-statement forwarding code are in this implementation. Thus the requested
full make<File> matrix is not yet complete, despite restoration of CSV's actual
value-construction path. This WIP must not be marked Accepted.

Applied-part final validation: initializer provenance, new-initializer matrix,
Stage-0 CodeGen authority fault suite, and CSV corpus/full/streaming matrix:
4/4 CTest, 74.48 s. This is not qualification of the still-failing direct factory
result bindings and is not a whole-suite rerun. Other-worker RFC changes remain
untouched; no push or E activation.

## Result independence fact collection (c3a7393b checkpoint, initially not enabled)

The next implementation reuses the existing factory Unprepared/Preparing/Valid/
Invalid state machine and isolated body journal, rather than adding a public
dependency inference protocol. A private value certificate is seeded only after
an admitted, normally validated new initializer with complete empty dependencies.
It is bound to the expression/type/function instance and is not cloned or exported.
Fresh local bindings carry the fact; mutation, rebind, alias writes and uncertain
raw aliasing discard it. Snapshot rollback restores facts, while joins intersect.
Rebinding does not automatically restore an old fact merely because types match.

All checked returns contribute to a function frame; publication waits for full
successful body validation and all-return coverage. Generic Invalid/Unchecked
dependencies poison that frame using the existing validation paths. A consuming
unique parameter may carry a **conditional prerequisite**, never an unconditional
independence claim: an explicit whole-value relay requires the caller's actual
argument to have a corresponding fact. No borrow/dependency route is synthesized.
Missing bodies, missing summaries, wrong function/type identity and invalid
generic cache entries cannot supply a fact.

Native Sema tests exercise constructors, conditional relay, forwarding, all-return
branches, field/alias/loop mutation, rebind, rollback and conservative joins.
The combined fact/new-initializer/CSV gates pass 3/3 (77.86 s); after an additional
function-identity guard, the fact unit passes again (1/1, 1.67 s).

**No new result admission is enabled.** Auto-review rejected the small consumer
that would set Dependency=None, DependencyFactsComplete=true and
TemporaryEligibility=Eligible only for a normally validated result with a valid
summary, discharged prerequisites and no existing external dependency. The full
unapplied consumer is [result_independence_activation_unapplied.patch](result_independence_activation_unapplied.patch).
Direct make<File> caller bindings therefore remain pending; cache/source-hidden
end-to-end qualification and the requested full baseline refresh have not been
claimed. Explicit production-admission approval is still required by execution
review. E, ABI/TKI, raw_take, publishing and other-worker changes remain untouched.

## CSV result admission activation candidate

After explicit production-admission approval, the saved consumer diff is applied.
The historical `activation_unapplied.patch` filename describes the reviewed
artifact, not its current application status. A normally validated unique result
with a completed exact-instance summary, no remaining argument prerequisites and
no recorded external dependencies supplies the three missing dependency/temporary
facts. The full planner remains the only admission path; no capability, source,
cleanup or lifecycle check is bypassed.

Reader/Writer `make<File>`, repeated Reader specialization calls, a forwarder
declared before its factory, and a conditional unique relay now compile and run.
The forwarded File-owning payload is destroyed exactly once. Missing result
summaries, unmet relay prerequisites, field mutation, branch pollution and
rebinding remain rejected without object/IR, with normal/shadow parity. The
existing default/type-error source rollback, permissions, borrowed/raw fields,
source duplication, snapshot join and invalidation tests remain in place.

Targeted CTest: result facts, initializer syntax provenance, new/factory matrix,
Stage-0 CodeGen fault gates and the original CSV corpus/full/streaming tests:
5/5, 77.78 s. A follow-up run strengthens the new rejection assertions to require
the intended E04661/IncompleteFacts rather than merely any compile failure.
Full tools/PASS/FAIL/CTest comparison follows on a fixed implementation snapshot;
these targeted counts do not replace the previous full baseline. No E, push,
ABI/TKI or raw_take change; the other worker's RFC edit stays unstaged.

## Fixed candidate full comparison: c4b726a7

Implementation tested: `c4b726a74641d88b39067e7a7a2c29c997061117`.
The strengthened rejection run above also passed (all five expected
E04661/IncompleteFacts checks, parity and no-object/no-IR assertions).
This is a unified CSV/factory candidate, **not an Accepted or RC13-ready claim**.

Full logs: `/private/tmp/toka-rc13-csv-final.2SiBic`.
Machine-readable metadata and named differences:
[rc13_baseline_c4b726a7.json](rc13_baseline_c4b726a7.json).
Comparison baseline is the complete `291db6cb` run, not an intermediate directed
test result. Every stage preserved the tracked source manifest, including the
other worker's uncommitted RFC; no implementation changes occurred during runs.

| Gate | Result | Named change from complete baseline | Seconds |
| --- | --- | --- | --- |
| All tool targets | Passed | No build failure | 0.73 (incremental, full targets) |
| PASS | 412/457 | 11 recovered, 0 new failures | 266.67 |
| FAIL | 447/478 | 2 recovered, 0 new expectation failures | 145.13 |
| CTest | 103/106 | Same 3 failures; 6 added gates pass | 1126.52 |

No compiler/runtime abnormal exits were recorded. PASS recoveries are the seven
accepted Vec/domain cases plus `g10_io_bufio`, `g14_generic_nested_reader_bound`,
`g14_stdx_csv_corpus_test` and `g14_stdx_csv_test`. The two FAIL recoveries are
`for_alias_removes_morphology` and `morphology_raw_extendable_vec_borrowed`.
These changes span the revisions since 291db6cb; they are **not all attributed
to the 17-line activation patch**. No oracle was changed in this candidate.

The remaining CTest failures retain their actual causes:

- indirect: source-hidden callable factory lacks environment information,
  E04661 `CallableReturnEnvironmentUnavailable`, before the intended E04570 check;
- return matrix: build-return-buffer fixture hits Vec/HashMap raw_take
  `ElementDependenciesUnproven`;
- Arena: writable raw request from a known read-only/frozen source, E04663.

All 45 remaining PASS failures were checked for their current first diagnostic.
44 match the baseline code/message/source-file (ignoring line shifts); the one
text difference is the existing TCP echo-server missing `join` on Result, whose
mangled nominal text reflects interface version 21→22. This candidate itself
does not change the interface key. Current shared first sites include BTreeMap
(3), Vec element dependencies (Build/TOML/Template, 3), and Slab (2). Their full
probe results remain in `first-errors.json` alongside the run logs. They are
separate release work, not reasons to broaden this CSV implementation.

CSV's original corpus and streaming paths, real File constructors/factories,
forwarding/cache, exact-once payload cleanup and the admitted/rejected matrices
are complete for the authorized candidate. E remains off, no push/PR, no frozen
ref movement; leak tooling's previously recorded permission limitation is not
reinterpreted as a zero-leak result.

## CSV accepted; source-only semver/thread migration

`c4b726a7` is **Accepted** for CSV library migration, explicit new initialization
and restricted independent-return qualification. Independent review reran the
five targeted gates (5/5, 136.23 s) and checked the full named comparison.
This scope is closed; it is not general binding or RC13 acceptance.

The next source-only batch removes seven redundant temporary-result `cede`
spellings in semver. The two old TCP tests now check spawn Result, extract the
JoinHandle, and check join Result plus worker exit status. Their actual writable
receive buffers are declared writable on the binding, without a compiler/raw
permission workaround. Existing chunking, concurrent server/client, EOF and
content assertions are preserved; echo write/read and clone/source-preservation
assertions are strengthened. A network-unavailable skip is not a qualified run.
The dedicated runner also runs existing thread-example and condvar controls.
No compiler implementation or existing oracle is changed by this batch.

Final directed verification: full tool targets built successfully; CTest
`toka_rc13_semver_thread_migration` and `toka_permission_net_regression` pass
2/2 (111.64 s). The former runs all five programs with normal/shadow parity
(34.31 s); the latter reruns the exact twelve net controls. Strengthened
semver clone, echo content, spawn/join and worker exit assertions are included.

Actual restored failures from the c4b726a7 full baseline are:

- `g15_stdx_semver_test.tk`;
- `g10_net_read_exact.tk`;
- `g10_net_tcp_echoserver.tk`.

These are **three directed recoveries only**, not a new full-suite result.
The complete baseline stays 412/457, 447/478, 103/106 until rerun. Neither
the remaining stress/MPSC source failures nor the three old CTest failures
are declared resolved by these changes. No full rerun, compiler changes,
E activation, push or release; other-worker RFC edits remain untouched.

## Semver/thread accepted; Arena nullable-slot migration

`1c5c42fb` is Accepted for the preceding source-only batch. Independent review
reran its five-program test (1/1, 32.62 s); the three recoveries remain directed,
not a revised full-suite count.

Arena isolation showed that writable nullable result binding failed even before
`unwrap`; non-Arena ordinary/generic pointer returns and unwrap controls passed.
Arena's five `null as nul *ArenaChunk` expressions explicitly introduced a
readonly pointee view into writable nullable slots and their recorded ancestry.
Use context-typed `null` for those slots instead: the declaration already carries
the complete pointer permissions. `alloc_type`/`alloc_array` return signatures,
the unsafe Addr constructor, nullable checks and runtime allocation remain intact.

An independent probe also exposed conservative sibling-ancestry pollution when
reading an integer member as an allocation size. A trial compiler refinement
fixed that probe but did not alone fix Arena; it was **removed**, and the compiler
rebuilt before the successful Arena run. No compiler change belongs to this
candidate. That separate probe is not declared solved or made an Arena prerequisite.
Isolation files are in `/private/tmp/toka-arena-permission.IzNdzD`.

The original Arena runtime, scoped malloc/free failure/retry counts, and five
readonly/nullable/PAL rejection pairs already passed with the library-only change.
The gate now also checks a split nullable binding, explicit non-null check,
unwrap, and a forwarding function, initializing the Point fields before reads.
No allocation is reclassified as an initialized T or given Drop ownership.

Final directed CTest: `toka_binding_b2_arena` and
`toka_unsafe_raw_construction`, 2/2 (49.28 s). Arena covers four source-parity
cases, three runtime programs, zero-size/multi-chunk/reset/repeated release/drop,
both allocation failures and retry, plus five rejection pairs and ten
no-object/no-IR checks. All tool targets rebuilt successfully after removing
the trial compiler patch; `src/` and `include/` have no candidate differences.
`g07_arena_test` and the previously failed Arena CTest are directed recoveries;
the complete 412/457, 447/478, 103/106 baseline is not rewritten. No E, push,
publication or other-worker changes; this is a completed candidate, not an
Accepted claim.

## Arena accepted; five legacy thread programs (source migration WIP)

`2fcb343d` is Accepted for the five library null initializers; independent
Arena/raw-construction CTest passed 2/2 (64.17 s). No compiler change is included.

The following batch changes only test source and its runner. Mutex uses
`make_shared` instead of binding a unique `make` result as shared. AtomicUsize
uses its existing shared `new` construction. Captures select shared handles;
owned environments use actual matching callable modes. Named callables transfer
with cede; spawn Results are unwrapped before joining. All thread counts and
iteration budgets are unchanged. No serial substitute, early success, unsafe
permission bypass, runtime/ABI/compiler change or oracle update was made.

Strict five-program run (`tools/scripts/test_rc13_legacy_threads.py`, logs in
`/private/tmp/toka-thread-five-final/results.json`) finishes **3/5**, not green:

| Original program | Actual result |
| --- | --- |
| g09_atomic_stress | Runtime/parity passes: 5 × 50,000; final value 250,000; both Counter drops still observed |
| g09_mutex_stress | Runtime/parity passes: 5 × 1,000 with existing yields; final value 5,000 |
| g09_std_atomic | Runtime/parity passes: 10 × 10,000; final value 100,000, sequential atomic assertions retained |
| g08_sync_mpsc_bounded | Still rejected: EnvironmentLifetimeUnproven at spawn; capacity 2, prefill 2, third send and worker retained |
| g08_sync_mpsc_multi | Still rejected: EnvironmentLifetimeUnproven at both spawns; two producers, delays and sum 300 retained |

MPSC workers now own their captured endpoints and move them into writable local
bindings on their single invocation. Dropping a mismatched type-erasure annotation
lets the concrete closure bind, but does not qualify its environment. The two
programs are still required positives: their refusal is not a passing oracle.
The runner asserts all five succeed and therefore currently exits nonzero.

Independent pending positive:
`tests/semantics/rc13_thread_migration_pending/sender_capture.tk` captures an owned
Sender, sends one integer and joins. Equivalent isolated source is rejected with
EnvironmentLifetimeUnproven in normal/shadow and produces neither object nor IR.
A shared Mutex owning-environment control runs successfully. These probes isolate
the missing Sender environment qualification; they do not establish whether its
underlying fix belongs to library qualification or the compiler fact producer.
No such fix was attempted in this source-only scope. Probe files and stderr:
`/private/tmp/toka-thread-five-probes`.

All five have matching normal/shadow return code and stderr. Only the three
runtime successes are directed recoveries; no full baseline figures are updated.
No E, push or release; other-worker RFC edits remain unstaged. This batch is an
incomplete WIP, not a request to accept all five or to weaken the environment gate.
