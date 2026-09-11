# Concrete JSON factories — incomplete implementation checkpoint

Current follow-up: see `json_leaf_closeout_2026_09_11.md`. The figures and two
blockers below describe historical checkpoint `35af4aae`, not the latest work.

Base: `e1402bd23ef78f460021a432422689cfc4ae1fd9`.
Branch: `impl/json-leaf-factories`. No Accepted/freeze, push or PR.

## Fixed scope and implementation

The user authorized the concrete leaf slice in `binding_b6_json_boundaries.md`
lines 7–50, not public JsonFactory, generic bounds, recursive-container witnesses,
general receiver adaptation or new return rules.

- `json_leaf.tk` contains the shared whitespace/string scanner and decoder,
  and complete i32/string/str factories. Existing json entry bodies delegate
  to those factories; no additional public capability/trait was introduced.
- Borrowed str preserves escape text and points into input; owning string
  decodes into its own allocation. `rest` always remains input-dependent.
- Consuming match carries existing dependencies through an enum payload.
  Structural borrowed fields are detected by exact resolved declarations,
  not short names. A selected owned field does not inherit a sibling's data
  provenance. Actual borrowed method/member origins feed the existing lifetime
  validator; no declared-return-dependency fallback was added.
- Truncated `\u` reads are refused before reading beyond the closing quote;
  a decoded prefix is cleaned on that error path. Existing successful parsing,
  raw escaped str views, unknown-escape and nonhex lenience are retained.
  This adds `Incomplete unicode escape` only for the formerly out-of-bounds
  decoder path, not a general stricter JSON validator.

## Required tests, not a partial-green claim

`test_json_leaf_factories.py` has three independently registered CTest targets:

- `toka_json_leaf_factories`: concrete runtime/normal-shadow parity, 22 behavior
  checks, 8 factory cleanup checks, and 5 lifetime/dependency refusals with
  object/IR non-production. Instrumentation observes real string release
  instructions in an isolated library copy; production Drop is unchanged.
- `toka_json_leaf_entries`: exact existing FromJson trait/method bodies extracted
  from json.tk into a logical test module. Five behavior checks and four old
  target cleanup checks are required. This is not full JSON-module recovery.
- `toka_json_leaf_static_error`: a required successful return of the static error
  after a local input dies. It is not marked expected-failure or skipped.

Final directed run: **5/7 passed, 2 failed, 121.84 seconds**. Factory-core
passed all 22 behavior checks, 8 cleanup checks and 5 refusals. The four existing
gates (binding B3 static returns, binding value dependencies, Stage 1 return
source, shared aggregate handoff) passed. Entry behavior passes, but replacement
cleanup fails. The static-error positive remains rejected. Thus the three new
CTest targets are **1/3**, not a completed first-slice matrix.
Incremental Debug tokac build and `git diff --check` passed. No full PASS/FAIL
or release qualification run has been claimed.

## Two remaining blockers

### 1. Static enum payload provenance

`static_error.tk` receives E0455 for `input.buf` at `result.unwrap_err()`.
The error text really is a literal, but the whole Result dependency is carried
through the consuming extraction. This is a conservative false rejection,
not permission to mark the whole Result independent.

An attempted Sema enum-field summary implementation was rejected by execution
review as a broader lifetime-authority subsystem than the authorized existing
field plumbing. Its implementation file was **not applied**, and all associated
unfinished declarations/hooks were removed. No indirect retry was used.

The additional scope needing explicit confirmation is a source-visible,
validated static payload summary, keyed by exact enum declaration/variant/slot
and consuming extraction, with no summary for invalid/unresolved/changed
sources. It must never transfer an Err witness to Ok/rest or to the address of
a local descriptor. This is not an accepted design or permission to bypass
execution approval.

### 2. Existing entry replacement does not release the old string

The replacement probe expects one old-target release before returning and two
total after final target destruction. It exits 60: **zero old-target releases**.
The same success-replacement probe using the pre-slice json method bodies
also exits 60 (`--section entries --entry-baseline`). That diagnostic mode is
not an acceptance mode; old truncated-Unicode behavior is not tested as though
it already had the new refusal.

Relevant implementation: `CodeGen_Expr.cpp` whole direct-value replacement
only emits old-value cleanup when it finds a local DropFlag. A borrowed writable
parameter has no local owning DropFlag. This explains the observation, but no
CodeGen change was made in this slice. A valid fix must consume a checked
destination cleanup plan, not pretend the parameter owns its caller's binding
or infer Drop authority from a name. Scope for this repair must be settled
before the complete leaf/entry matrix can be called passed.

An earlier test concatenated entry bodies into an external main file and
produced IncompleteFacts on even baseline scalar assignments. This was a
test logical-module-identity limitation, **not a demonstrated receiver bug**.
The final harness imports those exact bodies from a logical module and uses
matching TOKA_LIB for the instrumented copy.

## Scope not absorbed

The full `binding_b4_enum_copy/json_result.tk` still fails at HashMap/Vec
`ElementDependenciesUnproven` and generic JSON raw-element `RouteIneligible`.
Those remain positive recovery targets, not new expected failures. Recursive
container witnesses and generic factory migration are not implemented here.
No thread, raw_take, Copy/Dup, Parser, TKI, ABI or interface-key change is included.
