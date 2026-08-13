# Toka v1.0.0-rc.4 Candidate Notes

**Status:** Published as a GitHub pre-release at `2026-08-12T12:33:33Z`.
Annotated tag object `ad9499d18cf98bcfd930c7ea7b999191155550db`
points to candidate commit `5c9cb77b5708d2ce598720821da97b683c0edf9f`.
The historical audit does not contain a protected-promotion workflow run or a
clean first-hour replay receipt.

## Purpose

RC4 was the next public 1.0 release candidate from Toka's frozen language
surface. It corrects release evidence and SDK version propagation found while
preparing RC3, and includes bounded standard-library additions. It adds no
source syntax, PAL rule, TKI-format, or CodeGen semantic change.

## Candidate improvements

- The external `race2` cancellation regression now observes child-side-effect
  suppression and wait-registration retirement at the qualified `block_on`
  drain boundary. It no longer asserts the unqualified claim that a canceled
  `race2` parent is already cancel-join-drained at its own terminal state.
- `toka --help` and `tokafmt --version` derive their release label from the
  sibling `tokac`, so all four SDK executables agree for an ordinary build and
  an explicit `TOKA_RELEASE_VERSION_OVERRIDE` build.
- The active release label, diagnostic map, installer guidance, website
  candidate banner, packaging default, and release workflow examples now name
  `v1.0.0-rc.4`.
- `std/process::Command` now has POSIX-only per-child working-directory,
  environment, inherited/null stdio, and explicit non-blocking cancellation
  configuration. `Child::wait_status()` remains the sole reclamation boundary;
  Windows/WASI reject custom configuration rather than silently widening
  authority.
- `stdx/crypto` now includes pure-Toka SHA-512 plus HMAC-SHA-1 and HMAC-SHA-512
  with hexadecimal and constant-time verification helpers, qualified against
  RFC 2202 and RFC 4231 vectors.
- Local RC prequalification can now run an isolated committed checkout through
  the native macOS/Linux gate and Docker Linux ARM64/AMD64 preflights before
  GitHub's authoritative four-target qualification.
- `v1.0.0-rc.3` is retained as an immutable, non-qualified historical tag. Its
  tag gate did not create a draft or public release and it cannot be promoted.

## Qualification boundary

The candidate must pass an exact-revision, four-target, thirteen-stage release
gate. Required reports, archive digests, and native first-hour replay are
defined in [`1_0_rc4_release_plan.md`](1_0_rc4_release_plan.md). A tag may
create only a complete draft; public pre-release promotion remains a separate,
explicit maintainer authorization.

Install the published RC4 explicitly rather than relying on GitHub's stable
release selector:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.4
```
