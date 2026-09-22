# Build + return-matrix library-side closeout (candidate, not Accepted)

The preceding anonymous-record/task-result/byte-owner package was accepted at
`a08578e8`; documentation-only acceptance commit: `1ccf4bc1`.
This delivery is separate and does not extend its byte-only contract.

## Confirmed cause and library migration

Independent probes in `validation/rc13-build-probes` distinguish the real issue:
`Vec<string>` and scalar/string container parsing work. A record containing a
`Vec<string>` fails when used as a HashMap value; replacing that field with an
owning string succeeds. HashMap's value-dependent methods also instantiate
`Vec<Value>`, exposing the same unproved nested raw-storage dependency at
`vec_pop_raw_tail` and the HashMap take bridge. Later JsonFactory errors are not
the first cause.

Build now uses `BuildStrings` for metadata dependencies, roots, dirty dependency
lists and DFS chains. It owns a single length-prefixed string plus a scalar
count; no raw pointer or internally borrowed field was added. Consequently
`ModuleSnapshot`, `RebuildModuleInfo` and `DfsResult` contain only structurally
closed fields, and the existing compiler rules apply without new exceptions.
The command-line argument vectors remain `Vec<string>`.

This is a Build source-API representation change, not a compiler/native ABI or
TKI-key change. `BuildStrings` is re-exported by build: `new`, `push`, `len`,
`clone`, owned `get`, and owner-dependent `borrow -> str`. Indexed access scans
the flat encoding (linear in the preceding entries); it is not advertised as a
general Vec replacement. Arbitrary UTF-8, empty items and delimiters are preserved
by byte lengths, with bounds/overflow checks at access. Parser failures discard
the constructed temporary and retain the receiver; the returned remainder still
borrows the input. A scoped parser result ends before rebinding its input view.

The JSON wire schema remains arrays/maps with the same field names. Build's
string writer now reuses the existing JSON escape writer so quotes, backslashes
and control characters survive roundtrips. The three consuming buffer helpers
retain their original signatures, capacity/address reuse and exact-once release.
Temporary call-result cede spellings in the affected Build implementation were
migrated; named transfers remain explicit. No compiler checks or old rejection
oracles were changed.

## Directed verification

- Original `g10_build_hybrid_test.tk`, unchanged: compiled, built the C++ shim
  and generated Toka program, linked them, and ran the hybrid executable.
- `toka_stage1_return_matrix`: passed, including its original negative cases,
  three real private buffer helpers and five parsers.
- Expanded buffer fixture: nonempty nested metadata, Unicode/escape/empty item
  roundtrips, input replacement after parsing, independent clone/replacement,
  transitive and memoized rebuild dependency chains, and partial parse failures.
- Test-only BuildStrings Drop receipts are paired with the actual core/string
  free instruction to check partial-prefix and replaced-target storage cleanup;
  a Drop callback alone is not treated as proof of release.
- Added `toka_build_metadata`: growth, serialization, owned accessor lifetime,
  borrowed local escape refusal, normal/shadow parity, object/LLVM checks, and
  the original hybrid build runtime. No dangerous negative is run.
- Related JSON factory and accepted byte-owner gates remain passing.

Combined targeted CTest: **4/4, 188.05 seconds**. Full-run results will be recorded
only after one fixed-candidate tools/PASS/FAIL/CTest comparison completes.

TOML/Template, source-hidden callable and the previously recorded aggregate
extern lowering limitation remain separate. E, pushing and publishing remain
paused. The independent RFC is neither staged nor modified by this delivery.
