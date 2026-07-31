# `@Encap` Epoch Slice 0 Redline Results

**Status:** Completed audit evidence. This document records expected results
for the proposed clean-break epoch. It does not enable any proposed parser,
Sema, TKI, cache, Copy/Dup, or lowering rule.

**Authority:** [the hybrid-policy RFC](encap_hybrid_policy_rfc.md) defines the
target semantics. The baseline remains
[encap_slice0_baseline.md](encap_slice0_baseline.md).

## 1. Reproducible runners

- `python3 tools/scripts/test_encap_slice0_audit.py` validates resolver
  coordinates through direct, workspace, relocation, symlink, package-alias,
  source-less TKI, cached-TKI, missing-node, and forged-`source_path` cases.
- `python3 tools/scripts/test_encap_slice0_redlines.py` evaluates the
  audit-only RFC reference models and inventories legacy grants.
- `tools/scripts/test_semantic_replay.sh` preserves the legacy source/TKI
  semantic replay suite.

The redline runner is intentionally independent of the current access and
lifecycle implementation. A later semantic slice must replace its reference
models with implementation tests before activation.

## 2. Access and identity matrix

The logical-coordinate model implements RFC Section 8.2 exactly:

| Case | Shadow result |
| --- | --- |
| Owner module | allow |
| `pub` with known requester | allow |
| `pub(crate)` in the same `CrateId` | allow |
| `pub(crate)` in another `CrateId` | deny |
| `pub(path)` at its target or descendant segment | allow |
| sibling, substring, or cross-crate path | deny |
| unknown owner or requester identity | deny |

The audit keeps three intentional legacy/shadow disagreements visible:
legacy unconditional `pub(crate)`, physical suffix path matching, and missing
module identity would each allow a case that the proposed model denies. They
are migration deltas, not regressions in the active compiler.

## 3. Source, TKI, and cache parity

For a module at the logical path `lib`, the audit compares these forms under
one supplied workspace node:

1. source-backed import;
2. source-less sibling `.tki` import;
3. `TOKA_BUILD_DIR` cached interface import; and
4. source-less `.tki` with a forged absolute `source_path` metadata value.

Every form yields the same `Known(workspace-replay-v1, lib)` shadow coordinate.
The coordinate is derived from the resolver-selected import candidate, never
from the interface's `source_path` metadata. Missing workspace and package
node input yields `Unknown` rather than a path-derived identity.

## 4. Lifecycle and resource redlines

The reference model fixes the proposed custom-drop normal-path sequence:

```text
hook -> declaration-order live structural fields
```

It also demonstrates that structural roots may clear one exact partial-move
slot, while every custom-drop root rejects partial `cede`. Raw pointers and
release-call spelling create no resource fact; only a validated FFI record can
produce a Borrowed or Owned resource contract. The existing legacy ownership
tests remain the baseline implementation coverage until Slice 3.

## 5. Copy and Dup redlines

The reference model has deterministic expected results for:

- all-Copy aggregate success;
- first canonical non-Copy blocker path;
- fail-closed opaque/Unknown dependency;
- direct by-value recursion as a layout error; and
- generic `T: @Copy` success with `T: @Dup` and an unbounded parameter left
  unresolved; and
- zero, intrinsic, user, and overlapping Dup-provider sets.

This is the Slice 0 expected-result model for the later SCC/fixed-point
implementation. It makes no current `clone` or `@Encap` behaviour change.

## 6. Legacy grant inventory

The audited `lib/**/*.tk` corpus has 46 parenthesized policy grants with four
targets:

| Target | Current declarations | RFC migration status |
| --- | ---: | --- |
| `build` | 17 | resolves to `lib/build.tk` |
| `build/internal/codec` | 22 | resolves to `lib/build/internal/codec.tk` |
| `core/str` | 1 | resolves to `lib/core/str.tk` |
| `std` | 6 | resolves to the existing logical `std` module tree |

Nine files retain wildcard policy entries and are therefore also Slice 6
migration inputs: `lib/core/comptime.tk`, `lib/core/task.tk`,
`lib/std/async.tk`, `lib/std/hashmap.tk`, `lib/std/time.tk`,
`lib/std/vec.tk`, `lib/stdx/log/json.tk`, `lib/stdx/log/log.tk`, and
`lib/toolchain/llvm.tk`.

The inventory is complete for the shipped library and build-tool source tree.
Its `std` prefix and wildcard entries are deliberate clean-break migration
work, not an authorization fallback in the proposed model.

## 7. Slice 0 conclusion

All five evidence workstreams now have deterministic, reproducible output:
access/identity, source/TKI/cache parity, lifecycle/resource, Copy/Dup, and
grant inventory. The outputs establish the migration inputs and expected
results; they do not authorize semantic activation. Activation still requires
a later review that confirms the implementation slices preserve these results.
