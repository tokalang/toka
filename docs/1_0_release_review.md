# Toka 1.0 Release Review Report

**Revision Under Audit**: `d0b15ae1` (Target Release Candidate Tag: `v0.9.9-rc3`)
**Status**: Pre-Release Review Audit (Pending Multi-Platform Artifact Verification)
**Date**: 2026-07-25

---

## 1. Review Executive Summary

This document serves as the official Release Review Report for Toka 1.0 qualification. All normative language specifications, compiler lowering rules, diagnostic maps, and qualification micro-slices have been integrated and verified locally. Transition of core P0/P1 items in [`docs/1_0_gap_ledger.md`](1_0_gap_ledger.md) to `closed` is subject to multi-platform CI verification via release candidate tag `v0.9.9-rc3`.

---

## 2. Target Platform Matrix

| Platform | Architecture | OS / Runtime | Qualification Profile | Release Gate Status |
|---|---|---|---|---|
| **Linux x86_64** | `x86_64` | Ubuntu 22.04 / glibc | Tier 1 Primary Release | Pending `v0.9.9-rc3` CI Run |
| **Linux aarch64** | `aarch64` | Ubuntu 24.04 / glibc | Tier 1 Primary Release | Pending `v0.9.9-rc3` CI Run |
| **macOS ARM64** | `arm64` | macOS 15 (Apple Silicon) | Tier 1 Primary Release | Pending `v0.9.9-rc3` CI Run |
| **macOS x86_64** | `x86_64` | macOS 15 Intel | Tier 1 Primary Release | Pending `v0.9.9-rc3` CI Run |
| **Windows x86_64** | `x86_64` | Windows Native (MSYS2) | Tier 2 Secondary Release | Non-blocking Profile |

---

## 3. Pre-Release Verification Evidence

### Local Test Matrix (macOS ARM64 Baseline)
- **Full Pass Suite**: 358/358 PASSED (`python3 tools/scripts/test_pass.py`)
- **Toka 1.0 Conformance Test Suite**: 14/14 PASSED (`python3 tools/run_conformance.py`)
- **Source-Less `.tki` Semantic Replay Suite**: 20/20 PASSED (`tools/scripts/test_semantic_replay.sh`)
- **Plaintext No-OpenSSL Script**: 3/3 PASSED (`tools/test_no_openssl.sh`)
- **Runtime AddressSanitizer Smoke**: PASSED (`tools/build_sanitized.sh runtime-asan`)
- **Release Notes Document**: [release_notes_v0.9.9-rc1.md](release_notes_v0.9.9-rc1.md)
- **Code Hygiene Check**: CLEAN (`git diff --check`, 0 whitespace errors)

---

## 4. Multi-Platform Release Gate Procedure

1. **Push Main Branch**: Push verified commits to `main`.
2. **Manual Workflow Dispatch / Tag Trigger**: Trigger `.github/workflows/release.yml` with `tag_name=v0.9.9-rc3` and `publish_release=false` via GitHub Actions workflow_dispatch.
3. **Artifact Collection**: Verify all 13 release gate stages (`build`, `pass`, `fail`, `warn`, `semantic_replay`, `cache_invalidation`, `tooling`, `incremental`, `native_build_reference`, `qslite`, `async`, `sanitizer`, `package_smoke`) pass cleanly across all Tier 1 targets.
4. **Final Closure**: Update this document with CI artifact links and transition P0/P1 items in `docs/1_0_gap_ledger.md` from `verified` to `closed`.
