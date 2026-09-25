# Toka v1.0.0-rc.13

RC13 is a release candidate, not a Toka 1.0 stability declaration. The source
candidate is commit `6fb3337fa4f9956392326a7783c9dcdecabd7e5b`. The
[four-target release qualification](https://github.com/tokalang/toka/actions/runs/36093979008)
passed for that exact commit on Linux x64/ARM64 and macOS x64/ARM64. This draft
is not a promotion or a public release.

## Language and migration boundary

- Caller-side explicit `cede` is active on the qualified call routes: a named
  source transferred to a consuming parameter must show the transfer at the
  call site. Eligible complete temporaries remain exempt; ordinary Copy and
  borrowed arguments do not silently become consuming transfers. Named
  return sources use their validated source spelling, including `return cede
  value` where required. The old `-> cede T` result qualifier is removed;
  parameter `cede` contracts remain.
- Generic `T` now carries its complete, permitted value morphology without a
  user-written generic apostrophe placeholder. This does not grant Copy,
  ownership, write access, or dependency independence. The separate proposal
  for checked dependency-contract elision (E) is **not** part of RC13.
- Ordinary data and handle write permissions are requested at the binding
  name or handle selector. A name-side `#` is a request, not proof of a writable
  source. The explicit unsafe Addr-to-raw construction contract remains
  bounded by source, nullable, borrow and permission checks. Callable-receiver
  syntax is a separate protocol; no `fn_` redesign is included.
- The compiler-interface/cache key is **`0.9.9-23`**. Rebuild old TKI and
  caches; interfaces from earlier keys are not interchangeable. Source-hidden
  qualification can revalidate retained implementation bodies and execute the
  associated cleanup/helpers locally. A declaration-only interface without
  required evidence remains fail-closed; this is not opaque-binary support.

See the [explicit cede RFC](semantic_core/explicit_call_boundary_cede_rfc.md)
and [whole-value generics RFC](semantic_core/whole_value_generics_and_checked_dependency_elision_rfc.md)
for the detailed accepted rules and their remaining boundaries.

## Standard library and tools

- JSON and YAML use library-side complete-value/flat-document representations
  instead of requiring a general recursive-container proof. Container,
  iterator, byte-buffer, return-source and thread/sync call sites were
  migrated to the accepted ownership contracts. Existing package consumers
  may need source and API migrations; old spellings are not compatibility
  shims.
- Host mailbox imports move from `std/task` to `std/task_mailbox`; no forwarding
  module is supplied. GUI, regex and `trg` consumer qualification are separate
  from this Toka archive qualification.
- The relocatable SDK, project-aware commands, installer checksum verification,
  source-hidden execution and release-gate regressions remain part of the
  four-target qualification. `SHA256SUMS` covers the four platform archives.

## Known limits

- A borrowed-referent shadowing case may conservatively report E0455 even for
  a valid return. This recorded P2 is a false rejection, not permission to
  return a dangling borrow.
- Extern aggregate lowering and declaration-only source-hidden callable
  qualification remain bounded separately; this candidate does not claim
  general support for those cases.
- E (dependency-contract elision) and `fn_` design work are deferred. Windows
  remains a source-build dogfood target, not a packaged RC13 SDK target.

Platform archives target Linux x64/ARM64 and macOS x64/ARM64. Do not treat a
draft release or a passing qualification workflow as final promotion.
