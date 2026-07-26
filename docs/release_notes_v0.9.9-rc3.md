# Toka Release Candidate v0.9.9-rc3 Release Notes

`v0.9.9-rc3` is the final qualification candidate reviewed before a Toka 1.0 release-tag decision. It is a dry-run candidate: no `v0.9.9-rc3` Git tag or GitHub Release was created.

## Highlights

- **Native async entry**: `async main` is lowered through a native process entry wrapper, rather than returning an unstarted task handle.
- **TLS stream ownership**: HTTP awaits explicitly transfer `AsyncStream` ownership and released TLS handles are cleared, eliminating the previously flaky HTTPS/WSS close path.
- **Package identity**: release builds inject the candidate version into `tokac`; `toka --version` reports the sibling compiler version, so packaged tools agree with the archive label.
- **Raw string literals**: the language includes raw-string lexical support with positive and malformed-input conformance coverage.
- **Optional TLS backend**: plaintext builds remain supported without OpenSSL; TLS/HTTPS/WSS require the OpenSSL backend.

## Qualification Evidence

- [RC3 Release Gate 30189209349](https://github.com/tokalang/toka/actions/runs/30189209349): passed on Linux x64, Linux arm64, macOS x64, and macOS arm64.
- Every Tier 1 job passed the 13-stage release gate, including package smoke.
- Audited source revision: `d0b15ae1`; the workflow head `60bb7ed0` adds release-review metadata only.
