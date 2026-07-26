# Toka 1.0 Release Review Report

**Revision Under Audit**: `d0b15ae1` (Target Release Candidate Tag: `v0.9.9-rc3`)
**Status**: Tier 1 Release Candidate Gate Passed (Final Tag Decision Pending)
**Date**: 2026-07-25

---

## 1. Review Executive Summary

This document serves as the official Release Review Report for Toka 1.0 qualification. All normative language specifications, compiler lowering rules, diagnostic maps, and qualification micro-slices have been integrated and verified locally. The Tier 1 multi-platform qualification for the `v0.9.9-rc3` candidate has passed; the corresponding P0/P1 entries in [`docs/1_0_gap_ledger.md`](1_0_gap_ledger.md) are closed. No release tag or GitHub Release has been created by this review.

---

## 2. Target Platform Matrix

| Platform | Architecture | OS / Runtime | Qualification Profile | Release Gate Status |
|---|---|---|---|---|
| **Linux x86_64** | `x86_64` | Ubuntu 22.04 / glibc | Tier 1 Primary Release | [PASSED](https://github.com/tokalang/toka/actions/runs/30189209349/job/89759184095) |
| **Linux aarch64** | `aarch64` | Ubuntu 24.04 / glibc | Tier 1 Primary Release | [PASSED](https://github.com/tokalang/toka/actions/runs/30189209349/job/89759184109) |
| **macOS ARM64** | `arm64` | macOS 15 (Apple Silicon) | Tier 1 Primary Release | [PASSED](https://github.com/tokalang/toka/actions/runs/30189209349/job/89759184054) |
| **macOS x86_64** | `x86_64` | macOS 15 Intel | Tier 1 Primary Release | [PASSED](https://github.com/tokalang/toka/actions/runs/30189209349/job/89759184082) |
| **Windows x86_64** | `x86_64` | Windows Native (MSYS2) | Tier 2 Secondary Release | Non-blocking Profile |

---

## 3. Pre-Release Verification Evidence

### Local Test Matrix (macOS ARM64 Baseline)
- **Full Pass Suite**: 358/358 PASSED (`python3 tools/scripts/test_pass.py`)
- **Toka 1.0 Conformance Test Suite**: 14/14 PASSED (`python3 tools/run_conformance.py`)
- **Source-Less `.tki` Semantic Replay Suite**: 20/20 PASSED (`tools/scripts/test_semantic_replay.sh`)
- **Plaintext No-OpenSSL Script**: 3/3 PASSED (`tools/test_no_openssl.sh`)
- **Runtime AddressSanitizer Smoke**: PASSED (`tools/build_sanitized.sh runtime-asan`)
- **Release Notes Document**: [release_notes_v0.9.9-rc3.md](release_notes_v0.9.9-rc3.md)
- **Code Hygiene Check**: CLEAN (`git diff --check`, 0 whitespace errors)

### Tier 1 RC3 Evidence

- **Gate Run**: [30189209349](https://github.com/tokalang/toka/actions/runs/30189209349), `success`.
- **Source under audit**: `d0b15ae1`; the workflow head `60bb7ed0` adds only this review metadata.
- **Result**: all four Tier 1 jobs passed the complete 13-stage unified release gate (`build`, `pass`, `fail`, `warn`, `semantic_replay`, `cache_invalidation`, `tooling`, `incremental`, `native_build_reference`, `qslite`, `async`, `sanitizer`, and `package_smoke`).

---

## 4. Multi-Platform Release Gate Procedure

1. **Push Main Branch**: Push verified commits to `main`.
2. **Manual Workflow Dispatch / Tag Trigger**: Trigger `.github/workflows/release.yml` with `tag_name=v0.9.9-rc3` and `publish_release=false` via GitHub Actions workflow_dispatch.
3. **Artifact Collection**: Verify all 13 release gate stages (`build`, `pass`, `fail`, `warn`, `semantic_replay`, `cache_invalidation`, `tooling`, `incremental`, `native_build_reference`, `qslite`, `async`, `sanitizer`, `package_smoke`) pass cleanly across all Tier 1 targets.
4. **Final Closure**: Complete for RC3. A final release tag remains an explicit maintainer decision.
