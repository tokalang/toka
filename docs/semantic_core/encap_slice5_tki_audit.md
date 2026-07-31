# `@Encap` Slice 5 TKI v2 audit

Slice 5 is selected with `--encap-epoch=v5`; it includes the policy,
lifecycle, and Copy/Dup rules from the preceding epochs.

## TKI v2 boundary

- The interface format is `2` and the compiler-interface cache key is
  `0.9.9-02`.  A format-1 interface is incompatible and falls back to source
  only when source is available.
- Every exported interface carries the identity schema version, logical module
  path, and a resolver-binding digest.  Under the v5 gate the resolver derives
  the expected identity from its package/workspace/toolchain graph and rejects
  missing or mismatched metadata.  An unbound coordinate is explicit and can
  never authorize an `@Encap` grant.
- Semantic records are emitted as `@tki v2` comments after Sema has completed:
  complete field graph entries, normalized policy grants, Copy proof and
  witness, generic `CopyRecipe`, intrinsic/user Dup provider, and custom-drop
  ABI symbol.  The ordinary interface declarations remain the replay source
  for type checking, where the exported recipe is recomputed and validated
  against the same field graph and generic bounds.
- v5 emits no `structural_drop` marker and does not replay it from an
  interface.  Structural cleanup is recomputed from the transported private
  field graph; only a custom drop ABI is transported explicitly.
- v5 excludes deleted declarations and the removed `@Clone` / `@Drop` facets
  from emitted interfaces.

## Regression evidence

Run:

```sh
cmake --build build --parallel 2
python3 tools/scripts/test_encap_slice5_tki_audit.py
```

The audit exports governed concrete and generic Copy capsules, concrete and
generic user `@Dup` capsules, and a custom-drop capsule. It confirms their
v2 records, hides the source, then replays bounded-generic Copy, source-less
rejection of a non-Copy generic argument, and one explicit `[dup ...]`
provider call for each Dup form through the interface. It then proves both
format-1 and forged/missing resolver-identity metadata are rejected.
