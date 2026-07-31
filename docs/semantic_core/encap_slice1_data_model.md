# `@encap` Epoch Slice 1 Data Model

**Status:** Complete. This is an internal, audit-only implementation slice.
No proposed epoch access, Copy/Dup, drop, parser, TKI, or cache decision is
active.

## Delivered facts

`Sema` now owns separate maps keyed by a canonical nominal-definition identity
plus concrete type spelling:

- `PolicyMap`;
- `ResourceContractMap`;
- `DropPlanMap`;
- `PartialMovePlanMap`;
- `CopyProofMap`;
- `CopyWitnessMap`; and
- `DupProviderMap`.

The current values intentionally record only Slice 1 observations: no resource
contract, unknown Copy proof, no Copy witness, no Dup provider, and legacy
structural/custom drop provenance where it is already known. No legacy Sema
query consults a new map.

An `ImplDefId` is recorded from its lexical resolver coordinate and its
declaration ordinal. Generic-instance bookkeeping is keyed by that definition
identity and concrete type. Empty generic marker impls are therefore stable
without the old “first method exists” proxy. Non-empty generic impls preserve
their legacy registration path until their later rewrite.

## Marker and Dup preparation

`@encap` and `@Copy` are explicitly rejected as dynamic trait objects through
the existing object-safety path. They have no dynamic method dictionary in the
proposed epoch.

`@Dup` providers are classified audit-only as a candidate or invalid candidate.
The strict candidate signature is one public `dup(self) -> Self` method with a
non-consuming, non-exclusive receiver and no other methods. Slice 4 will turn
that classification into the active trait/coherence rule.

## Evidence

`python3 tools/scripts/test_encap_slice1_audit.py` verifies:

1. the audit JSON schema and all fact-table populations;
2. generic empty marker instantiation bookkeeping;
3. valid and invalid `@Dup` candidate classification; and
4. rejection of `dyn @encap` and `dyn @Copy` with `E0617`.

The compiler exposes the audit-only snapshot through:

```text
tokac --encap-slice1-facts=json <source>
```

This output is not TKI and cannot authorize source-less replay. TKI v2 remains
Slice 5 work.
