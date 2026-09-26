# RC14 concrete-payload call-address acceptance

**Status:** Accepted for this repair at
`174633c08a7a838340852ceb867bb36b31b031a5` after independent incremental
review. This is not RC14 release qualification. The initial `4fd865e7`
candidate was rejected because wrapped concrete payload arguments could still
fall back to the handle-slot address.

## Root cause and bounded correction

The published RC13 compiler accepted a concrete payload argument obtained
through a unique or shared root but passed the *handle storage* to a captured
plain-value formal. The callee interpreted that address as the payload. The
[pre-fix receipt](rc14_call_argument_address_baseline.md) includes runtime and
LLVM IR evidence. A direct `Owner` value with a unique field was already
correct; the error depended on the **outer source root**, not the field type.

`CodeGen::genCallExpr` uses the checked formal and source value view to select
`getEntityAddr` only for a concrete payload capture from a proven named source
place. `cede`, `unsafe`, name-side payload-write, and same-storage
ascription/conversion wrappers are unwrapped only when the checked storage and
complete nominal type match. A top-level writable view may be attenuated, but
not amplified; nested type, pointer morphology, blocked/nullable and abstract
whole-value differences remain distinct. A concrete payload formal cannot
fall back to the old handle-identity address after wrapper recognition fails:
real conversions instead materialize one temporary from a single evaluation.
The ordinary method-argument route now makes the same source selection.
Existing handle-identity, consuming unique and init-place paths remain
separate; receiver lowering was not changed.

In generated LLVM IR, the `read_unique` argument comes from a load of the
unique handle slot, while `read_shared` comes from the shared carrier's data
field. Wrapped direct and method writes likewise receive a payload address
loaded from the owning handle slot, not the slot itself. The test follows each
call operand's SSA definition back to that storage layer; matching a temporary
variable name alone would not suffice.

## Validation on the fixed local source

- The external Release compiler rebuilt successfully after this revision.
- The RC14 gate and 9 related call, Stage 0, generic, binding and shared-ABI
  tests passed **10/10**. Nine independent audit reproductions compiled and
  ran with exit 0; the new fixture also checks method wrappers and that a real
  numeric conversion evaluates once without writing through its source.
- Full PASS passed **459/459** and full FAIL passed **480/480**; no Bless or
  oracle updates.
- Complete no-exclusion CTest under permitted process-inspection access ran
  **124/125**; `toka_channel_storage` timed out under `-j 4`. The unchanged
  test passed **1/1** alone in 46.30 s. This is not represented as a single
  green 125/125 run. An earlier sandboxed attempt was invalid because several
  harnesses could not invoke `/bin/ps` and timed out.
- Independent conformance suite passed **325/325**.
- Independent incremental review reran the nine original audit sources and
  additional method-conversion, generic-owner and readonly controls: **48**
  mode checks and **11** positive runtime programs passed. The registered
  RC14 gate passed independently **1/1** (14.41 s). The review did not rerun
  the full suites.
- `git diff --check` passed. No Parser, Sema admission, TKI/ABI layout,
  permission rule, refcount algorithm or receiver-lowering implementation was
  changed.

The repair is closed; do not append further address-selection features here.
The nested-owned-`Vec` `E04662` issue, compiler-speed investigation, Channel
qualification stability and post-1.0 RFCs remain separate. No push, PR, tag
or release is claimed.
