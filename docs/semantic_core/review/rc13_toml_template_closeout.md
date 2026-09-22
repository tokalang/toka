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
  byte `substr`, rather than character `substring`. ASCII upper/lower preserve
  non-ASCII bytes instead of encoding each byte as a character. UTF-8 callback
  and pipeline assertions cover this library-only correction.

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

Related directed CTest and final fixed-candidate comparison will be recorded
below after completion. Original full baseline remains `4750fbdd`:
457/459 PASS, 480/480 FAIL, 117/118 CTest. Directed passes do not replace it.

No push, PR, freeze-ref movement or publishing. The independent RFC modification
is preserved and excluded from this package's commits.
