# RFC: Semantic Manifest Envelope

**Status:** Proposed envelope and trust model. P1.0/P1.3 implement an
independent compiler-owned sidecar carrier, replay-surface closure, and an
explicit default-off validation profile for a narrow declaration-recomputed
CDW1 prototype, including project-build propagation. P2 implements the narrow
local Outcome-attestation execution plan recorded in
[`semantic_manifest_envelope_p2_plan.md`](semantic_manifest_envelope_p2_plan.md).
It changes no source syntax or TKI grammar.

**Depends on:** resolver-owned module identity, TKI semantic replay, current
Encap/Copy/Dup/drop facts, and the existing object-bound trusted-memory-
evidence experience. The language payload is gated on a stable PlaceState Core
and `OutcomeTransition` IR.

## 1. Purpose

A semantic manifest binds canonical, machine-checkable semantic summaries to
the declarations and, where necessary, the exact implementation object they
describe. Its purpose is to prevent source-less separate compilation from
turning a declaration comment, stale cache entry, forged path, or unpaired body
summary into new language authority.

The first RFC slice proposes only:

- envelope identity and versioning;
- canonicalization and digest requirements;
- trust classes and their allowed consumers;
- exact-object binding for body-derived attestations;
- atomic validation and fail-closed behavior; and
- the relationship to ordinary TKI and the existing `.tke` cache.

The exact set and encoding of semantic payload records remains deferred.

## 2. Non-goals

This envelope is not:

- a proof language or proof-carrying-code system;
- a replacement for parsing and checking TKI declarations;
- a package signature, publisher identity, or supply-chain trust system;
- a stable cross-version machine ABI;
- permission for arbitrary TKI files to assert body-derived facts;
- a verifier for foreign/native objects;
- a reason to trust raw-pointer or unsafe-wrapper invariants; or
- an expansion of the current trusted-memory-evidence `.tke` contract.

## 3. Artifact relationship

Ordinary `.tki` remains a semantic replay interface. Its declarations remain
the source from which declaration-recomputable facts are derived. The initial
carrier is an adjacent compiler-owned sidecar named by appending `.tsm` to the
resolved interface path, for example `net.tki.tsm`. The sidecar has its own
schema and is neither TKI text nor TKI metadata: the lexer never sees it and
an ordinary TKI comment cannot substitute for it.

P1.0 defines the strict writer/validator; P1.1 writes a sidecar only for a
root with admitted CDW1 records and a resolver-complete replay-surface closure.
P1.2 adds `--validate-semantic-manifests`: an explicit profile that validates a
resolver-selected, known-coordinate source-less `.tki` with reconstructed P1
CDW1 records against its adjacent sidecar. In that profile, a missing or
invalid `.tsm` fails closed with `E04633`; outside it, no existing import
decision changes. In particular, neither mode changes Level-A retained-body
replay or the bodyless Outcome `E04631` boundary. A closure that cannot be
constructed still omits emission, while profile validation of an admitted
source-less interface fails rather than inventing a fallback.

P1.3 makes the same profile available as
`toka build --validate-semantic-manifests`. It forwards the resolver identity
context to the project build and executes a no-write semantic check even when
the incremental build plan is clean. The option is invocation-scoped: it adds
no `package.tk` dialect, lockfile claim, or default requirement.

Whichever layout is selected, the manifest is logically distinct from:

- declaration text;
- comments such as current audit-only `@tki` records;
- public semantic-decision evidence;
- dependency-manifest cache status; and
- trusted optimizer memory evidence in `.tke`.

The sidecar's adjacency only identifies a candidate artifact for compiler-owned
resolution; it does not establish trust by itself. No file extension, path,
filename, source path, or metadata field establishes trust by itself.

## 4. Stable identity requirements

### SME-ID-01: Resolver-owned module identity

Every record is rooted in a resolver-proven `ModuleCoordinate`, not an
absolute path, basename, symlink spelling, working-directory-relative path,
TKI cache location, or author-supplied `source_path`.

### SME-ID-02: Stable declaration identity

Every record subject uses a stable declaration identity:

```text
StableDefId = ModuleCoordinate + CanonicalDeclarationIdentity
```

The declaration identity must distinguish declaration kind, nominal owner,
canonical name, generic arity/domain, and overload/signature identity where
needed. It must not depend on source offsets, AST allocation order, diagnostic
locations, or process-global registration order.

The exact byte encoding is deferred, but stability and collision rejection
are acceptance requirements before payload activation.

### SME-ID-03: Stable paths

Any future payload path must be rooted in a stable formal or declaration and
use typed components such as stable field identities and bounded constant
indices. Free-form dotted strings are not semantic identities. Unsupported,
dynamic, dereferenced, or ambiguous paths fail closed until a payload RFC
defines them.

## 5. Logical envelope

The manifest envelope must carry at least:

```text
Envelope = {
  EnvelopeSchema,
  PayloadSchema,
  CompilerBuildIdentity,
  InterfaceFormatVersion,
  TargetTripleAndABIIdentity,
  ModuleCoordinate,
  InterfaceContentDigest,
  SemanticDependencyClosureDigest,
  CanonicalPayloadDigest,
  ObjectBinding?,
  Records
}

Record = {
  Criticality,
  TrustClass,
  StableSubject,
  RecordKind,
  CanonicalPayload
}

ObjectBinding = {
  ExactObjectDigest,
  ObjectRetainedMarker,
  ProducerProvenanceRef
}
```

`PayloadSchema` may remain an explicit `none` before a language payload is
frozen. Trust class belongs to each record so one envelope can carry
declaration-recomputed and object-attested records without promoting the
former or weakening the latter. `ObjectBinding` is mandatory whenever the
envelope contains a compiler-attested body-derived record and is forbidden
from being inferred from file adjacency.

An object digest binds a summary to executable bytes; it does not prove that
the compiler, object, or summary is semantically correct. Likewise, a build-ID
string or retained marker is not producer authentication. `ProducerProvenanceRef`
must resolve through compiler/resolver-owned policy to a non-author-controlled
local build receipt or another verifiable producer attestation. Copying or
inventing its bytes inside a TKI, object, or manifest does not establish that
provenance.

`SemanticDependencyClosureDigest` canonically commits to every safety-relevant
semantic interface/manifest fact used when checking and lowering this object,
including the accepted provider-proof profile and, for object-attested
dependencies, their provenance, payload, and exact-object link obligations. It
is a compiler-owned dependency/Merkle closure, not an author-curated import
list. A caller object checked against an older callee contract is stale even
when its own interface, symbol ABI, and object bytes did not change.

`Criticality` is either `SafetyRequired` or `OptionalOptimization`. A safety
record may depend only on other validated safety records. Optional records may
never be prerequisites for parsing, type checking, authority, PlaceState, PAL,
cleanup, or safe lowering.

The serialized `Criticality` and `TrustClass` fields are claims, not provider
choices. For every `(PayloadSchema, RecordKind)`, the consumer-owned schema
fixes the only permitted criticality and trust class. The importer recomputes
that classification and rejects a mismatch; a provider cannot downgrade a
required fulfilment record to `OptionalOptimization` or relabel its provenance.

## 6. Canonicalization

The manifest must have one deterministic canonical byte representation for
digesting and comparison. Before payload activation, the chosen encoding must
freeze:

- normalized identifier and type encodings;
- deterministic record and collection ordering;
- duplicate-key and duplicate-record rejection;
- integer, boolean, enum, and optional-field encodings;
- treatment of unknown fields and schema versions;
- canonical generic substitutions and constraints; and
- canonical representation of empty and absent payloads.

Pretty printing, source locations, diagnostic text, filesystem paths, map
iteration order, and compiler pointer values are excluded from canonical
identity.

## 7. Trust classes

Every semantic record belongs to exactly one trust class. A consumer may not
silently promote a record to a stronger class.

### 7.1 Recomputed declaration facts

Examples include normalized Encap grants, field graphs, signature H/P and
morphology, structural Copy recipes, structural drop eligibility, and later
the signature-level shape of init/outcome contracts.

The importer parses the declarations, recomputes the fact, and compares it
with the canonical record. The record is redundant validation data, not an
authority assertion. Missing, contradictory, or non-canonical required facts
fail closed.

### 7.2 Accepted-provenance, exact-object-bound compiler facts

Examples may later include callee fulfilment of init/outcome transitions,
unsafe-wrapper shell obligations, custom cleanup paths, and async frame
cleanup.

These facts require either a body rechecked by the consuming compiler or an
attestation produced through an accepted compiler-owned build provenance and
bound to the exact object selected for linkage. Validation requires matching
producer provenance, compiler/build policy, schema, interface version,
target/ABI, interface digest, object digest, object marker, declaration
mapping, canonical payload digest, and the complete semantic dependency-
closure digest. An ordinary third-party provider that
supplies all of those bytes itself has not established producer provenance and
remains bodyless/untrusted.

The body-rechecked alternative is valid only when the consuming compiler also
lowers that exact checked body and the build links the object produced by that
same trusted compile action. It may not recheck a retained body and then pair
the result with a provider-supplied object. Selecting any prebuilt provider
object instead enters the attested Level-B path and requires accepted producer
provenance plus exact-object binding.

Import-time validation of an object-bound fact creates an unforgeable link
obligation containing the resolved module/definition identity, exact object
digest, producer provenance, target/ABI, payload digest, and the accepted
transitive semantic dependency/link obligations. That obligation must survive
cache reuse and object selection. The final linker (or an
equivalent compiler-owned pre-link gate) revalidates it against the object
actually selected and recursively against every exact dependency object and
contract selected for the link. Substitution, omission, stale dependency
semantics, or a digest/provenance mismatch fails the build. Import-time
adjacency checks alone do not close this TOCTOU boundary.

This is compiler attestation, not an independently checked proof. Its trusted
computing base includes the provider Sema, lowering, CodeGen, attestation
writer, provenance store or verifier, importer, and linker-object selection.
It can discharge the provider's implementation obligation for a contract whose
authority is already defined and recomputed from the signature. It cannot add,
broaden, or synthesize caller authority beyond that declared contract.

### 7.3 Compiler/runtime intrinsic facts

Builtin copy/drop behavior, allocator conventions, runtime task state
machines, and other intrinsic contracts may be selected only through compiler
or resolver-owned provenance tied to the active compiler/runtime ABI.

An interface, package mapping, manifest field, filename, or ordinary include
path cannot create or override an intrinsic fact.

### 7.4 Foreign and unsafe assumptions

Native acquire/release behavior, raw-pointer validity and provenance, foreign
aliasing, thread affinity, callbacks, and author-maintained unsafe invariants
remain explicit assumptions absent a dedicated verifier.

A manifest may identify and scope such assumptions for auditing, but cannot
reclassify them as recomputed or compiler-proved facts. Foreign assumptions do
not directly grant safe authority; a safe wrapper must separately satisfy its
language-defined shell obligations and retain the foreign behavior inside an
explicit unsafe trust boundary.

## 8. Validation and failure policy

### SME-FAIL-01: Structural and safety-payload validation

The envelope framing, canonical digest, identity mapping, and object binding
are validated before either record class is consumed; failure there rejects
the whole envelope. `SafetyRequired` records are then accepted as one validated
payload for a module and object binding. A malformed, duplicated, missing,
mismapped, stale, unsupported, or tampered required record rejects that safety
payload as a whole. No surviving subset may affect language acceptance or safe
lowering.

An explicitly `OptionalOptimization` record may downgrade independently only
after envelope integrity is established, only when no safety record depends on
it, and only to an implementation path whose language behavior is identical.
This is not partial trust in a safety payload.

### SME-FAIL-02: No authority from downgrade

Failure behavior depends on the consumer:

- optional optimization evidence may downgrade conservatively to no
  optimization;
- a fact required to type-check or grant language authority causes the import
  or affected declaration use to fail closed; and
- a foreign declaration may proceed only through its explicit unsafe/extern
  boundary, never by treating a failed safe attestation as advisory.

Unknown schema versions, unknown trust classes, or unknown safety-critical
record kinds never mean Allow.

### SME-FAIL-03: Standalone TKI boundary

A standalone or ordinary third-party TKI may carry declarations and
recomputed facts. It cannot establish compiler-attested, intrinsic, or trusted
system provenance merely by serializing those labels.

Safe caller authority comes from the recomputed declaration/signature
contract. Reliance on a provider's claimed fulfilment of that contract requires
a rechecked body or a valid accepted-provenance, exact-object-bound compiler
attestation. The attestation cannot create a stronger contract. Without either
proof source, a bodyless provider fails closed or remains explicitly unsafe.

## 9. Relationship to `.tke`

The existing trusted-memory-evidence sidecar provides useful implementation
experience:

- exact backing-object SHA-256 binding;
- a marker retained in the object;
- canonical payload validation;
- full-artifact rejection; and
- conservative cache-miss behavior.

This RFC may reuse those design lessons, but it does not widen `.tke`.
Trusted memory evidence remains an optimizer cache and must not change parsing,
type checking, PAL, ownership, effects, async acceptance, or diagnostics.

Language-semantic attestation requires its own schema, consumer gates, version
policy, and conformance evidence even if a future implementation shares
low-level hashing or canonicalization code.

## 10. Payload freeze gate

This envelope may be reviewed and prototyped before its semantic payload, but
no language payload ABI is frozen until all of the following exist:

1. one qualified synchronous PlaceState Core shared by Sema, CodeGen cleanup,
   and source-less declaration replay; any async cleanup record additionally
   waits for the separate async/place bridge;
2. implemented Level-A whole-place synchronous `init` contracts;
3. a stable typed `OutcomeTransition` representation already exercised at the
   source/body-rechecked completion level;
4. callee and caller conformance tests for branch-indexed post-states at that
   level;
5. stable declaration, variant, formal, and bounded path identities; and
6. a decision about which payload facts are recomputed, body-attested,
   intrinsic, or explicit foreign assumptions.

Possible future records include Encap policy, Copy/Dup, DropPlan,
ResourceContract, init/outcome transitions, unsafe-wrapper obligations, and
async cleanup. This list is illustrative and freezes no record schema.

## 11. Adversarial acceptance matrix

Before any safety-relevant consumer is enabled, tests must cover:

- forged module coordinate, source path, trusted-system label, and DefId;
- stale interface, compiler, schema, target, ABI, and payload versions;
- payload reorderings, omissions, duplicates, and unknown record kinds;
- provider attempts to relabel a required record as optional or to select a
  stronger trust class than its consumer-owned schema permits;
- copied manifest with a different TKI or backing object;
- a provider object whose own bytes are unchanged but whose checked semantic
  dependency contract, proof profile, or manifest has changed;
- modified object with an unchanged manifest;
- object substitution after import validation but before final link;
- transitive dependency-object substitution or omission at final link;
- a compliant retained body paired with a different provider-supplied object;
- modified manifest with an unchanged object marker;
- generic declaration/instance mismapping;
- ordinary standalone TKI attempting each stronger trust class;
- foreign metadata attempting to create safe ownership authority;
- atomic rejection with no partially retained safety facts; and
- identical source, source-less-attested, and conservative-failure decisions.

## 12. Completion condition

The envelope RFC is complete when identity, canonicalization, trust-class and
criticality selection, accepted producer provenance, object binding, and
failure behavior have an implementation-backed tamper matrix. Completion does
not imply semantic proof or that any language-semantic payload is stable.
Payload standardization begins only after the gate in Section 10.
