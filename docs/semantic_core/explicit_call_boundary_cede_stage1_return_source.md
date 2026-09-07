# Explicit `cede` Stage 1: return/source behavior

**Status:** Accepted — return/source behavior slice.

**Acceptance date:** 2026-09-07.

The final reviewer accepted this bounded behavior slice after closing the
reference-rebinding P0 and independently verifying branch joins, zero-iteration
loop paths, and failed-call rollback. Freeze the reviewed implementation,
migrations, and regression tests together with this acceptance record.

This acceptance does **not** complete all of Stage 1, the full explicit-cede
RFC, or release qualification. Subsequent work must be a separately scoped
slice, with its remaining scope agreed before implementation. Existing freeze
refs and RC13 are not advanced by this acceptance.

This bounded slice fixes direct construction of a `dyn fn` at a return
boundary. A concrete closure environment must be materialized into the same
refcounted three-pointer carrier used by local bindings and call arguments;
aggregate bit reinterpretation is forbidden.

The gate compares a direct source-less constructed return, a constructed
return carrying an owned resource, and the previously working local binding
plus `return cede callback` form. Returned environments must remain callable,
support ordinary shared binding copies, and destroy captures exactly once.

This accepted behavior slice implements the return-source matrix: result
signatures use `-> T`, while source transitions are selected from named Copy,
named NonCopy, source-less temporary, unique/shared/raw identity, dependency,
and cede-obligation facts. Source migration must preserve evaluation order and
exact-once cleanup; a complex expression may be bound first, then returned in
the form required by its resulting source category.

Parser/TKI protocol cleanup and the interface-key change are still deferred.
The compiler-interface key therefore remains `0.9.9-17` during this behavior
slice.

## Closing source-provenance checks

`Span::to_str` derives an offset from its supplied source, rejects an invalid
start or a length exceeding `source.len() - offset`, and returns
`source.substr(offset, length)`. It no longer overwrites the layout of a
`str` with `memcpy`. The bounds fixture checks pointer equality (zero-copy),
empty input, invalid starts, oversized lengths, and maximum-sized integers.

For factory-created records with borrowed fields, the binding retains actual
origins mapped through the selected call and its checked arguments. The
enclosing function's return dependency remains an allowed ceiling, never an
origin witness. The Token/Lexer fixture exercises sibling-state updates both
before and after constructing a view, then transfers that view through the
factory result. Unknown origins are not inferred from the record's type.

An existing complete, named direct-value structural plan is fed into the
original return lifetime checker. The dependency collectors traverse `cede`
and `unsafe` in active Stage 1, retaining ordinary and per-field dependency
checks. Known dependencies are preserved on owning/callable transfers;
transferring ownership does not additionally invent a borrow of the old
binding. This is not a change to global `checkExpr` type classification.

The return-matrix gate includes normal/shadow return-code and stderr parity,
negative artifact checks, static-literal provenance, explicit raw-view
projections, wrong factory dependencies, local escape, and buffer exact-once
checks. This acceptance does not activate receiver syntax or protocol cleanup.

### Descriptor storage versus view contents

A view's static character origin does not prove that the view descriptor's
storage is static. The actual-origin query records address-taken storage
separately from value/content origins. Reference construction and resolved
reference-return calls preserve the addressed place rather than following
that place's initializer to its character contents. The original lifetime
collector retains these storage dependencies and reports E0455 for local
escape; no second lifetime-checking block is added.

For a local reference binding, storage lookup follows its checked reference
initializer to the referenced storage, with cycle rejection. It does not
follow an ordinary view initializer to its contents. Return evidence keeps
that prepared target, so a legal projection through a reference does not
mistakenly depend on the intermediate local alias slot.

A selected consuming formal instead transfers reference values carried by its
input (such as `Option<&T>::unwrap`). Their checked dependency paths are still
referent storage, not character-content witnesses: unwrapping `Option<&str>`
cannot make a local `&view` escape legally.

Regression coverage distinguishes bare static-view returns (including a
payload read through a reference alias) from escaping `&view`, `&holder`,
reference-alias and call-forwarded descriptor addresses, and descriptors
borrowed into returned records. Parameter-storage borrows remain valid with
their declared dependency. Rejected descriptor addresses must not acquire
`static_storage_origins` evidence or produce object/LLVM IR artifacts.

### Reference target freshness

Local reference bindings retain a snapshot of their checked target storage.
A validated whole-reference rebind replaces both that target and its lifetime
dependency set; it does not accumulate the initial target as an alternative.
Unknown writes record an explicitly unknown target and cannot fall back to the
declaration initializer. Subsequent references capture the current target.

These facts and their dependency sets participate in analysis snapshots,
rollback, and control-flow joins. Joins retain possible targets from reachable
paths, with unknown state absorbing known alternatives. The ordinary return
lifetime checker remains responsible for E0454/E0455; no duplicate checker or
PAL relaxation is introduced. Tests cover parameter-to-local rejection,
parameter-to-parameter exact mapping, unknown writes, aliasing, and branches.

### Working-tree verification (2026-09-07)

- Complete Debug build, including `toka`, `tokafmt`, and `tokalsp`: passed.
- `tokafmt` version and actual formatting smoke checks: passed.
- Complete CTest with `-j2`: 42/42 (140.94 seconds).
- Complete PASS suite, without exclusions: 451/451 (215.69 seconds).
- Complete FAIL snapshot suite, without exclusions or blessing: 473/473.
- Expanded return-matrix gate: passed, including the previous three audit
  regressions, unknown/overwritten origins, Token factory lifetime negatives,
  Span bounds/zero-copy, normal/shadow parity, and buffer exact-once checks.
- Descriptor-storage audit counterexample: E0455 in normal/shadow modes,
  identical return code/stderr, and no object or LLVM IR output.
- Reference-rebinding audit counterexample: E0455 and no object/LLVM IR output;
  current-parameter, unknown-target, alias, and branch regressions passed.
- `git diff --check`: passed.

The final independent review reran the complete incremental build, CTest
42/42, and `git diff --check`, plus the branch/loop/rollback probes noted above.
It accepted the implementation's reported PASS 451/451 and FAIL 473/473 results
without rerunning those two full suites in that review. This record accepts
only the return/source behavior slice; it is not full-RFC/release qualification.
