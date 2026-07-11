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
- packed/array/enum/union shape kind,
- layout-relevant attributes,
- borrow-like field types,
- resource-bearing field facts needed for drop/clone/copy prevention,
- Send/Sync-relevant structural facts.

Rationale:

- Visibility determines access, not compiler knowledge.
- PAL path reasoning and resource checks can depend on private structure.

Current exporter reference:

- `TKIExporter::exportShape`

Open audit item:

- The exporter currently has legacy support paths for shape life dependencies.
  Toka 1.0 syntax excludes shape header dependencies and shape-internal member
  dependency annotations. The interface contract should ensure unsupported
  dependency syntax is not used as a substitute for stable-placement semantics.

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

## Phase-1 Missing Replay Coverage

The current test suite has broad source-level coverage. The following replay
cases should become explicit conformance tests in phase 2:

- PAL call-site alias conflict through imported `.tki`: covered by
  `tests/semantics/tki_replay/cases/pal_call_001_alias`.
- Cede parameter and cede return obligations through imported `.tki`: covered
  by `tests/semantics/tki_replay/cases/own_cede_001_signature` and
  `tests/semantics/tki_replay/cases/own_cede_002_return`.
- Whole-return dependencies through imported `.tki`: covered by
  `tests/semantics/tki_replay/cases/eff_ret_001_return_deps`.
- Member-specific `effects:` dependency declarations through imported `.tki`:
  export, source-less parsing, and caller-side source locking covered by
  `tests/semantics/tki_replay/cases/eff_member_001_return_deps`; field-sensitive
  release/transfer and negative member-swap replay remain open.
- Private resource fields causing downstream copy/destructure rejection:
  covered by
  `tests/semantics/tki_replay/cases/own_resource_001_private_field` for copy
  capture and naked destructuring; spread and generic cases remain open.
- Public unsafe/raw API redline through imported `.tki`: covered by
  `tools/scripts/test_tki_unsafe_revalidation.sh` for parameters, returns,
  fields, generic declarations, forged exempt-looking source paths, ordinary
  include paths, explicit naming exemptions, and compiler-configured trusted
  roots.
- Async borrowed return dependency through imported `.tki`: positive replay
  export, source-less parsing, and caller-side enforcement after `.wait`
  covered by `tests/semantics/tki_replay/cases/async_suspend_001_return_deps`;
  borrowed `.start` rejection is covered by the same case, and explicit cede
  handoff is covered by
  `tests/semantics/tki_replay/cases/async_start_001_cede_handoff`; in-function
  suspension remains open.
- Cache invalidation when only semantic annotations change: covered by
  `tests/semantics/tki_cache/cases` for parameter mutability, `cede`
  parameters, effects routing, and async markers. Resource structure,
  drop/clone, generic constraints, and trait metadata remain open.
