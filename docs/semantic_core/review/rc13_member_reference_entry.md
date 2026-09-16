# RC13 member-reference entry candidate

Tested implementation: `6952a25b8c439ad2057309ae9b0021670a577c5b`.
This is one complete implementation/verification package, not a new language
decision or an Accepted declaration. The previous 18-test migration and corrected
IR gate are Accepted at `29d18390`.

## Cause and implementation

Normal member checking already produces a reference for `obj.&field`, but both
preflight readers returned the field's physical value type. The binding plan
therefore compared e.g. `i32` with `&i32` and rejected before the intended PAL
operation. The place classifier also treated reference construction as transfer
of the named member slot, inconsistent with `ReferenceConstruction` facts.

The implementation now:

- Projects `&`/`&#` member selection to its actual reference type in both readers,
  following the existing normal member rules. No expected destination type is
  used as proof of source permission. Existing reference fields remain reference
  identity selections, not references to their local descriptor storage.
- Classifies a constructed reference as a new reference value, retaining the
  exact selected member as its referent/dependency. An empty transfer source does
  not mean dependency-free or independently owned: the admitted records carry
  `ReferenceConstruction`, `CopyIdentity`, `NoLiability`, and the member path.
- Limits its payload flow ceiling to the actual field/access-path capability.
  Temporary classification cannot manufacture writable storage. Readonly,
  frozen-field and readonly-parameter counterexamples remain rejected.
- Uses selected storage, not contents, when collecting construction origins.
  Static characters in a local `str` field do not give its descriptor a static
  lifetime; `return holder.&view` is rejected. Selecting an existing reference
  field instead preserves its actual referent.
- Passes a successful member-loan receipt through the already established PAL
  argument check. The receipt is reset for each evaluation and not cloned.
  Only its exact transient loan can be excluded; other overlapping loans,
  invalidation/rebinding operations, mismatched views and ambiguous current
  reference targets retain the ordinary checks. PAL's checking algorithm was
  not changed. The receipt unit test now also checks member AST clone isolation.

The implementation changes are in Sema and private AST metadata. No Parser,
CodeGen, runtime, library, public ABI/TKI or permission-contract change was made.
No broad source-proof or container qualification mechanism was introduced.

## Concentrated validation

`toka_member_reference_entry` runs seven programs covering:
field/interior permission, disjoint siblings, direct and equal-view-wrapped call
arguments, existing reference-field return, wrapper preservation, unique/shared
owner exact-once cleanup, and a live sibling after another field was invalidated.
Those seven include the original `blockers/member_reference.tk`, which runs
successfully unchanged.

The four original FAIL sources and their expectations are byte-for-byte unchanged:

| Original case | Actual target reached |
| --- | --- |
| pal_member_mut_borrow_duplicate | E0441 at the second member borrow |
| pal_member_mut_borrow_payload_read | E0441 at the original payload read |
| pal_member_mut_borrow_payload_write | E0441 at the original payload write |
| readonly_member_ref_decl_upgrade_from_plain_cast | E04573 at the attempted writable reference declaration |

Fourteen additional negatives cover readonly/frozen access, readonly bases and
parameters, parent/child overlap, duplicate arguments, a pre-existing loan,
uninitialized/moved fields, descriptor and referent escape, wrappers, and rejection
rollback. The rollback fixture requires exactly its intended capability error,
without a spurious loan conflict on the following valid borrow.

Both shadow modes match normal return code and stderr. All 18 negatives match
the target semantic diagnostic and source line in check-only, object and
`--emit-llvm` modes; all 36 rejected outputs are absent. Valid object and LLVM IR
controls verify the output modes. Evidence assertions check reference type,
member referent/dependency, source disposition, liability and permission ceiling.

The new gate, prior negative-purpose gate and loan-receipt unit test pass **3/3,
87.46 s**. The prior purpose manifest updates only these four entries from blocked
to repaired. The other nine entries, FAIL sources and expectations are unchanged.

## One fixed full run

After saving the implementation checkpoint, tools, PASS, FAIL and CTest ran once
in sequence on the same build. Every stage checked the tracked-source fingerprint;
no input changed. The other-worker RFC diff remained untouched throughout.

| Suite | Result | Elapsed seconds |
| --- | --- | --- |
| Complete tools build | Passed | 15.57 |
| PASS | **424/457** | 304.97 |
| FAIL | **469/478** | 126.31 |
| CTest | **109/111** | 1092.68 |

No added failures or unexpected abnormal exits in any suite.

Against the previous complete 421/457 PASS baseline, three cases recover:
`g04_token_interior_mut.tk`, `g08_pal_member_shared_borrow_payload_write.tk`,
`g08_pal_stress_test.tk`. Against the accepted negative migration's **465/478**,
exactly the four listed FAIL cases recover. Against the older complete **447/478**,
the apparent 22-case improvement includes the earlier 18 migrations and is not
attributed wholly to this implementation.

The CTest increase from 107/109 to 109/111 is two added passing tests:
`toka_rc13_negative_purposes` from the prior package and
`toka_member_reference_entry` from this package. It is not recovery of old failed
CTests. The same two failures remain: source-hidden indirect callable qualification
and `build_return_buffers` in the return matrix. The same other nine FAIL cases
remain blocked; they were not repaired or reclassified as passing here.

Raw logs, fixed-input manifest and scripts:
`/Users/zhyi/GitDP/tokalang/validation/rc13-member-reference.uluW37`.
The full run is under `full/`, targeted logs are `targeted.log`, and detailed
member records/runtime outcomes are `gate/results.json`. Portable summary:
[rc13_member_reference_results.json](rc13_member_reference_results.json).

E, push and release remain disabled. Existing frozen refs were not moved. RC13
is not declared ready; the remaining failures stay separately recorded.
