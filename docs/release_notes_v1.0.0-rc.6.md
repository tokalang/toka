# Toka v1.0.0-rc.6 Release Candidate Notes

These notes describe the contents associated with `v1.0.0-rc.6`; they are not
qualification or publication evidence. The immutable candidate, gate reports,
asset digests, clean-replay receipts, and current release state belong in the
[RC6 qualification audit](release_audits/v1.0.0-rc.6.md) and the corresponding
GitHub Release record.

## Purpose

RC6 is a 1.0 release candidate from Toka's frozen language surface. It delivers
key standard library capabilities, compiler verification refinements, syntax ergonomics,
and official package ecosystem cutovers while establishing the `0.9.9-11` interface
compatibility boundary.

## Candidate improvements

- **Std / Stdx Additions:**
  - Extended `Vec` element read indexing to `@Dup` types without requiring manual clone invocations.
  - Added `stdx/encoding/yaml.tk` for robust YAML parsing and serialization.
  - Added `stdx/text/template.tk` offering a lightweight template rendering engine.
  - Added `std/fs/temp.tk` providing secure temporary file creation with atomic fail-safe semantics.
- **Call-site Mutable Sigil Enforcement:**
  - Enforced explicit `#` sigil requirement at call sites when passing arguments to mutable parameters (`W0408` warning, full verification suite coverage).
  - Migrated standard library, stdx, and internal tooling call sites to explicit `#` annotations.
- **Syntax & Ergonomics:**
  - Added field punning shorthand support in shape literal construction (`Shape(x, y)` expanding to `Shape(x = x, y = y)`).
- **Type Checker & Flow Analysis:**
  - Enforced dual capability verification at call boundaries: validating both declared write authority on bindings and shared flow ceilings.
- **Official Ecosystem Cutover:**
  - Cut over official Redis client to standalone release (`redis@0.2.0`).
  - Updated ecosystem compatibility fixtures (including `openai_compat@0.1.2`).
- **Interface Cache Boundary:**
  - Compiler-interface compatibility key bumped to `0.9.9-11`. Caches produced for RC5 are incompatible and must be rebuilt cleanly.

## Qualification boundary

The candidate must pass an exact-revision, four-target, thirteen-stage release
gate. The tag workflow may create only a complete draft. Required reports,
archive digests, and clean installed first-hour replay are defined in
[`1_0_rc6_release_plan.md`](1_0_rc6_release_plan.md). Public pre-release
promotion remains a separate, protected maintainer action after the replay
receipt is recorded.

After `v1.0.0-rc.6` is published, install it explicitly rather than relying on
GitHub's stable-release selector:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.6
```
