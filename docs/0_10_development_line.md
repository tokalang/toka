# The 0.10.x development line

**Status on 2026-09-27: `0.10.0` is pending, not published.** This document
describes the development route. It does not announce a tag, downloadable SDK
or compatibility qualification for `0.10.0`.

The 1.0 release-candidate cycle has ended. Development continues on `0.10.x`
because foundational language and standard-library contracts are still evolving.
This does not retract any published RC, including `v1.0.0-rc.13`. Their tags,
source, archives and checksums remain historical, reproducible releases.
Version precedence places `0.10.0` below `1.0.0-rc.13`; users and package
resolvers must not infer the development route from SemVer ordering alone.

Major version zero does not promise a stable public API. The project's policy is
to keep compatible changes within `0.10.x` where practical and group planned
breaking changes in `0.11.0`, with explicit migration notes. This is a project
discipline, not a promise that `0.10.x` already has 1.0-level compatibility.
Security checks and the release gate remain unchanged in strength.

The first `0.10.0` candidate carries the accepted concrete-payload call-argument
address repair and the one-shot memory-contract preparation reuse, plus their
tests and release-validation corrections. It does not include a nested-owned-Vec
solution, dependency-contract elision (E), or other new RFC implementation.
The compiler-interface/TKI key and native ABI are not bumped solely for the
version-route change.

## Release boundary

1. A branch/SHA qualification run validates the four supported Linux/macOS
   targets and uploads evidence. It cannot create a GitHub Release.
2. An annotated `v0.10.N` tag may trigger the same four-target gate and create
   an unpublished, non-prerelease draft with the exact archives and checksums.
   The draft is not Latest.
3. A protected manual promotion checks the annotated tag, exact candidate SHA,
   four-target qualification reports, draft state, archive names and checksums,
   and a successful qualified macOS x64 replay receipt bound to the same run
   and archive. Only then may it publish the full release and explicitly mark
   it Latest.

Until that publication, use an exact already-published tag such as
`v1.0.0-rc.13` for a reproducible public install. The unqualified installer
follows GitHub's full-release Latest selector; it does not automatically choose
the newest prerelease or infer this route from version precedence. See the
[`0.10.0` draft release notes](release_notes_v0.10.0.md) for the planned contents.
