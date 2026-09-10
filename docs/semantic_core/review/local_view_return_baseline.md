# Local owning-string view return — open baseline defect

Status: independently recorded; **not fixed and not accepted as safe**.
This is outside B1. No return-rule change is authorized by recording it.

## Reproducer and result

The exact input is [b1_view_return_probe.tk](b1_view_return_probe.tk):

```toka
fn bad() -> str {
    auto owner = string::from("local")
    return owner.as_view()
}
```

An independently built Debug compiler at B1's baseline
`7240711202a80481ddbc3a123da0013f94fad1a0` and the B1 compiler at
`4142eebdc30e0952e97a48908703502d025e7179` both accept this identical input.
Normal/non-call-shadow return codes and diagnostics agree within each version.
Both emit this relevant sequence:

```llvm
%2 = call %str @string_as_view(ptr %owner)
; owner live-bit guard
call void @Encap_string_drop(ptr %owner)
; drop.done
ret %str %2
```

The complete `bad` function IR is byte-identical between these builds:
SHA-256 `b232bb4576a93ce522e16fc1a938ae85cb2a76cedcb612cef11bccc457d37333`.
Input SHA-256:
`12f994250411d268c84aa11d95ee4693300233182d6edbf51b3077c898aba5a3`.

This demonstrates that the local owner's cleanup precedes returning its view;
the caller can receive a view into retired storage. The reproducer's `main`
does not invoke `bad`; no undefined-behavior runtime outcome is used as proof.
This is distinct from borrowing the local descriptor with `return &view`,
which the B1 negative matrix rejects.

This is not inferred solely from the word `drop`: `lib/core/string.tk`'s
`string::from` allocates and copies the nonempty literal (line 157 onward), and
the string drop implementation frees that buffer with `free [0]` (line 50
onward). These library implementations are unchanged between the two revisions.

## Attribution and follow-up

- Present at the exact B1 starting revision; not introduced by B1.
- Keep as a separate release-safety debt. Passing B1 does not close it.
- A future repair should validate actual owning-storage lifetime, with positive
  parameter/static-owner cases and negative local-owner cases. This note does
  not authorize reopening the accepted return implementation now.

Reproduction is included in `tools/scripts/compare_binding_b1_integration.py`.
Local full-run evidence directory: `/private/tmp/toka-b1-final-serial`;
see `metadata.json` and each version's `view.ll` and normal/shadow logs.
