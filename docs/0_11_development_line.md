# The 0.11.x development line

**Status on 2026-10-01: v0.11.0 published and selected as GitHub Latest.**
Install [published v0.11.0](https://github.com/tokalang/toka/releases/tag/v0.11.0)
for a pinned public SDK. Its annotated tag directly names
`57b0f7dd7d52bdc24c6dde0457803240e5c62e8a`. The `v0.10.0` tag and its assets
retain their original source and bytes. The
[0.10 release record](0_10_development_line.md) remains historical evidence.

The `0.11.0` release integrates the accepted unsafe-container package
from `9b99e4abe7ba1af03aa10d166263b44ab7395737` with main's published-status
documentation. Public Vec storage and raw-operation contracts change as
described in the [migration guide](migration_0_11_vec.md). Owning nested Vec
operations use the formal standard library, and values carrying external
borrows retain those sources across supported calls, assignments and exits.
See [release notes](release_notes_v0.11.0.md) for scope and limitations.

Major version zero continues to describe evolving language and library
contracts. The release classification on GitHub does not imply 1.0 API
stability. This version step does not add E elision, arbitrary parameter
poststates or a general raw-storage proof system.

## Published evidence

- [Four-target qualification 36844946549](https://github.com/tokalang/toka/actions/runs/36844946549),
  attempt 1, passed for the exact SDK candidate and `v0.11.0` label.
- [Draft creation 36883909484](https://github.com/tokalang/toka/actions/runs/36883909484)
  reused the four original archives and verified their downloaded draft bytes.
- [macOS x64 SDK replay 36885059618](https://github.com/tokalang/toka/actions/runs/36885059618)
  passed against the original qualified archive.
- [Protected promotion 36885564471](https://github.com/tokalang/toka/actions/runs/36885564471)
  verified the tag, qualification, replay and archive digests, then published
  the release and selected it as Latest.

No SDK archive was rebuilt or recompressed between qualification and
publication. Release-control maintenance on `main` is separate from the
immutable SDK candidate; its revision is not the release tag's target.

## Qualification and release boundary

1. Fix the integration SHA and validate default/override version identity,
   compiler and SDK tests, and the existing 13-stage release gate.
2. A branch/SHA qualification dispatch accepts canonical `v0.11.N` labels,
   validates four native targets, and uploads reports and candidate archives
   without creating a GitHub Release. Old-line, malformed and mismatched
   labels are rejected by the active flow.
3. The [qualified-candidate draft entry](qualified_candidate_draft.md) can reuse
   those original candidate archives without rebuilding. Its release-control
   revision is separate from the qualified SDK candidate; tag creation and
   draft creation require separate authorization.
4. A separately authorized annotated tag may create an unpublished,
   non-prerelease draft. A protected promotion validates the same candidate,
   qualification source, four archives, checksums and macOS x64 replay before
   public release and an explicit Latest choice.

Local tests are prequalification evidence. A new integration SHA needs its
own hosted four-platform result before release. Published 0.10/RC tags and
assets remain immutable and cannot be relabeled as 0.11 qualification.
