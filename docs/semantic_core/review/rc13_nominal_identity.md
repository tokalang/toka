# RC13 alias / nominal construction candidate

Final tested implementation: `7477d4f748af55382088fe2487198641cf435299`, following
the main implementation checkpoint `a301b8cf`. This package is a candidate for
review, not an Accepted declaration. The prior member-reference package remains
Accepted at `6952a25b`.

## Implementation boundary

The failures were not resolved by treating newtypes as their underlying type.
Normal Sema now returns the resolved nominal type from a validated constructor;
read-only consumers preserve that identity and consume existing declarations.
Copy classification is prepared as a type property, including for signature-only
and source-hidden use, rather than depending on a constructor body having run.

The important identity/representation distinctions are:

- Transparent aliases remain synonyms. Strong aliases retain their own nominal
  identity. Layout resolution no longer force-expands nested nominal arguments:
  `StrongBox<Id>` stores `Id`, not its underlying `Leaf`.
- Private `NominalLayoutOrigin` / source syntax / parameter metadata supplies
  field shape and whole-value contracts. It is not an identity-equivalence or
  trait-inheritance link. Abstract `T` stays abstract in a newtype's generic
  body; specializing it to a known shape does not authorize field access.
- Complete morphology is retained for unique/shared/reference fields. The new
  layout link does not make a field or its pointee writable. Readonly inner
  reference controls still reject writes despite a writable outer binding.
- Nominal declarations materialized before the shape filling pass receive only
  missing resolved member types from the exact layout origin. No reinterpretation
  of old field syntax in the caller's scope is used.
- The alias declaration's lexical owner is retained for its own validated
  `@Encap` policy. Its own Drop is recorded; the defining-module check is not
  bypassed. Underlying NonCopy restrictions remain NonCopy, even without Drop.
- Scalar newtypes use a cycle-checked, read-only query of declared numeric/bool
  representation for Copy/ownership/dependency properties. The expression type
  remains nominal. Unknown, generic, shape-backed or handle targets do not get
  a scalar exemption; the free-type-parameter capture-avoidance marker is honored.

No Parser, CodeGen, runtime, library, ABI or TKI format change is included.
No trait implementation is inherited merely because representation matches.
E and push remain disabled. The other-worker RFC diff remains unstaged and
unchanged; existing frozen refs were not moved.

## Original goals and exact tests

The following original files are unchanged:

- `tests/semantics/rc13_negative_purposes/blockers/alias_good.tk`: constructs and
  runs successfully, returning its intended value **30** (not falsely counted as
  an exit-zero program).
- `tests/pass/g07_alias_generic_test.tk`: ordinary and generic StrongNode plus
  transparent aliases compile and run successfully with their original assertions.
- `tests/fail/alias_generic_test.tk` and its snapshot: construction succeeds;
  the original assignment at line 25 reaches **E0408**, with the exact existing
  nominal-mismatch diagnostic. Neither source nor oracle was migrated.

`toka_nominal_alias_identity` covers 16 runtime controls, 13 semantic negatives,
26 object/LLVM-IR non-production checks and two valid artifact controls. Both
shadow modes match normal diagnostics. In addition, source-visible and
source-hidden TKI-plus-object consumers link and run.

The matrix includes same-nominal assignment, both transparent-alias directions,
distinct nominal and underlying-target rejection, nested nominal arguments,
abstract field rejection, writable/readonly reference permissions, actual borrowed
return dependencies and local escape, trait non-inheritance and own trait dispatch,
NonCopy/no-Drop transfer, unique/shared whole-value transfer, remaining shared
owners, old-target replacement cleanup, own Drop exact-once, and rejection rollback.
The failed nominal transfer must not produce E0438/E0410 on subsequent source use.

Only `alias_generic_test` changes status in the earlier 31-purpose manifest. The
other eight blocked entries and all original FAIL sources/expectations are intact.

## Regression found and corrected before final delivery

The first complete run on `a301b8cf` recovered `g07_alias_generic_test` but newly
failed `g04_match_range`: read-only strong-alias lookup rejected scalar `Char16`
because it has no shape layout declaration. Its result was **424/457, 470/478,
110/112**, and is retained under `full/`; it is not represented as a clean run.

`7477d4f7` fixes that regression by separating scalar representation properties
from identity. `Char16` was not renamed to `u16`, and `g04_match_range` was not
edited. The gate adds that original program, scalar newtype copy/struct-field
controls, and scalar-to-underlying / distinct-scalar nominal rejection. Scalar
construction tests use the existing explicit `as` conversion, not a new implicit
conversion or relaxed type-ascription rule.

## Final fixed full run

All tool targets, PASS, FAIL and CTest ran on fixed `7477d4f7` after the correction.
Every stage compared the tracked-source hash manifest; none changed. Compared
with the accepted **424/457, 469/478, 109/111** baseline:

| Suite | Final result | Seconds | Named change |
| --- | --- | --- | --- |
| Tools | Passed | 6.03 | All targets |
| PASS | **425/457** | 364.02 | `g07_alias_generic_test.tk` restored |
| FAIL | **470/478** | 119.37 | `alias_generic_test.tk` restored |
| CTest | **110/112** | 1144.69 | New `toka_nominal_alias_identity` passed |

Added failures **0**, unexpected abnormal exits **0**. The CTest delta is one
new gate, not recovery of an old failed CTest. `g04_match_range` is confirmed
restored from the intermediate regression but is not counted as a recovery from
the accepted baseline, where it already passed.

The same two CTest blockers remain: source-hidden indirect callable qualification
and `build_return_buffers` in the return matrix. The other eight FAIL blockers
remain failed, and no unrelated implementation work was undertaken to fix them.

Within this final full run, the nominal gate, nominal-ID unit, G, enum Copy and
negative-purpose gates all pass (individual results are in the JSON). Earlier
targeted **5/5, 148.99 s** results precede the scalar correction and are not
relabeled as a new targeted run on the final revision.

Evidence: `/Users/zhyi/GitDP/tokalang/validation/rc13-nominal.ua4ddT`.
`full-final/` is authoritative for the final baseline; `full/` preserves the
superseded run and its regression. `scalar-gate/results.json` contains the final
runtime/parity matrix; the original files and their artifacts are retained there.
[Machine-readable results and named comparisons](rc13_nominal_identity_results.json).

RC13 is not declared ready. This package closes the stated nominal-construction
candidate, with remaining release failures separately recorded.
