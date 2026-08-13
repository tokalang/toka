# Toka v1.0.0-rc.5 Candidate Notes

**Status:** Preparation only. `v1.0.0-rc.5` has no selected candidate SHA and
is not tagged, drafted, or published. These notes describe the proposed
candidate source; they do not claim qualification.

## Purpose

RC5 is the next proposed 1.0 release candidate from Toka's frozen language
surface. It recovers compiler and SDK regressions found after RC4 and makes
the compiler-interface cache boundary explicit. It adds no new source-syntax
or PAL guarantee.

## Candidate improvements

- Atomic intrinsics preserve valid ordering operands through lowering and
  diagnose orderings that are invalid for the selected operation.
- Generic ownership, borrowing, allocation, and dynamic-interface CodeGen
  paths recover their intended behavior, with regression coverage for empty
  and generic dynamic vtables.
- Toolchain trust is limited to configured SDK roots instead of being inferred
  from a module name alone.
- Generated diagnostics are registered consistently, and the diagnostic
  generator has an idempotence check.
- SDK build and test paths handle direct OpenSSL linkage, UTF-8 tool output,
  generated package inputs, and Windows environment-string ownership.
- Interface-cache conformance is part of the release gate. `.tki` metadata is
  format 2 and the compiler-interface compatibility key is `0.9.9-10`.
  Caches produced for RC4 are incompatible and must be rebuilt rather than
  copied into RC5 qualification or archive replay.

## Qualification boundary

The candidate must pass an exact-revision, four-target, thirteen-stage release
gate. The tag workflow may create only a complete draft. Required reports,
archive digests, and clean installed first-hour replay are defined in
[`1_0_rc5_release_plan.md`](1_0_rc5_release_plan.md). Public pre-release
promotion remains a separate, protected maintainer action after the replay
receipt is recorded.

If RC5 is eventually published, install it explicitly rather than relying on
GitHub's stable-release selector:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.5
```
