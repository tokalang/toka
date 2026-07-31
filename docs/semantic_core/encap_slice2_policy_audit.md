# `@Encap` Epoch Slice 2 Policy Audit

**Status:** implementation-backed, gated v6 evidence.

## Implemented checks

- An `@Encap` policy has exact field grants and at most one `drop` hook; a
  wildcard grant or ordinary method is rejected.
- A policy belongs to the nominal type's defining module, is unique by that
  nominal/module identity, and has no bounded/conditional generic parameter.
- Every grant is a global exact field grant. `pub(crate)`, `pub(path)`, and
  wildcard forms are parser errors.
- `CanNameField` is used by member projection, named construction, update and
  spread-generated fields, match patterns, and declaration destructuring.
- In v2 `@Encap` is an authority policy rather than the legacy clone/drop
  trait/vtable contract.

## Reproducible evidence

Run after building `tokac`:

```sh
python3 tools/scripts/test_encap_slice2_audit.py
```

The audit covers allowed exact grants; rejected private member,
named-initializer, spread, and destructuring access; rejected legacy scoped
and wildcard forms; conditional and duplicate policies; and a single permitted
drop hook.
