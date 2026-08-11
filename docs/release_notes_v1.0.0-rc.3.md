# Toka v1.0.0-rc.3 Candidate Notes

**Status:** In qualification. `v1.0.0-rc.3` is not tagged or published until
its exact revision passes the four-platform release gate and maintainers
authorize publication.

## Purpose

RC3 is the next public 1.0 release candidate from Toka's frozen language
surface. It makes the developer-facing contracts used by people and AI coding
tools more truthful and installable; it does not add language syntax or change
the `.tki` compatibility format.

## Candidate improvements

- TaskHandle Lifecycle Contract v1 now distinguishes its qualified runtime
  subset from deferred cancellation, TaskScope, and PlaceState work rather
  than promising behavior current runtime and CodeGen do not provide.
- Rejected semantic `toka index`, `toka context`, and `toka query` requests
  now emit one `toka.diagnostics` v2 JSON object, matching the public
  machine-output contract.
- Release archives package the preview helper and prove `toka preview` from a
  clean extraction, and carry the release-matched AI Completion Card v0.2.
  `toka test` now returns a nonzero exit status when its preview scan finds a
  failure.
- Publication waits for all four architecture archives before one aggregate
  job creates the GitHub pre-release.
- The new AI Authoring Friction Baseline v1 documents and tests four recurring
  H/P and borrowed-pattern misunderstandings without treating a syntax
  experiment as a public language commitment.

## Qualification boundary

The candidate must pass an exact-revision, four-target, thirteen-stage release
gate. Required reports, archive digests, and native first-hour replay are
defined in [`1_0_rc3_release_plan.md`](1_0_rc3_release_plan.md). The withdrawn
`v1.0.0-rc.2` tag is historical only and cannot be moved or republished.

When published, install RC3 explicitly rather than relying on GitHub's stable
release selector:

```sh
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.3
```
