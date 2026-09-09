# Shared parameter reception — implementation qualification

Base: `855e8591ae1d06d8e7d0802089bdc41af4c80430`.
Scope authorized by the user: complete shared body reception, symbol registration,
read/handle/cede/return addressing. No other type family, parameter ABI, carrier
layout or reference-count algorithm changes.

## Fix

Resolved shared parameters now use the same incoming carrier address as explicit
shared parameters. Morphic symbol registration preserves the shared projection
metadata. Neither synchronous direct carriers nor asynchronous full frame
carriers gain an extra pointer-wrapper layer or a captured-slot load flag.

The final failing standalone shared cede was an expected-type leak: the enclosing
function's scalar return context collapsed the expression's cleanup type to its
pointee. For an admitted shared-discard plan only, Sema now checks that expression
without the unrelated return context. Source invalidation, borrow checks and the
existing reference-release implementation are unchanged; no CodeGen ownership
facts are reconstructed.

## Minimal IR comparison

Names below are shortened from the fixture's generated IR, not new IR algorithms.
The test decodes the exact generic declaration identities and checks actual bodies.

Before, synchronous morphic cede addressed a pointer-sized wrapper as a carrier:

```llvm
define ... @generic(ptr %value) {
  %value.addr = alloca ptr
  store ptr %value, ptr %value.addr
  %moved = load { ptr, ptr }, ptr %value.addr ; wrong level/extent
}
```

After, the signature is unchanged and the real incoming carrier is used directly:

```llvm
define ... @generic(ptr %value) {
  %moved = load { ptr, ptr }, ptr %value
  store { ptr, ptr } zeroinitializer, ptr %value
}
```

Async entry still copies the complete incoming carrier into its existing frame:

```llvm
%carrier = load { ptr, ptr }, ptr %value
store { ptr, ptr } %carrier, ptr %frame.value
; reads/transfers use this complete frame carrier, without another pointer load
```

Standalone discard uses the existing shared reference release, never a direct
pointee Drop on the carrier slot. Its synchronous body has exactly one
`atomicrmw sub`; pointee cleanup is behind the last-reference branch.

## Gates

`toka_shared_parameter_abi` requires all existing 23 cases, with no skips or xfail:
explicit/hatted-generic/morphic parameters, borrow/cede, sync/async, remaining
owner/exact-once, both shared sync positives, and source-hidden concrete/morphic
provider TKI + object executions. The original raw_take shared-generic isolation
and E04583 rejection of borrowed `.start` remain mandatory no-artifact controls.
IR carrier-level checks are additional hard assertions in the same test.

Result: **23/23 plus IR assertions pass**. Directed CTest also passes **4/4**
(shared ABI, standalone cede, public thread responsibility, existing sync storage),
90.53 seconds. Incremental tokac build and diff checks pass. This is implementation
qualification, not a claim of independent external acceptance.

No full CTest/PASS/FAIL run, protocol cleanup, TKI/interface-key change or later
thread caller migration is included. Native storage remains a separate authorized
implementation and is not claimed complete by this shared repair.
