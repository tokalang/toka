# Toka Release Candidate v0.9.9-rc1 Release Notes

Toka `v0.9.9-rc1` is the official pre-release candidate for Toka 1.0 Release Candidate Qualification across Tier 1 platforms.

## Key Changes & Qualification Highlights

- **Unified 1.0 Qualification Framework**: Established manifest-driven Conformance Suite (`tests/conformance/manifest.json`, 14/14 tests) covering syntax, ownership, async local lifetimes, diagnostic error spans, and LLVM SRET lowering.
- **Source-Less TKI Replay Suite**: 20/20 verified module cases (`tools/scripts/test_semantic_replay.sh`) for compilation from `.tki` metadata without source `.tk` files.
- **Plaintext No-OpenSSL Profile**: Decoupled non-TLS socket and stdlib execution (`tools/test_no_openssl.sh`) ensuring core Toka binaries execute cleanly without OpenSSL dependencies.
- **Subprocess & OS Execution Micro-Slice**: Standardized `Command` output capture, child process spawning, and exit status handling (`demos/vertical_slices/03_subprocess_slice.tk`).
- **CI PR & Release Candidate Gate Alignment**: Standardized `.github/workflows/ci.yml` PR gate and `.github/workflows/release.yml` 13-stage release qualification.
