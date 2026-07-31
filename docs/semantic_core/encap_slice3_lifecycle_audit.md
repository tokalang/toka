# `@Encap` Epoch Slice 3 Lifecycle Audit

**Status:** gated implementation evidence; not a publishable language epoch.

`tokac --encap-epoch=v3` includes Slice 2 policy checking and activates the
first lifecycle lowering changes:

- resource-containing workspace shapes no longer receive a synthesized
  structural `impl T@Encap`, clone declaration, or named structural drop
  symbol;
- a valid custom hook is private `fn drop(self#) -> void`; it is omitted from
  ordinary semantic method lookup;
- on normal custom-hook return, the compiler continues with its structural
  field tail. Owning pointer fields enter that tail in declaration order; and
- the existing custom-drop rule rejects a direct partial `cede` of a capsule
  field.

Trusted toolchain modules retain their legacy lifecycle rules until the Slice 6
library migration. The v3 gate therefore gives workspace code an auditable
lowering target without silently changing the current shipped library.

Run the targeted evidence after building `tokac`:

```sh
python3 tools/scripts/test_encap_slice3_audit.py
```

The fixture verifies the custom hook symbol, absence of a synthetic structural
symbol, custom-capsule partial-move rejection, hidden hook invocation, and
invalid hook-signature rejection.
