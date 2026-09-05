# Dynamic-function binding lifecycle slice

**Status:** implementation slice; pending acceptance.

This slice closes an existing runtime defect in inferred `dyn fn` binding
copies. The public erased callable carrier remains exactly three pointers:
environment, invoke function, and drop function. A private control header now
precedes heap-owned environments and carries an atomic owner count.

Sema classifies each dynamic-function initialization as `Retain` or
`Transfer`. A bare named binding copy retains the environment; explicit
`cede` and source-less construction transfer the existing owner. CodeGen only
executes that disposition. It does not reconstruct copy intent from source
syntax.

Scope cleanup releases one owner. Only the final release runs the environment
drop cascade and frees the allocation. The regression gate covers multiple
binding copies with a captured resource and a destructive binding transfer;
both must destroy the captured resource exactly once.

This slice does not change Parser syntax, the three-pointer `dyn fn` carrier,
TKI syntax, caller-side `cede` diagnostics, or receiver spelling. It does
change the cross-object environment-allocation convention, so the compiler
interface key advances from `0.9.9-16` to `0.9.9-17`; older TKI/object pairs
must not be silently mixed with the refcounted environment contract.
