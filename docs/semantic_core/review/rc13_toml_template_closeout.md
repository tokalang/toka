# TOML / Template library closeout (candidate, not Accepted)

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
