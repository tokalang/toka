# TOML / Template library closeout (Accepted with callable correction)

Accepted scope: library candidate `123a3aa4` together with lexical callable
correction `8057bfab`; test-environment correction `99c89a88` is also accepted.
Independent review: `validation/callable-tail-review-20260922.xWv8to/review.md`.
It reran 3/3 related CTest (87.14 seconds), the TKI anti-forgery script and the
three original audit programs. No new full baseline was run. The fn_ proposal,
captured-callable E0721, source-hidden qualification and release acceptance are
not included. The candidate narratives below are retained as historical records.

This package follows Build acceptance at `4750fbdd`, recorded separately in
`611ef306`. Build is not extended. The implementation changes library storage
and its affected source API, not compiler qualification or recursive-container
proofs. E, source-hidden callable and extern aggregate lowering remain separate.

## Representation and source API

- TOML previously stored `Vec<TomlValue>` inside `TomlValue`, then placed those
  values in `Vec<TomlEntry>`. It now stores an owned, validated TOML array spelling
  in `ArrayValue(string)`. Temporary parsed lists contain only scalar/string
  variants, not recursively owning nodes. Clone copies owned spelling; array
  access reparses with the existing depth/error rules and returns independent
  elements. This costs a scan and temporary allocations per array access, not
  constant-time indexing. Direct enum construction changes source API; existing
  `TomlDocument::parse/get` and value accessors remain intact. Malformed manually
  constructed array spellings return None from accessors, never trusted metadata.
- Template lists previously used `HashMap<string, Vec<string>>`. They now use
  flat owned key/value entries plus an empty-list marker. Replacement releases
  old entries, preserves other keys and order, and copies input strings into
  owned entries before consuming the input list. Lookup/replacement scans entries;
  repeated keys and copies cost memory/time. Empty and missing lists differ.
- A `Vec<FuncHandler>` also required unsupported callable-containing raw element
  qualification. It is removed rather than given a special proof. Applications
  use `enable_func(name)` and `render_with(context, dispatcher)`, where the thin
  dispatcher takes `(name: str, arg: str) -> Result<string, string>`. No callback
  is stored or escapes the render call. `render` retains builtin functions and
  returns an error for an enabled custom function without a dispatcher. The
  original two mock callbacks and their failure assertions are preserved behind
  the test dispatcher. This does not qualify source-hidden callable environments.
- Template scanning already uses byte offsets. Its substring helper now uses
  byte `substr`, rather than character `substring`. UTF-8 callback and pipeline
  assertions cover this library-only correction. The push_char-to-push_byte_raw
  spelling change is not a separate behavior fix: push_char already delegates
  to raw-byte insertion in the current string implementation.

No JSON-text conversion, new unsafe storage operation, compiler change, runtime,
ABI/TKI-key change, raw_take extension or byte-contract modification is included.
The TOML original test is unchanged; Template test changes are temporary cede
spellings and the dispatcher API, not weakened expected results.

## Directed verification

`toka_toml_template` checks both original programs plus an owned-values pressure
program and two local-view escape negatives. Every case runs normal/shadow
checking and object/LLVM emission, with target diagnostic and no-artifact checks
for negatives. Positive binaries run their assertions.

The pressure program repeats 100 times: nested arrays, depth/escape/duplicate
failures after allocated prefixes, clone/input replacement, array bounds,
template list replacement/empty/missing/other-key isolation, NUL and UTF-8,
custom callback success/failure, missing dispatcher and strict-variable failures.
It verifies returned owned values remain valid after their input/context dies.
`leaks --atExit` reported 0 leaks / 0 bytes; the initial sandbox task-port failure
was not counted as a measurement. Actual permitted measurement is saved under
`validation/rc13-toml-template-directed/leaks.log` (with the platform's restricted
process inspection notice retained).

Related directed CTest (`toka_toml_template`, `toka_build_metadata`,
`toka_json_factory_values`): **3/3, 92.87 seconds**. Full output is retained in
`validation/rc13-toml-template-directed/ctest.log`.

## Fixed candidate and full comparison

Implementation candidate: `123a3aa454222545deb222d0e837ac24f1310109`.
Full evidence: `/Users/zhyi/GitDP/tokalang/validation/rc13-toml-template-final`.
The commands, revision, source manifest, preserved RFC diff and compiler hash
are recorded in `metadata.json`; every phase reported zero tracked-source changes.
The documentation-only result record is subsequent, not a different tested
implementation. No implementation acceptance or release is claimed here.

| Gate | Result | Seconds | Command exit |
| --- | --- | --- | --- |
| Complete tools build | passed | 10.13 | 0 |
| PASS programs | 459/459 | 375.46 including script tail | 1 (see below) |
| Complete FAIL suite | 480/480 | 129.85 | 0 |
| Unfiltered CTest | 118/119 | 1522.42 | 8 |

Compared by case name against `4750fbdd` (457/459, 480/480, 117/118):
the only restored positives are `g15_stdx_toml_test.tk` and
`g18_stdx_template_test.tk`. There are no newly failing cases or recorded
abnormal exits in those suite comparisons. `toka_toml_template` is an added
passing test, not a recovered old CTest. The sole remaining CTest is unchanged:
`toka_stage1_indirect_parameter_cede`, whose source-hidden factory binding fails
with `CallableReturnEnvironmentUnavailable` before reaching the spelling check.

### PASS command is not wholly green

After all 459 programs pass, `test_pass.sh` now reaches its appended checks.
Public unsafe/raw naming passes, but `test_tki_unsafe_revalidation.sh` exits 1
when it replaces TOKA_LIB with its temporary trusted directory and emits the
system interface. The resulting core/traits error is E04648. The complete PASS
command therefore still exits 1; later appended checks are not executed.

The old two-failure baseline stopped before these appended checks. This is a
newly reached failure, not an added PASS-program failure; this comparison alone
does not date the underlying problem. The condition matches the previously
recorded TKI environment limitation. A bounded standalone reproduction and
actual diagnostic are saved in `validation/rc13-toml-template-directed/` as
`tki-tail-trace.log` and `tki-tail-error.log`. No oracle or script was changed to
hide it. This remains a release follow-up, outside TOML/Template.

Final compiler SHA-256:
`a76aa12ce7c30459a3cb9c16fe13712da88607779c5b41379c7982138f90a67b`.
Preserved independent RFC SHA-256:
`1b73b0fc46f9a700e1bdc9d0a0031614e1d9a4b398ce97f6a6423972474d70e2`.

No push, PR, freeze-ref movement or publishing. The independent RFC modification
is preserved and excluded from this package's commits.

## Incremental correction after dispatcher review (not yet Accepted)

Review: `validation/toml-template-review-20260922.gU9niJ/review.md`.
Callable correction: `8057bfab`; test-environment correction: `99c89a88`.

Sema now retains a lexical value selection instead of replacing it with a
module function or specialization-cache entry. A non-callable local also
shadows a function, and is rejected rather than calling that function. Indirect
calls carry the selected VariableExpr (resolved codegen name, binding ID and
type); AST clones resolve again. CodeGen uses that value through ordinary
variable lowering, bypasses the module-function spelling lookup, and does not
fall back to a constructor/global on a failed indirect lowering. Existing
argument, receiver, transfer, cleanup and ABI logic is unchanged.

`toka_callable_shadow` includes the three original audit sources, actual supplied
Template dispatcher invocation, local/nested/dyn-fn/generic bindings, an explicit
module-qualified control, and non-callable-shadow rejection. It executes both
public documentation examples too. Runtime, normal/shadow parity, object/LLVM
checks pass. The minimal `apply` IR loads and calls `%closure_func`, with no
direct call to `@dispatch`. Enabled custom functions still fail under `render`
when no dispatcher was supplied, even with an unrelated global `dispatch`.
`docs/stdx_template_v1.md` now documents enable_func/render_with and the actual
signature, and no longer recommends the removed API.

Complete incremental tool build passed. Related CTest groups:

- generic-body qualification, ordinary caller cede, dyn-fn lifecycle,
  TOML/Template and callable shadow: **5/5, 147.34 seconds**;
- original call-transfer freeze: **1/1, 102.57 seconds**.

Logs: `validation/rc13-callable-shadow-{build-final,ctest,call-freeze}.log`;
standalone matrix: `validation/rc13-callable-shadow-directed.log`.
Compiler SHA-256:
`c306234e4e87b3eeab8e05eeff6cae2da3b89e78341a8dac858aac819e1771b8`.
These incremental results do not replace the earlier full-suite numbers.

### All appended PASS checks attempted; PASS command remains unqualified

The unsafe TKI test now creates a full source-only temporary SDK and compiles
inside its isolated directory, so the selected core is actually inside the
declared trust root. A partial root or symlink outside it was not made trusted.
All forged-interface, local-shadow and package-spoof assertions remain intact;
the unsafe TKI script passes. Five additional SDK scripts now receive the same
out-of-tree build path instead of using missing checkout-local binaries. No
semantic assertions were removed or blessed.

All **22** checks appended by `test_pass.sh` were executed, continuing past
failures to obtain the complete list. Five path-dependent checks were rerun
after explicit build routing: three pass, two reach real assertion failures.
The combined actual result is **13/22**, not an end-to-end PASS-command success.
Original and rerouted attempts are both retained:

- `validation/rc13-pass-tail-checks/results.json` and per-check logs;
- `validation/rc13-pass-tail-checks-routed/results.json` and per-check logs;
- `validation/rc13-pass-tail-consolidated.json` maps every final result to its
  attempt, exact command and original return code.

Remaining first failures (unmodified assertions, no introduction-date claim):

| Check | Observed failure |
| --- | --- |
| TKI cache validation | Test 7.10 still generates removed `LocalBox<'T>` syntax; E01268 |
| memory summary | `source_summary.tk:48`, `ms_global = value`: E04661 TypeIncompatible |
| experimental readonly | same memory-summary fixture fails before its target checks |
| cede obligation evidence | expected fulfilled return-transfer record is missing |
| semantic diff preview | `toka preview` output differs from direct preview |
| semantic replay | 39/56 cases pass, 17 fail; individual source/diagnostic failures retained |
| Outcome body recheck | strong-alias Outcome rejection assertion fails; requires separate analysis, not treated as harmless noise |
| semantic cache invalidation | 9/13 cases pass; 4 fail, including member-result IncompleteFacts |
| incremental build | Tests 0–9 pass; native-build reference step uses a partial/symlink SDK and fails E04648 |

These are newly reached follow-ups after the program corpus became green,
not nine new failures in the 459-program corpus. That corpus was not rerun in
this incremental correction. Native-build/source-hidden contracts, ABI,
raw_take, E, pushing, freezing and publishing were not expanded.
