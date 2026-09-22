# Build + return-matrix library-side closeout (Accepted)

Accepted implementation: `4750fbdda7c056a680fd3c01b587da9ddc671882`.
Acceptance is limited to this Build library migration and return-matrix package,
not RC13 release qualification. No further Build features are included.
Independent review reran Build metadata, full return-matrix and JSON factory:
**3/3, 89.85 seconds**, including the real hybrid build/run. Additional malformed
array old-value retention and UTF-8, embedded NUL and control-byte roundtrips
passed. The reviewer checked the full logs below, but did not rerun the full
suites. Review evidence:
`/Users/zhyi/GitDP/tokalang/validation/build-closeout-review-20260922.gC3Ljj/acceptance.md`.

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

Combined targeted CTest: **4/4, 188.05 seconds**.

## Fixed candidate and complete comparison

Implementation: `4750fbdda7c056a680fd3c01b587da9ddc671882` (Accepted in the scope above).
Evidence: `/Users/zhyi/GitDP/tokalang/validation/rc13-build-closeout-final`.
All four commands reported zero tracked-source changes. The manifest, preserved
RFC diff, exact commands, logs and comparison are retained there.

| Gate | Result | Seconds |
| --- | --- | --- |
| Complete tools build | passed | 11.88 |
| PASS | 457/459 | 366.24 |
| FAIL | 480/480 | 128.74 |
| Unfiltered CTest | 117/118 | 1529.82 |

Compiler SHA-256 after the complete tools build:
`838bcf49347de69a226b12e3190760f6378ff9b32b1b3e14f88988c54acd4f04`.

Against the accepted a08578e8 full run, the only restored positive is
`g10_build_hybrid_test.tk`; the restored old CTest is
`toka_stage1_return_matrix`. `toka_build_metadata` is new, not a recovery.
There are no new failures or recorded abnormal exits. The remaining positives
are `g15_stdx_toml_test.tk` and `g18_stdx_template_test.tk`; the remaining CTest
failure is `toka_stage1_indirect_parameter_cede`.

The implementation diff contains only Build library representation/call-site
changes, regression fixtures/scripts, CMake registration and this record. No
compiler, byte contract, raw_take, native runtime or TKI-key changes. No push,
PR, publishing or freeze-ref changes. The independent RFC remains dirty at
SHA-256 `1b73b0fc46f9a700e1bdc9d0a0031614e1d9a4b398ce97f6a6423972474d70e2`,
unchanged and excluded from both local commits.

TOML/Template, source-hidden callable and the previously recorded aggregate
extern lowering limitation remain separate. E, pushing and publishing remain
paused. The independent RFC is neither staged nor modified by this delivery.
