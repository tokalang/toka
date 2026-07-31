# `@encap` Epoch Slice 2 Policy Audit

**Status:** implementation-backed, gated evidence.  This is not the release
language epoch: the supplied toolchain stays on its legacy policy form until
Slice 6 migrates it.

`tokac --encap-epoch=v2` enables the Slice 2 checker for workspace and package
modules. The parser retains the legacy token forms only so trusted toolchain
sources can still load; Sema rejects those forms for every non-trusted policy.
The trusted toolchain's remaining wildcard and clone/drop declarations must be
migrated as one later clean-break change.

## Implemented checks

- An `@encap` policy has exact field grants and at most one `drop` hook; a
  wildcard grant or ordinary method is rejected.
- A policy belongs to the nominal type's defining module, is unique by that
  nominal/module identity, and has no bounded/conditional generic parameter.
- `pub(crate)` and `pub(path)` use resolver crate and logical-module
  coordinates.  Missing coordinates and cross-crate path grants fail closed.
- `CanNameField` is used by member projection, named construction, update and
  spread-generated fields, match patterns, and declaration destructuring.
- In v2 `@encap` is an authority policy rather than the legacy clone/drop
  trait/vtable contract.

## Reproducible evidence

Run after building `tokac`:

```sh
python3 tools/scripts/test_encap_slice2_audit.py
```

The audit covers allowed global/crate/path grants; rejected private member,
named-initializer, spread, and destructuring access; unknown identity;
wildcards; conditional and duplicate policies; and a single permitted drop
hook.
