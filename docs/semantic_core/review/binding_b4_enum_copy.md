# Binding continuation: enum payload Copy proof

Base: `ba7d22292ca67f073959ef8ab528a74f92f888e1`.
Branch: `impl/binding-b4-enum-copy`.
Status: local implementation/validation; **not Accepted** and not complete JSON
or binding qualification.

## Corrected diagnosis

At JSON lines 546/563, the rejected source is the constructed outer
`Result<f64, string>`. It is legitimately a `NoSourcePlace` expression, separate
from its `buf[0]` argument. Its facts incorrectly claimed `ProvenCopy` while
also carrying cleanup liability, so the planner correctly rejected the tuple
as `ContradictoryFacts`.

`deriveSlice4CopyRecipe` tested the owning shape's `Kind == Enum` on **every**
recursive visit. Consequently, it skipped not only variant shells but also
their real payload fields, deriving `Always` for generic enums with non-Copy
payloads. This was not an initialized-extent or raw-extraction proof.

## Implementation

- Skip only a top-level enum variant shell. Visit payload fields with their
  complete physical types, including generic requirements, nested types and
  arrays. Unit variants contribute no storage edge.
- Keep existing explicit Drop, governed/Copy-domain, recursive/unknown and
  unique/shared rules; absence of Drop still does not imply Copy.
- Make consuming callable classification in the recipe and read-only query
  agree with the existing normal Copy query: consuming callable values are not
  Copy. Ordinary callable and raw/reference identity classifications are not
  changed.
- No planner consistency check is removed. No raw source is reclassified as a
  temporary and no raw_take, allocation, initialization, lifetime, CodeGen or
  ABI rule is changed.

The correct callable Copy classification initially exposed two regressions in
previously supported paths: standalone consuming-dyn discard and construction
of a holder with a consuming callable. They had indirectly relied on Copy to
infer absence of environment dependencies. The fix does not restore that false
Copy bit: standalone uses the already checked binding environment; consuming
constructor fields require a stable, non-alias binding of the exact declared
type and a complete dependency-free environment in the entry snapshot. Missing,
mutated, dependent, native-owner or mismatched environments cannot be guessed.
The existing capture, transfer, retain and cleanup implementations are unchanged.

The existing TKI recipe representation now records the actual payload
requirements. No schema or interface key changes. A source-hidden generic enum
is checked against its stored fields; an old/false `Always` comment cannot
authorize copying a resource payload.

## Verification

Final Debug tools build (`toka-tools`, including tokafmt/tokalsp) and planner
target build succeeded. The final combined targeted CTest run passed **7/7**
in **136.51 seconds**, without skipped cases:

- `toka_binding_b4_enum_copy`;
- `toka_stage1_binding_transfer`;
- `toka_binding_value_dependencies`;
- `toka_stage1_standalone_cede`;
- `toka_dyn_fn_binding_lifecycle`;
- `toka_stage0_codegen_authority`;
- `toka_explicit_cede_plan`.

The new test includes **six runtime/parity cases and ten negative parity
cases**, plus generic Copy-domain and source-hidden controls. These counts are
not a full PASS/FAIL result or release qualification.

`toka_binding_b4_enum_copy` covers:

- Copy scalar payloads and phantom generic unit enums;
- non-Copy resource and no-Drop governed payloads, including active empty
  variants, nested Option, fixed arrays and multi-field variants;
- generic and directly declared consuming callable fields retaining NonCopy;
- exact-once resource cleanup on explicit move and scope exit;
- the existing unique/shared enum lifecycle program, including surviving shared
  owners, and the existing nested generic Vec Copy witness program;
- bare-copy rejection, moved-source rejection, active borrow and ordinary
  borrowed-parameter transfer rejection, with normal/shadow parity and no
  object/IR output;
- generic `@Copy` declarations with and without the required payload bound;
- source-hidden scalar/resource consumers and a falsely unconditional recipe.

The holder escape counterexample first proves its borrowed callable binding is
admitted with the real owner dependency, then verifies the holder is not granted
independent-temporary authority. This is not a test that passes on an earlier
capture syntax error.

A newly reached existing `Path.with_extension` statement explicitly ceded a
fresh `string::from_with_len` result. Its one-line source migration removes the
redundant cede, without changing the algorithm or ceding its input. The original
path test and replacement/removal/dotted-extension/input-preservation controls
are retained. Explicit cede of a new owning temporary still rejects.

The independent values prove the actual plan, not only compiler success:
resource-bearing enum temporaries are `ProvenNonCopy + ConsumeTemporary +
DestinationAssumesLiability`; explicit named moves invalidate their source;
Copy values remain `CopyValue + NoLiability`. Non-Copy thin callable/no-Drop
values do not acquire fabricated cleanup liability.

The old `test_encap_slice4_audit.py` cannot currently complete its first positive:
its `return ... + capture()` is rejected with E04658 / IncompleteFacts. The
independent `72407112` compiler reproduces the same failure when run with its
own source library and working directory. That test/oracle is unchanged and
is not counted as passing.

## JSON and remaining scope

JSON passes its former outer-enum contradiction and now reaches the existing
HashMap raw-element transfer rejections (`RouteIneligible`, lines 29/34 and
199/200 in the current library). The original JSON positive and the smaller
`json_result.tk` remain failed positives, not new rejection tests or restored
PASS counts.

A separate final `toka_stage1_return_matrix` run failed after **123.47 seconds**
at its final `qualify_build_return_buffers` integration step, with those HashMap
diagnostics. Earlier return-matrix checks reached that step successfully; the
CTest as a whole is still **failed**, not part of the seven passing targets.

This patch does **not** certify that zeroing arbitrary `T` makes it initialized,
that the parser's raw buffer can always be transferred, or that all parser
failure paths clean up partial values. Those are separate obligations; Copy
classification cannot substitute for them. No JSON/HashMap library source has
been changed by this patch.

No full PASS/FAIL suite is run at this checkpoint. Earlier B1/B2/B3 and thread
refs remain unchanged; no push, PR, Actions or new Accepted marker is requested.
