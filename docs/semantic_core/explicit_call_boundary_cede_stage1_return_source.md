# Explicit `cede` Stage 1: return/source lowering

**Status:** first implementation slice; pending acceptance.

This bounded slice fixes direct construction of a `dyn fn` at a return
boundary. A concrete closure environment must be materialized into the same
refcounted three-pointer carrier used by local bindings and call arguments;
aggregate bit reinterpretation is forbidden.

The gate compares a direct source-less constructed return, a constructed
return carrying an owned resource, and the previously working local binding
plus `return cede callback` form. Returned environments must remain callable,
support ordinary shared binding copies, and destroy captures exactly once.

This slice does not yet activate the complete return-source matrix, remove
`-> cede T`, migrate return signatures, or change Parser/TKI syntax. The
compiler-interface key remains `0.9.9-17` from the dynamic-function lifecycle
slice.
