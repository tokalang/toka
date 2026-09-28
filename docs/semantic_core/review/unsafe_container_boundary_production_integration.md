# Production nested Vec integration candidate

**Status:** isolated candidate on `design/unsafe-container-boundary`; not yet
accepted or merged. This builds on the binding-ID receiver poststate package
`3ef17ae8251025d2f2877b77848f2146e225255f`. It changes no `0.10.0`
candidate, tag, release asset, or native ABI.

## Responsibility boundary

The review of `self.external` found no loss of a newly created storage borrow.
`Option::unwrap` of an `&i32` borrowed from a local Vec still rejects E0455
when returned after that Vec dies. Conversely, a `str` extracted from Vec
while its external string owner remains alive can outlive the Vec. The
external-source route is joined conservatively; a fresh element borrow still
has its separate storage source. No unknown source is turned into an empty
set.

Production `Vec::pop` now delegates tail retirement to its existing `remove`
operation, with `return <- self.external`. That operation moves the complete
element value, shifts the remaining initialized slots, and reduces `len`;
for the tail the shift loop is empty. The archived `pop_remove.patch` remains
only historical diagnostic evidence. The compiler's production `raw_take`
check is unchanged. No raw allocation is accepted because a type is named
Vec, and no automatic proof of every unsafe container operation is added.

Vec's `buf`, `len`, and `cap` no longer have public `@Encap` grants. Outside
the defining module, direct field mutation and direct shape construction
reject E0418. `from_raw`, `unsafe_set_len`, raw accessors and owner-forgetting
operations are declared `unsafe fn`; a safe call rejects E0623, while an
explicit `unsafe` call is allowed. `from_raw` requires a compatible allocation
of `cap` slots, `len <= cap`, a fully initialized prefix, no conflicting live
borrow, and sole cleanup responsibility. Shrinking or growing via
`unsafe_set_len` requires the caller to account for retired or newly
initialized slots. These are unsafe caller obligations, not compiler-inferred
facts from raw pointers. Unsafe trait methods are rejected E0624 in this
narrow first version so a safe trait dispatch cannot erase the call boundary.

The modifier is preserved in source-hidden TKI; interface format 5 and
compiler interface version `0.9.9-25` reject older records. Native value and
call ABI are unchanged. Source-hidden tests verify that removing `unsafe`
without updating the interface replay hash fails and that a safe caller still
cannot invoke the imported declaration.

The existing byte-buffer contract has exact, compiler-owned SHA-256 seals
for `std/vec`, `std/bytes` and `std/net`. These were refreshed only after the
reviewed source changes. The initial mismatch correctly invalidated byte
buffer independence, which also invalidated async TaskResult summaries;
`g13_net_buffer_abi_test` passes after refreshing the three source seals.
The DataFile native lease contract likewise has a Sema and CodeGen source
digest; both copies were refreshed to the same reviewed `std/data_file`
content after its callers entered unsafe. These are exact trusted
implementation checks, not new type-name grants.

Standard-library clients of formerly public Vec fields were migrated:
`std/task` initializes with `Vec::new`; `std/bytes` borrows through Vec and
uses the unsafe raw constructor for its zero-copy thaw; BTreeMap uses checked
element duplication or explicit unsafe slot access; HashMap uses explicit
unsafe raw-storage and initialized-slot borrow methods; DataFile, net and
bufio enter unsafe for their raw/length operations. HashMap keeps its own
occupied-slot metadata and retirement responsibility.

## Verification boundary

`test_unsafe_container_boundary_probe.py --mode production` uses the actual
standard library, with no overlay or archived pop patch. It checks all 17
borrow escape cases in normal and non-call shadow mode, requiring E0455 and
no object or IR. One case proves that an `unsafe` block around `push` does not
erase the ordinary string-view owner source. It runs ten runtime controls,
including a nonempty nested `Vec<Item(children: Vec<Leaf>)>` case covering construction,
both levels' growth, insertion, nested borrow, replacement, removal, pop,
consuming iteration, whole-container lifetime and exact-once drop accounting.
Unsafe raw constructor, storage and length calls reject in safe code; direct
field access and construction reject even inside an unsafe block; the explicit
unsafe empty constructor and public raw signature control run.
The existing `Vec<&T>::appended` path and active-borrow conflict rejection
remain in the probe. `test_stage1_vec_pop.py` passes all 20 checks including
unique/shared handles and move-only cleanup. Source-hidden poststate and
unsafe-call TKI tests pass.

The older `test_binding_value_dependencies.py` still checks that standalone
`raw_take` rejects a drop-only shape without value dependency proof; its
fixture now invokes `raw_take` directly instead of relying on Vec::pop to
instantiate it. The byte-buffer and TKI cache/unsafe/excluded-syntax gates
also pass. The first broad pass-suite run found 13 affected BTreeMap/HashMap
clients after field closure; all 13 passed a targeted rerun after migration.
The complete pass suite then passed 459/459. The first conformance run passed
324/325; its single DataFile closure failure was the old exact source digest.
After both Sema and CodeGen digests were refreshed and the DataFile concurrency
case passed alone, the full conformance suite passed 325/325. Initial failures
and final reruns are kept as separate logs under
`evidence/unsafe-container-production-3ef17ae8/` outside the repository.

## Remaining limits

External dependency sets may retain an old source after `remove` or `clear`.
The compiler does not prove raw allocation ownership, initialized extents,
alias freedom, or cleanup for an unsafe constructor; an unsafe caller must
uphold those preconditions. This package does not add generic constraints,
arbitrary parameter poststates, PAL redesign, or a general proof for other
unsafe containers such as HashMap.
