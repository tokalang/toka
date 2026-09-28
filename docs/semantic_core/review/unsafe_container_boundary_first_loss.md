# Vec write boundary: first lost borrowed source

**Status:** decision required before an implementation change. This branch is
based on `a1fdc191b4be453654f458d465dd9f3fb1735696`. It changes no
production Vec, `raw_take`, PAL, generic bound, ABI, or release asset. The
four archived counterexamples are compiled only; none is executed.

## Controlled isolation

`test_unsafe_container_boundary_probe.py` copies the unchanged production
`lib/std/vec.tk` into a temporary library and applies only the archived
`pop_remove.patch`. That replacement bypasses the eager, unused `pop`
instantiation error so the caller's write/return chain can be observed. The
patch is a **rejected diagnostic device**, not an implementation candidate.
Its SHA-256 is `f6ac5e8e0341bb2527323072e2077c23b9fd840aa938e28279a41d5916d66879`.

The exact tracing instrumentation is saved as
`tests/semantics/unsafe_container_boundary/trace_instrumentation.patch`; it
was removed from compiler sources after collecting the trace. Its SHA-256 is
`d051e691409baaab98031f86e616c86abce781ad02910001347c8388d09d668e`.
The trace compiler SHA-256 was
`1092cdefb2b7496f5fcf0b9a0fcb8280250877c7d89ba1e594256610563368e9`.
The same source rebuilt without tracing has SHA-256
`ac16e7e12bb9e721dfc654b6ea47d5c335a41dfbbad8706c0610003b691c6410`;
the saved probe result was rerun with this clean compiler.
Binding IDs in the trace belong to that compile and must be resolved afresh
on replay; names are displayed only for readability.

## First loss in the minimal whole-Vec escape

| Point | Recorded fact |
| --- | --- |
| `owner.as_str()` | Receiver `owner` has binding ID **1468**. Result field dependency is `buf <- owner.buf`. |
| Argument to `values#.push(...)` | Actual `str` still has `buf <- owner.buf`, resolved to source binding ID **1468**. The generic `push` formal (ID 1381) was checked without any particular call-site owner; it cannot itself establish this instance's source. |
| After `push` | Receiver `values` has binding ID **1469**, but both its whole-value and field dependency sets are empty. The argument's field fact remains only in scratch state. |
| `return cede values` | The return checker does select the borrow-like path (`tracked=1`), but binding **1469** and last-result dependencies are empty, so it admits the escape. |

The complete four-point record is in `whole_vec_first_loss.trace`. The three
other archived escape cases show the same `push` receiver gap; their trace
files are alongside it. `pop`, `remove`, and owned `next` then return an
`Option<str>` with no retained source. The first missing persistent fact is
the transfer from the actual argument's known dependency to the **post-call
receiver state**, before the return check. The observed return is a downstream
effect, not the first loss.

The call audit also marks the temporary actual as borrowed with no complete
`dependency_roots`. This is not proof of independence: the separately recorded
member dependency still names binding 1468. An unknown or incomplete call
fact must not be converted into an empty receiver dependency set.

## Why the present contract cannot express the write

`Vec<T>::push(self#, cede val: T)` has no receiver postcondition. The current
`effects:` parser accepts only `return` or the function's **named return
binding** as a target. `ReturnContractSyntax::deriveLegacyDependencies`
therefore yields only result `LifeDependencies` or result
`MemberDependencies`. The method-call checker maps those facts to a returned
value; it has no checked path for adding the actual argument's dependencies
to the mutable receiver. Adding `-> ... <- val` to this unit-returning method
cannot describe the receiver write. Inferring one from the spelling `Vec`, a
field name, or the mere existence of a mutable receiver would either miss
other containers or create an undeclared interface rule.

**Proposed smallest new contract surface for review:** allow a receiver
poststate target in `effects:` such as:

```toka
pub fn push(self#, cede val: T)
effects:
    self <- val
{ ... }
```

This means the receiver's external dependencies **after** the call must cover
its prior dependencies and the actual `val` dependencies. It is a monotonic
upper bound: `remove` or `clear` need not precisely subtract sources. The
compiler must resolve actual sources by binding identity, including member
and enum payload dependencies. An unknown actual remains unknown and fails
closed. The callee's writes must be checked against the declared upper bound,
and caller application must be atomic with successful call validation. The
effect must survive `.tki` export/import and semantic cache identity; a body
hidden behind an interface cannot silently lose it.

Extraction needs the existing result effect form, conservatively mapping a
stored source out of the receiver, for example `remove`, `pop`, owned `next`,
and any consuming Vec-to-iterator route with `result <- self`. Whole-Vec
`cede` return can then use its binding's dependency set. The existing
`Vec<&T>::appended` return route (`result <- self | val`) remains separate and
must continue working. `borrow`/`borrow_mut` retain their storage dependency
`&item <- self`; an element borrow cannot outlive the Vec allocation merely
because `T` itself has no external dependency.

Implementation would minimally require a distinct receiver poststate route
in the parser/AST, verified call-site mapping into the receiver binding,
callee checking, interface serialization/versioning, and the extraction
annotations. It must not modify or disable the production `raw_take` check.
No syntax or version change is made by this diagnostic package.

## Additional hidden-borrow controls

An aggregate spelling is not an independence proof. In the isolated library,
`hidden_shape_escape.tk` (`Holder(&value: i32)`) and
`hidden_enum_escape.tk` (`Payload::View(str)`) both reach IR; neither was
executed. The shape argument leaves an `owner` whole-value dependency in
scratch state, but the Vec receiver remains empty. The enum case loses the
visible `as_str` member fact while wrapping it as a variant; its return is
also classified `tracked=0`. Thus an implementation must additionally carry
actual enum payload sources into the receiver effect and make the return
lifetime gate inspect populated dependencies for a nominal enum-containing
Vec. The current trace bounds the enum subgap to variant construction through
`push` argument preparation; it does not yet name a single internal statement.
The direct `Holder(view: str)` spelling currently meets an unrelated
`Option::unwrap` E04658 during generic instantiation, so it is not used as a
lifecycle rejection oracle.

## Incremental evidence and acceptance boundary

Run `python3 tools/scripts/test_unsafe_container_boundary_probe.py
--build-dir <build> --mode diagnose`. The saved output is `probe-diagnose.txt`.
Normal and non-call shadow agree for all six escaping sources. With the
production library, the four archived cases still reject E04662 and emit no
object or IR. With the isolated replacement, all six escape cases are
admitted through IR: **known unsafe gap**, never executed.

The same probe runs `local_view_alive`, `owned_string_return`,
`owned_token_exact_drop`, and the existing `Vec<&T>::appended` growth case;
all passed. The wrapper around an owned string dropped exactly once after
returning its Vec. The existing reference escape rejected E0455, and a
mutation under a live element borrow rejected E0441, both without an object.
The repository gates `test_vec_reference_domains.py`,
`test_binding_value_dependencies.py`, and `test_stage1_vec_pop.py` passed
(the latter 20/20).

`--mode closed` is the deliberately red acceptance oracle. It requires all
six escapes to reach a local-owner lifetime diagnostic, agree in normal and
shadow modes, and emit neither object nor IR. At this checkpoint it fails at
`whole_vec_escape`, which is still admitted. A future implementation must
also keep the four runtime controls and the two existing rejections green.

**Decision requested:** approve or revise the receiver poststate dependency
contract and its source-hidden representation before implementation. This
package establishes the first loss and the additional enum limitation; it
does not claim that nested Vec or borrowed-element Vec is fixed.
