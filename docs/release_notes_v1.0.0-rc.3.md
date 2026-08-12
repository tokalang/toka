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

- Historical TaskHandle Lifecycle Contract v1 is preserved as released, with
  its invalid completion-subscription evidence explicitly withdrawn. New v2
  distinguishes the current qualified runtime subset from deferred
  cancellation, TaskScope, and PlaceState work and binds evidence to an exact
  candidate revision.
- Rejected semantic `toka index`, `toka context`, and `toka query` requests,
  plus a valid unknown-code `toka explain --json` request,
  now emit one structured JSON object, matching the public machine-output
  contract. Semantic compiler rejection uses `toka.diagnostics` v2; an unknown
  explanation code uses `toka.command-error` v1.
- Release archives package the preview helper and prove `toka preview` from a
  clean extraction, and carry the release-matched AI Completion Card v0.2.
  `toka test` now returns a nonzero exit status when its preview scan finds a
  failure.
- Qualification, draft asset creation, and public promotion are separate
  authorization steps. Tags create only a complete draft with four verified
  archives and checksums; a protected workflow publishes only after clean
  first-hour replay has been recorded.
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
