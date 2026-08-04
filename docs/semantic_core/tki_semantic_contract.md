# TKI Semantic Contract

This document records the semantic facts that `.tki` interfaces must preserve
for Toka 1.0. The goal is source/source-less equivalence: importing a module
from `.tki` must not weaken PAL, ownership, effects, async, visibility, or
unsafe-boundary checks compared with importing the original `.tk` source.

This is an audit contract over the current exporter/importer behavior, not a
new interface-file syntax proposal.

## Core Principle

A `.tki` file is not only an ABI summary. It is a semantic replay artifact.
Visibility controls what user code may name; it must not erase facts the
compiler needs for checking downstream code.

Compatibility metadata is not a trust credential. In particular,
`source_path` is supplied by the interface and cannot grant standard-library,
prelude, build-file, or test exemptions.

### Interface Trust Boundary

Source-less and third-party interfaces are untrusted semantic inputs. Their
public declarations must be checked against the same unsafe API redlines as
source declarations, including raw pointer parameters, returns, and fields.
Explicit `unsafe_`, `raw_`, `Unsafe*`, and `Raw*` naming remains the only
interface-controlled exemption.

Compiler-provided standard-library interfaces form a trusted system boundary.
That status is held only in resolver state and is never serialized into `.tki`.
It may be established when the physical interface is resolved from a
compiler-configured standard-library root, or when a real source module from
such a root is replaced by its build-cache interface. Package mappings,
ordinary include paths, and interface metadata cannot establish this status by
themselves.

## Required Semantic Facts

### Module And Cache Identity

The dependency manifest and resolver must retain enough metadata to reject stale
or incompatible interfaces:

- compiler version,
- interface format version,
- target triple,
- source path,
- source hash,
- loaded content hash,
- cache status and reason.

Relevant files:

- `docs/dependency_manifest_schema.md`
- `src/main.cpp`
- `src/Basic/ModuleResolver.cpp`
- `include/toka/InterfaceVersion.h`

### Function Signatures

Function declarations exported to `.tki` must preserve:

- public/private exported availability as required by the resolver,
- parameter order, names, and types,
- handle morphology: `&`, `*`, `^`, `~`,
- handle rebinding permission such as `^#p` and blocked rebinding such as
  `^$p`,
- payload mutability and blocked payload inheritance such as `x#` and `field$`,
- nullability markers,
- `cede` parameter obligations,
- return type,
- `cede` return marker,
- async/effect return shape,
- generic parameters, morphic parameters, and constraints,
- default argument facts if they affect call checking.

The current exporter entry points include:

- `TKIExporter::exportFunction`
- `TKIExporter::printArg`
- `TKIExporter::exportReturnType`

### Escaping Dependencies And Effects

Borrow-like values crossing a function boundary must be replayable from `.tki`.
The interface must preserve:

- whole-return dependencies such as `-> &T <- x`,
- named return dependencies,
- `effects:` blocks,
- member-specific dependencies such as `return.left <- a`,
- dotted dependency paths,
- async return dependencies such as an eventual borrowed result.

Implementation reference:

- `src/AST/TKIExporter.cpp`, especially `exportFunction`

Replay requirement:

- Call sites must never need to inspect a source body to decide whether an
  escaped borrow is valid.

### Outcome Contracts And Retained Bodies

An `outcomes:` block is declaration data and must round-trip with its exact
`init` formal, direct return variants, and `init`/`uninit` post-states. On
import, Sema resolves those spellings to the formal index and the nominal enum
member identities before callers, callee return checks, or CodeGen use them.

The first source-less completion level is retained-body rechecking. A generated
TKI retains the body of a resolved Outcome function, and source-less CodeGen
lowers that rechecked body instead of accepting a provider object as evidence
of fulfilment. A bodyless interface carrying the same declaration is not a
Level-A provider and calls through it fail with `E04631`.

This is not a Semantic Witness ABI or a bodyless attestation. Canonical wire
identities, declaration witnesses, exact-object binding, and provider
provenance remain governed by `semantic_manifest_envelope_rfc.md`.

For the later declaration-identity gate, the exporter emits an audit-only
`@tki v2 outcome_transition:` record for every resolved top-level Outcome
function. The record length-frames its resolver module coordinate, function
signature, formal index, enum declaration, and variant name/ordinal, and is
stable across source-less TKI replay. It contains no source path, source
location, AST address, or authority claim; parser/importer behavior is
unchanged by the comment. Its audit-only `coordinate=known` label appears only
when both the function and direct return-enum owners have resolver-known
coordinates; ordinary local compilation instead reports `coordinate=unbound`.
Neither label changes importer behavior. The function signature uses an
audit-only `toka-outcome-type-v1` identity for concrete first-order physical
types with resolver-known nominal owners; unsupported generic, strong-alias,
projection, anonymous-record, symbolic-extent, and callable forms report
`type-domain=unavailable` rather than falling back to display text.

For the fully admitted known-coordinate, non-generic concrete first-order
subset, the exporter also emits one lowercase-hex `@tki v2 cdw1:` encoder
probe. Its raw bytes follow the candidate CDW1 declaration-witness grammar and
must remain equal across retained-body source-less replay. The parser and
importer ignore this comment completely: missing, malformed, or altered
hexadecimal does not change Level-A acceptance and cannot enable a bodyless
provider. A future strict decoder and independently reconstructed comparison
remain an activation gate.

### Shape Structure

Shape definitions must preserve all structure needed for semantic checking,
including information hidden from user access by visibility:

- field names and order,
- private fields,
- field types,
- field handle morphology,
- field payload mutability and blocked inheritance,
- field nullability,
- field defaults when semantically relevant,
- array/enum/union shape kind,
- layout-relevant attributes,
- borrow-like field types,
- resource-bearing field facts needed for drop/clone/copy prevention,
- Send/Sync-relevant structural facts.

Rationale:

- Visibility determines access, not compiler knowledge.
- PAL path reasoning and resource checks can depend on private structure.

Current exporter reference:

- `TKIExporter::exportShape`

Closed 1.0 boundary:

- `ShapeDecl` and `TKIExporter` have no legacy shape-header dependency path.
  Borrow-like fields retain dependency facts from their initializers without
  reviving excluded declaration syntax.
- `tools/scripts/test_tki_excluded_syntax_revalidation.sh` forges interfaces
  containing shape-header and shape-member dependency declarations and proves
  that interface parsing rejects them with `E01247` and `E01248`.

### Traits, Associated Types, And Dyn Safety

Interfaces must preserve:

- trait names and method signatures,
- associated type declarations,
- `type` vs `per type` mode,
- trait prerequisites and bounds,
- impl signatures needed for generic checking,
- object-safety-relevant facts for `dyn @Trait`,
- rejection of associated-type binding syntax for 1.0 dyn trait objects.

Replay requirement:

- A trait object or generic call accepted through source import must have the
  same result through `.tki` import.

### Ownership And Resource Facts

Interfaces must preserve facts needed to reject resource duplication and invalid
transfer:

- which parameters consume through `cede`,
- which returns transfer through `cede`,
- which shapes manage resources,
- whether drop/clone implementations exist when required,
- whether raw pointer fields are present,
- whether resource fields are private,
- whether public APIs expose raw unsafe representation.

Replay requirement:

- A downstream caller must not be able to duplicate, copy-capture, destructure,
  or spread a resource because the source body or private field was hidden.

### Async And Execution Boundaries

Interfaces must preserve:

- async return types,
- task-handle result types,
- dependency annotations on eventual async results,
- execution-boundary function signatures such as thread/task spawn consumers,
- cede/copy requirements visible at call sites.

Replay requirement:

- Detached execution must not gain implicit borrowed captures through an
  interface boundary. `.start` must reject imported async calls with ordinary
  shape/borrow arguments or PAL dependencies, while preserving explicit cede
  handoff from both the parameter and call site.

## Cache Invalidation Rules

Any change to these facts must invalidate stale `.tki` replay:

- parameter access class or morphology,
- cede marker on parameter or return,
- return dependency or `effects:` routing,
- shape field morphology, mutability, nullability, or private structure,
- resource/drop/clone-relevant shape structure,
- trait associated type or object-safety facts,
- async return shape or dependency,
- public unsafe/raw API exposure,
- interface format version.

## FZ-2 Conformance Coverage

The following matrix records the explicit source/interface conformance tests
completed in phase 2:

- PAL call-site alias conflict through imported `.tki`: covered by
  `tests/semantics/tki_replay/cases/pal_call_001_alias` for read/read,
  overlapping and disjoint member paths, and cede/read conflicts.
- Cede parameter and cede return obligations through imported `.tki`: covered
  by `tests/semantics/tki_replay/cases/own_cede_001_signature`,
  `own_cede_002_return`, and `own_cede_003_generic_methods`, including generic
  functions, methods, double consumption, and use after transfer.
- Synchronous `init` parameter contracts through imported `.tki`: covered by
  `tests/semantics/tki_replay/cases/init_002_parameter`. The interface retains
  the `init` formal and explicit call-place spelling; source-backed and
  source-less consumers agree that a normal call (including the retained
  generic-body form) makes the caller place live, while a missing handoff
  reports `E04622`.
- Whole-return dependencies through imported `.tki`: covered by
  `tests/semantics/tki_replay/cases/eff_ret_001_return_deps` for references,
  `str`, and `bytes` views.
- Member-specific `effects:` dependency declarations through imported `.tki`:
  export, source-less parsing, and caller-side source locking covered by
  `tests/semantics/tki_replay/cases/eff_member_001_return_deps`, including
  field-sensitive transfer, unrelated-source release, and swapped routing.
- Private resource fields causing downstream copy/destructure rejection:
  covered by
  `tests/semantics/tki_replay/cases/own_resource_001_private_field` for copy
  capture and naked destructuring, and by
  `tests/semantics/tki_replay/cases/own_resource_002_spread_generic` for generic
  private resource fields, spread, and copy capture.
- Public unsafe/raw API redline through imported `.tki`: covered by
  `tools/scripts/test_tki_unsafe_revalidation.sh` for parameters, returns,
  fields, generic declarations, forged exempt-looking source paths, ordinary
  include paths, explicit naming exemptions, and compiler-configured trusted
  roots.
- Excluded shape dependency syntax cannot re-enter through a forged interface:
  covered by `tools/scripts/test_tki_excluded_syntax_revalidation.sh` for both
  removed declaration forms and their stable diagnostics.
- Async borrowed return dependency through imported `.tki`: positive replay
  export, source-less parsing, and caller-side enforcement after `.wait`
  covered by `tests/semantics/tki_replay/cases/async_suspend_001_return_deps`;
  borrowed `.start` rejection is covered by the same case, and explicit cede
  handoff is covered by
  `tests/semantics/tki_replay/cases/async_start_001_cede_handoff` for concrete
  and generic resources. The same async-return case also checks `.await`
  inside an async function and source invalidation after resume through both
  source and source-less replay.
- Cache invalidation when only semantic annotations change: covered by
  `tests/semantics/tki_cache/cases` for parameter mutability, `cede`
  parameters, effects routing and swapping, async markers, private resource
  structure, deleted clone, generic function and impl constraints, trait
  prerequisites, associated type bindings, and dyn object safety. Every case
  proves old-interface acceptance, `SourceHashMismatch` fallback, source-side
  rejection, and fresh source-less interface rejection.
