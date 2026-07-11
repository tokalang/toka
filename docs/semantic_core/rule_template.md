# Semantic Rule Template

Use this template when adding or auditing a semantic-core rule.

```text
Rule ID:
Title:
Status:
Area:

Source form:
Operation class:
Decision:
Rationale:

Compiler inputs:
Required semantic facts:
Interface replay requirements:
Cache invalidation requirements:

Primary diagnostics:
Implementation areas:

Positive tests:
Negative tests:
Warning tests:
Missing coverage:

Notes:
```

## Field Guide

`Rule ID` should be stable and independent of diagnostic codes. A diagnostic
may move, split, or be improved without changing the language contract.

Recommended prefixes:

- `PAL-*` for Path-Anchored Ledger rules.
- `OWN-*` for ownership, move, cede, drop, clone, and resource-copy rules.
- `EFF-*` for escaping dependencies and `effects:` routing.
- `ASYNC-*` for async/task/thread execution-boundary rules.
- `TKI-*` for interface replay and semantic cache rules.
- `UNSAFE-*` for public unsafe/raw API redlines.

`Status` should be one of:

- `Core guarantee`
- `Conservative rejection`
- `Post-1.0 precision`
- `Syntax exclusion`

`Operation class` should use the same vocabulary as the compiler when possible:

- `PayloadWrite`
- `SharedPayloadBorrow`
- `ExclusivePayloadBorrow`
- `HandleViewBorrow`
- `ExclusiveMutation`
- `Invalidation`
- `OwnershipTransfer`
- `CedeObligation`
- `EscapingDependency`
- `ExecutionBoundaryCapture`
- `InterfaceReplay`

`Interface replay requirements` should say what must remain visible when a
callee or imported module is consumed from `.tki` instead of source.

`Missing coverage` should be explicit. Use `None known` only when both positive
and negative coverage have been checked.
