# RFC: Canonical Declaration Witness

**Status:** Proposed P1 schema with an implemented audit codec and explicit
validation profile. The profile validates a separate `.tki.tsm` sidecar for
the admitted P1 subset only when `--validate-semantic-manifests` is selected;
the `@tki v2 cdw1:` comment remains ignorable. Neither changes source syntax,
metadata authority, object-link rules, or the bodyless-provider boundary. The
existing `@tki v2 outcome_transition:` comment remains a distinct audit
artifact.

**Depends on:** `semantic_manifest_envelope_rfc.md`, resolver-owned module
coordinates, the resolved `OutcomeTransition` representation, and Level-A
retained-body replay. It is deliberately prior to any bodyless Level-B
attestation.

## 1. Purpose

A Canonical Declaration Witness (CDW) is the candidate machine representation
of a fact the importing compiler can reconstruct from declarations. It exists
to make a later manifest comparison unambiguous; it does not prove a body,
grant authority, or authenticate a provider.

The first candidate record is the signature-level shape of a narrow Outcome
Contract:

```text
function identity
  + outcome formal identity
  + direct nominal return-enum identity
  + exhaustive (variant identity -> init | uninit) cases
```

This is intentionally narrower than an Outcome fulfilment proof. The witness
can say what a declaration promises; only a retained body recheck or a later
accepted-provenance, exact-object-bound attestation can say that the callee
fulfilled that promise.

## 2. Non-goals

CDW P1 is not:

- a user-visible `witness` or `outcomes` syntax extension;
- a replacement for parsing the ordinary TKI declaration;
- a digest, signature, package identity, or supply-chain credential;
- permission to use a bodyless `TKI + object` Outcome provider;
- a serialization of `OutcomeWitness`, `InitAuthority`, place state, cleanup,
  PAL facts, or a caller's local place;
- support for methods, generic Outcome functions, nested variants, paths, or
  arbitrary protocol states; or
- a decision to embed a manifest in TKI instead of a compiler-owned sidecar.

The `@tki v2` audit comments remain ignorable. In particular, an `unbound`
audit identity is evidence for tests and debugging only; it is never a valid
`ModuleCoordinate` for a future witness record. A `coordinate=known` audit
marker is likewise only an observability signal, not a witness or authority.

## 3. P1 record and consumer-owned classification

P1 defines one record kind:

```text
RecordKind:       outcome-transition
Criticality:      SafetyRequired
TrustClass:       RecomputedDeclarationFact
Subject:          StableFunctionDefId
Payload:          OutcomeTransitionDeclaration
```

`Criticality` and `TrustClass` are fixed by the consuming schema. A producer
cannot relabel this record as optional or body-attested. The record is
redundant validation data: an importer must parse the ordinary declaration,
resolve the same identities, reconstruct the record, canonicalize it, and
compare bytes. A matching record adds no capability; a bodyless callee still
requires Level B and otherwise fails closed.

The default importer neither reads nor requires this record. The explicit
P1.2 validation profile reads only the strict sidecar for a resolver-selected,
known-coordinate source-less TKI, reconstructs the complete CDW1 set from
declarations, and compares the canonical bytes atomically. The current
Level-A retained-body path remains the only enabled source-less Outcome
fulfilment path.

## 4. Identity domain

Every P1 identity is rooted at a resolver-proven coordinate:

```text
ModuleCoordinate = { CrateId, LogicalModulePath }
StableFunctionDefId = ModuleCoordinate + FunctionIdentity
```

P1 admits only a known coordinate. Absolute paths, `source_path`, cache paths,
basenames, AST pointers, source offsets, NodeSerial values, and process order
are rejected inputs.

The canonical declaration components are:

| Component | Canonical content |
| --- | --- |
| Function | declaration kind, canonical name, generic arity, effect kind, ordered parameter contract kinds and canonical physical types, canonical result type |
| Outcome formal | containing `StableFunctionDefId` plus its zero-based formal index |
| Return enum | module coordinate, nominal enum kind/name, generic arity |
| Variant | containing enum identity, canonical variant name, and zero-based declaration ordinal |

The ordinal is not a substitute for the name: both must agree. P1 has no
generic Outcome function record, so generic domains, substitutions, and
constraints remain an explicit activation blocker rather than an implicit
source-text fallback.

### 4.1 Current audit-only type boundary

The implementation now records a candidate `toka-outcome-type-v1` identity in
the `outcome_transition` audit comment instead of using `Type::toString()`.
It is not CDW1 byte encoding and is not parsed by the importer. Its deliberately
small domain admits only concrete first-order physical types:

- unit, ABI void, never, and primitive types;
- raw, unique, shared, and reference handles over an admitted type;
- fixed-size arrays with a numeric extent and slices over an admitted type; and
- non-generic nominal types whose defining declaration has a resolver-known
  coordinate.

Every node records its physical `cede`, writable, may-zero, and blocked bits.
The may-zero bit is admitted only for raw `nul *T`; value, borrow, unique, and
shared nodes always record false. The existing wire position is retained for
candidate-schema compatibility and does not reintroduce Safe nullable types.
A nominal node records its defining shape identity rather than an unqualified
display name. Function types, dynamic function types, `Uninit`, anonymous
records, projections, variant suffixes, symbolic const extents, generic
arguments, and strong aliases are outside this domain. In each such case (or
when any nominal owner is coordinate-unbound), the complete audit record says
`type-domain=unavailable`; it must not fall back to a source spelling.

This boundary makes source and retained-body source-less replay observable
without prematurely choosing CDW1's final binary type grammar. It also keeps
the eventual generic-binder and strong-alias definition identities as explicit
work, rather than accidentally treating a temporary synthetic shape name as a
stable witness subject.

## 5. Canonical byte encoding

The proposed schema identifier is `toka.declaration-witness`, version `1`.
It uses a length-framed byte grammar; pretty printing is never canonical:

```text
CDW1 = Magic Version RecordCount Record*
Magic = bytes("toka.declaration-witness\0")
Version = u16be(1)
RecordCount = u32be
Record = u32be(RecordByteLength) RecordBytes
RecordBytes = u16be(FieldCount) Field*
Field = u16be(Tag) u32be(ByteLength) ExactBytes
```

The outer record has exactly these fields, in this order:

| Tag | Field | Exact bytes |
| --- | --- | --- |
| `0x0001` | record kind | UTF-8 `outcome-transition` |
| `0x0002` | criticality | UTF-8 `SafetyRequired` |
| `0x0003` | trust class | UTF-8 `RecomputedDeclarationFact` |
| `0x0004` | subject | nested canonical field list |
| `0x0005` | payload | nested canonical field list |

The subject field has, in order, crate id (`0x0101`), logical module path
(`0x0102`), declaration kind `function` (`0x0103`), canonical function name
(`0x0104`), generic arity as `u32be` (`0x0105`), effect kind (`0x0106`), the
ordered parameter sequence (`0x0107`), and canonical result type (`0x0108`).
Each parameter sequence item is a nested field list containing its zero-based
index, `init`/`ordinary` contract kind, `cede` bit, and canonical physical type.

The payload field has, in order, formal index as `u32be` (`0x0201`), return
enum identity (`0x0202`), and the case sequence (`0x0203`). A return-enum
identity is a nested field list of its module coordinate, `enum` kind, name,
and generic arity. Each case sequence item contains the complete variant
identity and its `init`/`uninit` post-state. Sequence items are length-framed
nested field lists; their outer sequence is ordered by the complete encoded
variant identity.

The tag values and order above are schema-owned, not provider choices. UTF-8
identifiers and type encodings must already be the compiler's normalized
canonical forms. No platform newline, locale, map iteration order, quoted
display text, or filesystem spelling participates.

### 5.1 Current audit-only codec prototype

The compiler now encodes this exact P1 subset when all admission gates close:
resolver-known function and return-enum coordinates, a non-generic top-level
function and direct non-generic enum, and complete
`toka-outcome-type-v1` physical identities for every parameter and the result.
It writes the raw CDW1 bytes as lowercase hexadecimal in one ignorable
`@tki v2 cdw1:` comment. The comment is an observability carrier, not a
manifest field or an import format.

The implementation realizes the nested forms above as:

```text
FieldList = u16be(FieldCount) Field*
Sequence  = u32be(ItemCount) (u32be(ItemByteLength) ItemBytes)*
```

It checks strictly increasing field tags, UTF-8 text, fixed-width integer
lengths, positional parameter indices, a valid `init` outcome formal, and
duplicate complete variant identities. Cases are sorted by their complete
encoded variant identity, so source declaration order does not affect CDW1
bytes. The retained-body source-less replay test requires identical emitted
bytes; malformed, absent, or altered `cdw1:` comments remain deliberately
ignored by the importer.

The standalone strict decoder accepts only one complete CDW1
`outcome-transition` record. It validates every frame, tag set, fixed-width
value, UTF-8 identity, nested enum reference, parameter position, and post-state
atom, then re-encodes the decoded record and requires byte-for-byte equality.
Thus a sequence that is structurally readable but non-canonical is rejected.
The decoder has no production `.tki` comment or importer call path. The
source-less conformance test alone extracts a produced comment, passes its raw
hexadecimal to the standalone codec, and compares the result with the
declaration-reconstructed source-less bytes. It also proves that malformed
comment bytes are rejected by the codec while the compiler continues to ignore
them.

The prototype therefore cannot make a declaration more acceptable, permit a
bodyless provider, or create any caller, cleanup, or object-link authority.
The retained-body replay audit additionally proves that malformed, missing, or
repeated `cdw1:` comments leave Level-A caller acceptance unchanged. P1.2 adds
the separate manifest carrier and atomic comparison under an explicit profile;
missing or non-canonical sidecars fail that profile with `E04633`, but comments
remain non-authoritative in every mode.

For `outcome-transition`, the payload order is exactly:

1. Outcome-formal identity;
2. return-enum identity;
3. case count; and
4. cases sorted lexicographically by their complete canonical variant identity,
   each followed by the enum atom `init` or `uninit`.

Repeated fields, duplicate record subjects, duplicate variants, unknown
required tags, invalid UTF-8, non-canonical order, an unsupported version, or
a trailing byte are malformed. There is no permissive parse mode for a safety
record.

## 6. Comparison and failure policy

For a known-coordinate interface in the explicit P1.2 validation profile, the
consumer performs one atomic comparison:

```text
parse TKI declaration
    -> resolve declaration identities
    -> reconstruct CDW1 record
    -> canonicalize provider and reconstructed records
    -> byte-for-byte equality
```

Missing, stale, altered, duplicate, unknown, or mismapped required records
reject the declaration-witness payload as a whole. No surviving record subset
may affect an Outcome call, PlaceState, cleanup, CodeGen, or caller authority.
This comparison validates only the signature-level declaration fact. It never
converts a provider object into the object associated with a rechecked body.

The activated consumer must reject a record whose `ModuleCoordinate` differs
from its resolver-derived coordinate, even if every declaration spelling and
byte field otherwise matches. A provider-controlled `source_path` cannot repair
that mismatch.

## 7. Relationship to the current audit records

The current `@tki v2 outcome_transition:` comment establishes two useful P1
implementation facts:

- Sema stores resolved formal and variant identities rather than consuming the
  parsed Outcome strings; and
- source-less retained-body replay reconstructs the same audit bytes without
  relying on AST address or source path.

Its function signature uses the audit-only `toka-outcome-type-v1` identity for
the admitted concrete first-order domain, including resolver-owned nominal
definitions. A strong alias or another unsupported type form produces
`type-domain=unavailable` rather than a text fallback. This gives the future
encoder a conservative source/source-less oracle, but does not make the text
format a CDW1 payload.

The audit record also carries `coordinate=known` only when the resolver has a
known coordinate for both the function owner and the direct return-enum owner;
otherwise it carries `coordinate=unbound`. Ordinary local compilation remains
valid in the latter case. This reports the P1 coordinate boundary without
making the comment part of TKI import, caller acceptance, or cache authority.

The textual `outcome_transition:` comment is intentionally not CDW1: it
permits `unbound` local test modules and is not parsed by the importer. The
separate `cdw1:` comment carries an encoder probe only for the fully admitted
known-coordinate P1 subset. It is still not parsed by the profile or default
importer; an altered hexadecimal payload has no import effect. Neither comment
may be promoted in place.

## 8. Activation gate

Implementing CDW1 requires all of the following at one exact compiler revision:

1. canonical type and generic-domain encodings for every admitted declaration;
2. resolver-known coordinates and stable owner identity for every admitted
   record, with tests that reject an unbound or forged coordinate;
3. a structured encoder and strict decoder independent of `@tki` comments;
   the audit-only codec prototype now meets this representation boundary;
4. importer reconstruction and atomic byte comparison before any consumer can
   observe a record; P1.2 meets this for its explicit profile;
5. duplicate, reorder, omission, unknown-tag/version, and altered-identity
   tamper tests; P1.2 covers the sidecar schema and profile boundary;
6. source, retained-body source-less, and declaration-recomputed replay
   equality for caller acceptance, rejection, place state, and cleanup; and
7. an explicit decision whether P1 remains top-level/non-generic or grows only
   after method-owner and generic binder identities have equivalent coverage.

CDW1 activation still does not enable bodyless Outcome calls. Level B adds a
separate record kind for body fulfilment and must satisfy the complete
provenance and exact-object-binding rules in the Semantic Manifest Envelope.

## 9. Next decision

The resolver boundary and conservative concrete-type seed are now exercised by
the encoder prototype: an explicit workspace, package, or toolchain node can
provide a stable coordinate, while ordinary local compilation remains
coordinate-unbound and emits no CDW1 probe. The prototype proves deterministic
source/retained-body source-less encoding without turning its comment into
authority.

The next implementation decision is not "add a digest." The P1.2 profile has
an importer-visible declaration comparison with `E04633` failure behavior, but
its default-off compatibility boundary stays frozen. Any broader package policy
or activation remains separate from generic binders, strong aliases, and every
bodyless-provider design.

The current artifact boundary and the remaining activation gates are recorded
in [`canonical_declaration_witness_activation_audit.md`](canonical_declaration_witness_activation_audit.md).
