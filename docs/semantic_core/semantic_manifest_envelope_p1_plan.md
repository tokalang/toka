# Semantic Manifest Envelope P1 Execution Plan

**Status:** P1.0/P1.2 implemented. P1.2 is an explicit, default-off compiler
validation profile for the admitted source-less TKI subset; it does not alter
ordinary retained-body import compatibility.

## Objective

P1 gives CDW1 a strict compiler-owned transport outside ordinary TKI comments:

```text
compiler semantic facts + exact emitted .tki bytes
    -> adjacent .tki.tsm envelope
    -> strict standalone validation
    -> (P1.2 profile) atomic comparison with reconstructed CDW1
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

P1.1 defines the closure rather than substituting `content_hash`, a source
path, or an ordinary import list. For each resolver-selected module `M`:

```text
ReplaySurface(M) = metadata-free, audit-comment-free TKI replay export(M)

NodeDigest(M) = SHA-256(
  "toka.semantic-dependency-closure-v1\\0"
  + ModuleCoordinate(M)
  + SHA-256(ReplaySurface(M))
  + sorted[(ModuleCoordinate(D), NodeDigest(D)) for D in imports(M)]
)
```

The root `NodeDigest` is the envelope's
`semantic_dependency_closure_sha256`. `ReplaySurface` deliberately retains
declaration bodies that ordinary TKI replay retains, including the current
Outcome Level-A body. It excludes `@meta` source hash/path fields and ignorable
`@tki` audit comments, so source and source-less replay are compared through
their reconstructed semantic surface rather than their carrier spelling.

Every transitive node must have a resolver-known coordinate and an AST selected
by the resolver. Missing graph nodes, duplicate coordinates, unresolved
imports, and cycles fail closed; P1.1 emits no manifest for that root. Child
P1 CDW1 sidecars are intentionally not inputs to this version because they are
only declaration-recomputed redundancy. A later schema version must add any
validated body-attested child manifest/object obligation explicitly.

After the root `.tki` has been closed, the compiler writes a `.tki.tsm` only
when that root has admitted raw CDW1 records and the closure calculation
succeeds. Qualify source and retained-body source-less builds for
byte-identical records and closure digests, and verify sidecar-to-TKI binding
and tamper rejection. Since P1.1 has no resolver consumer, failure to
construct this optional carrier omits it (with a verbose diagnostic) rather
than changing compilation acceptance; an I/O failure after a qualified carrier
has been selected remains an output failure.

### P1.2 — explicit validation profile

`--validate-semantic-manifests` validates only a resolver-selected source-less
`.tki` module with a known coordinate and one or more declaration-reconstructed
admitted P1 CDW1 records. It is deliberately default-off, so a legacy or
sidecar-free retained-body Level-A interface keeps its existing import result.

For each activated module, the compiler:

1. reconstructs the replay-surface dependency closure from the complete
   resolver graph;
2. reads the exact resolver-selected `.tki` bytes and its adjacent `.tsm`;
3. strictly validates schema, compiler/interface version, target, coordinate,
   exact-interface digest, closure digest, classification, canonical payload,
   and record framing; and
4. compares the complete canonical CDW1 record set with the declarations that
   Sema reconstructed from that `.tki`.

Any missing, unreadable, stale, malformed, non-canonical, wrong-coordinate,
wrong-target, closure-mismatched, or declaration-mismatched sidecar fails the
profile with `E04633`; no partial record becomes visible. The profile runs
after ordinary semantic analysis and before `--check-only` exits. A bodyless
Outcome provider still fails first at the retained-body semantic boundary with
`E04631`; P1.2 neither accepts it nor turns a manifest into fulfilment,
cleanup, caller, or CodeGen authority.

The qualification runner covers a valid sidecar, a missing sidecar under both
default and profile modes, a non-canonical tampered sidecar, a canonical but
declaration-mismatched record, and the unchanged bodyless `E04631` boundary.

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
