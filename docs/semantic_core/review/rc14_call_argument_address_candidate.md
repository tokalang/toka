# RC14 concrete-payload call-address candidate

**Status:** Local implementation candidate; not independently Accepted.

## Root cause and bounded correction

The published RC13 compiler accepted a concrete payload argument obtained
through a unique or shared root but passed the *handle storage* to a captured
plain-value formal. The callee interpreted that address as the payload. The
[pre-fix receipt](rc14_call_argument_address_baseline.md) includes runtime and
LLVM IR evidence. A direct `Owner` value with a unique field was already
correct; the error depended on the **outer source root**, not the field type.

`CodeGen::genCallExpr` now uses the checked formal and source value view to
select `getEntityAddr` only for a concrete payload capture from a proven named
source place. `cede`, exact ascription, `unsafe` and name-side payload-write
wrappers are unwrapped only when their checked view or storage selection is
preserved. Conversion casts, arbitrary unary selectors and abstract whole
values do not enter the new path. Existing handle-identity, consuming unique,
init-place and rvalue materialization paths remain distinct. The ordinary
method argument route already used the payload address and was left unchanged.

In generated LLVM IR, the `read_unique` argument now comes from a load of the
unique handle slot, while `read_shared` comes from the shared carrier's data
field. The test follows each call operand's SSA definition back to that
storage layer; matching a temporary variable name alone would not suffice.

## Validation on the fixed local source

- Fresh external Release tools build completed.
- New `toka_rc14_call_argument_address` CTest and 9 related call, Stage 0,
  generic, binding and shared-ABI tests passed **10/10** in a targeted run.
- No-exclusion CTest passed **125/125** (2701.40 s).
- Full PASS suite passed **459/459**, failed 0 (644.59 s).
- Full FAIL suite passed **480/480**, failed 0; no Bless or oracle updates.
- Independent conformance suite passed **325/325**.
- `git diff --check` passed. No Parser, Sema admission, TKI/ABI layout,
  permission rule, refcount algorithm or receiver-lowering implementation was
  changed.

The nested-owned-`Vec` `E04662` issue, compiler-speed investigation, and
post-1.0 RFCs remain outside this candidate. No push, PR, tag or release is
claimed.
