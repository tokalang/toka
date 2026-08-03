# Toka 1.0 Release Review Report

**Revision Under Audit**: `01e6e88be4aef4593fd630355c9763755ed05bd4` (Pre-release Tag: `v0.9.9-rc4`)
**Status**: Recorded-revision Tier 1 gate passed; GitHub pre-release published
**Date**: 2026-07-27

**Authority:** This report qualifies only the revision named above. It is
historical evidence, not current-HEAD qualification. Current blockers and the
active P-1 gate are tracked in
[`semantic_contract_evolution_roadmap_rfc.md`](semantic_core/semantic_contract_evolution_roadmap_rfc.md).

---

## 1. Review Executive Summary

This document records the official Release Review for the `v0.9.9-rc4`
pre-release. The audited revision includes the GAP-LANG-04 handle identity /
payload write separation closure and its release qualification. Its Tier 1
gate completed successfully before the annotated tag was created; the tag
resolves exactly to the audited revision. This is a pre-release, not a final
`v1.0.0` freeze tag.

---

## 2. Target Platform Matrix

| Platform | Architecture | OS / Runtime | Qualification Profile | Release Gate Status |
|---|---|---|---|---|
| **Linux x86_64** | `x86_64` | Ubuntu 22.04 / glibc | Tier 1 Primary Release | [PASSED](https://github.com/tokalang/toka/actions/runs/30246461701/job/89914541403) |
| **Linux aarch64** | `aarch64` | Ubuntu 24.04 / glibc | Tier 1 Primary Release | [PASSED](https://github.com/tokalang/toka/actions/runs/30246461701/job/89914541452) |
| **macOS ARM64** | `arm64` | macOS 15 (Apple Silicon) | Tier 1 Primary Release | [PASSED](https://github.com/tokalang/toka/actions/runs/30246461701/job/89914541621) |
| **macOS x86_64** | `x86_64` | macOS 15 Intel | Tier 1 Primary Release | [PASSED](https://github.com/tokalang/toka/actions/runs/30246461701/job/89914541459) |
| **Windows x86_64** | `x86_64` | Windows Native (MSYS2) | Tier 2 Secondary Release | Non-blocking Profile |

---

## 3. Pre-Release Verification Evidence

### Historical Local Test Matrix (RC3 macOS ARM64 Baseline)
- **Full Pass Suite**: 358/358 PASSED (`python3 tools/scripts/test_pass.py`)
- **Toka 1.0 Conformance Test Suite**: 14/14 PASSED (`python3 tools/run_conformance.py`)
- **Source-Less `.tki` Semantic Replay Suite**: 20/20 PASSED (`tools/scripts/test_semantic_replay.sh`)
- **Plaintext No-OpenSSL Script**: 3/3 PASSED (`tools/test_no_openssl.sh`)
- **Runtime AddressSanitizer Smoke**: PASSED (`tools/build_sanitized.sh runtime-asan`)
- **Release Notes Document**: [release_notes_v0.9.9-rc3.md](release_notes_v0.9.9-rc3.md)
- **Code Hygiene Check**: CLEAN (`git diff --check`, 0 whitespace errors)

### Tier 1 RC4 Evidence

- **Gate Run**: [30246461701](https://github.com/tokalang/toka/actions/runs/30246461701), `success`.
- **Source under audit**: `01e6e88be4aef4593fd630355c9763755ed05bd4`, clean on every target.
- **Result**: all four Tier 1 jobs passed the complete 13-stage unified release gate (`build`, `pass`, `fail`, `warn`, `semantic_replay`, `cache_invalidation`, `tooling`, `incremental`, `native_build_reference`, `qslite`, `async`, `sanitizer`, and `package_smoke`).
- **Artifact checks**: each target reports `source_dirty=false`; the pass/fail/replay stages report 346/346 passing positive cases, 257/257 passing expected-failure cases, and 25/25 semantic replay cases. The tooling stage completed 56 checks and the sanitizer stage completed 82 mutations.
- **Published pre-release**: [`v0.9.9-rc4`](https://github.com/tokalang/toka/releases/tag/v0.9.9-rc4). Its annotated tag resolves to the audited source revision above. The tag was published only after qualification; the Release Gate workflow was temporarily disabled during the tag push and immediately re-enabled, so no duplicate release job was started.

---

## 4. Multi-Platform Release Gate Procedure

1. **Push Main Branch**: Push verified commits to `main`.
2. **Manual Workflow Dispatch**: Trigger `.github/workflows/release.yml` with the intended candidate label and `publish_release=false` via GitHub Actions workflow_dispatch.
3. **Artifact Collection**: Verify all 13 release gate stages (`build`, `pass`, `fail`, `warn`, `semantic_replay`, `cache_invalidation`, `tooling`, `incremental`, `native_build_reference`, `qslite`, `async`, `sanitizer`, `package_smoke`) pass cleanly across all Tier 1 targets.
4. **Pre-release Closure**: Complete for RC4. A final `v1.0.0` tag remains an explicit maintainer decision.

### Historical RC3 Evidence

The prior [RC3 gate run 30189209349](https://github.com/tokalang/toka/actions/runs/30189209349) remains valid historical evidence for `d0b15ae1`, but RC4 is the release-qualified baseline after GAP-LANG-04.
