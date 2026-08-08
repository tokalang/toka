# Semantic Manifest Envelope P1 Execution Plan

**Status:** Approved P1 direction. This plan freezes the carrier and its
non-authority boundary before any resolver integration.

## Objective

P1 gives CDW1 a strict compiler-owned transport outside ordinary TKI comments:

```text
compiler semantic facts + exact emitted .tki bytes
    -> adjacent .tki.tsm envelope
    -> strict standalone validation
    -> (later, separately gated) resolver comparison with reconstructed CDW1
```

It does **not** make CDW1 a body fulfilment proof. A matching record is only a
recomputed declaration fact, so a bodyless Outcome provider continues to fail
with `E04631`.

## Carrier contract

The compiler names the sidecar by appending `.tsm` to the final interface path:

```text
pkg/module.tki -> pkg/module.tki.tsm
```

The P1 schema is `toka.semantic-manifest-envelope`, version `1`. Its canonical
JSON representation contains exactly:

```text
schema, version, payload_schema,
compiler_version, interface_format, target_triple,
module { crate_id, logical_module_path },
interface_sha256, semantic_dependency_closure_sha256, payload_sha256,
records[]
```

Each admitted record contains exactly:

```text
kind = outcome-transition
criticality = SafetyRequired
trust_class = RecomputedDeclarationFact
cdw1 = lowercase hexadecimal canonical CDW1 bytes
```

The fixed classification is schema-owned. The CDW1 payload itself carries the
stable function subject, and validation decodes that subject to reject records
from another coordinate or duplicate subjects. Records are sorted by
their raw canonical CDW1 bytes. `payload_sha256` commits to a length-framed
canonical sequence of those bytes, while `interface_sha256` commits to the
exact raw `.tki` bytes.

`semantic_dependency_closure_sha256` is present from the first schema version
because it is an envelope identity requirement. P1.0 accepts it only as a
strict, supplied 32-byte SHA-256 digest and requires the loader's expected
digest to match; it does not invent a placeholder or claim that an import list
is a semantic closure. Compiler emission waits for a later slice that can
construct the resolver-owned closure value correctly.

## P1 slices

### P1.0 — codec and artifact boundary

Implement a standalone `SemanticManifestEnvelope` writer and strict loader.
It validates all framing, field sets, compiler/interface identity, exact TKI
digest, CDW1 canonicality, coordinate agreement, classification, ordering,
duplicate subjects, and payload digest. Writes publish atomically.

The codec takes its raw CDW1 values from compiler semantic data, never from
`@tki v2` comments. It has no `ModuleResolver` call path and no effect on
semantic acceptance.

### P1.1 — compiler emission and artifact qualification

Once the producer has a real resolver-owned semantic dependency-closure digest,
write the sidecar after the corresponding `.tki` has been closed. Qualify
source and retained-body source-less builds for byte-identical records and
verify sidecar-to-TKI binding and tamper rejection.

### P1.2 — resolver activation decision

Before consuming a record, decide and document which imports are in the
activated profile and how legacy/no-sidecar interfaces behave. The importer
must obtain the sidecar through resolver-owned identity, strict-decode exactly
one required record, reconstruct CDW1 from declarations, compare atomically,
and retain no partial fact on failure.

No default importer policy is chosen by this plan. In particular, a sidecar
must not become a silent compatibility break for ordinary retained-body Level-A
interfaces.

## Explicit exclusions

- no source syntax, attributes, TKI comments, or metadata authority;
- no generic functions, methods, unbound coordinates, or multiple CDW kinds;
- no object digest, retained object marker, provenance, or `.tke` reuse;
- no provider fulfilment, caller authority, cleanup fact, or CodeGen decision;
- no bodyless Outcome acceptance, and no change to `E04631`.

## Required P1.0 adversarial checks

- malformed JSON, unknown/missing/duplicate fields, unsupported schema, and
  wrong compiler, interface, target, or module identity;
- a sidecar copied beside different exact TKI bytes;
- altered, non-canonical, wrong-coordinate, reordered, or duplicate CDW1;
- altered payload or closure digest; and
- valid write/load round trip with no importer or Sema decision path.

P1.1 and P1.2 add their own qualification gates; passing P1.0 alone is not
payload activation.
