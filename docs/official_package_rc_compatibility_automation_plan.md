# Official Package RC Compatibility Automation Plan

**Status:** Deferred design record; not implemented and not a current release
gate.

This document records a future automation direction for checking the published
Toka package ecosystem against new compiler release candidates. The current
manual package qualification and Registry publication process remains
authoritative until this plan is explicitly activated.

## Scope

The first implementation is limited to packages published by the `tokalang`
organization and registered as official, installable packages. Community and
third-party packages are intentionally outside this initial scope.

The package set is dynamic. No workflow, script, document, or test may contain
a fixed package count or a hard-coded package-name list as its source of truth.
At the start of each compatibility run, the coordinator must read one exact
`toka-registry` catalog revision and select every package version satisfying the
future explicit official-package predicate, including:

- publisher or ownership scope is `tokalang`;
- the package is marked official;
- the package is installable and not retired;
- the selected version is the current Registry version intended for ordinary
  resolution.

The current catalog schema does not carry all of those facts explicitly. A
future implementation should add structured publisher/trust and lifecycle
fields rather than infer authority from a repository URL. The exact catalog
commit and resulting expected package set must be recorded in each run so a
concurrent Registry update cannot change the meaning of an in-flight result.

## Event model

The intended design is event-driven and has no per-package scheduled polling.
Two events close the lifecycle:

1. **Compiler RC published:** qualify the dynamic official package set from the
   selected Registry revision against the immutable RC SDK.
2. **Official package version registered:** qualify that new version against
   the current published compiler RC before it becomes the ordinary resolved
   version.

The central coordinator maintains a durable run ledger. Dispatch is
idempotent, keyed by the compiler candidate, compiler-interface key, catalog
revision, package identity, package version, and tarball digest. Missing or
failed work is retried explicitly from that ledger; periodic cron jobs are not
the reliability mechanism.

## Future repository responsibilities

### `toka`

- Publish an immutable RC tag, release assets, and `SHA256SUMS`.
- Emit one compatibility event after protected promotion succeeds.
- Provide the compiler release label, candidate SHA, compiler-interface key,
  release URL, and checksum digest.

### `toka-qualification`

- Act as the future central coordinator.
- Resolve and freeze the official package set from an exact Registry commit.
- Dispatch package checks in bounded parallel shards.
- Track pending, verified, incompatible, and infrastructure-failed results.
- Verify evidence provenance before producing a compatibility summary.
- Support manual retry of only missing or failed package checks.

### Official package repositories

- Expose a small, standardized compatibility workflow entry point.
- Test the immutable Registry tarball, not an uncommitted source checkout.
- Verify the requested RC SDK and checksums before executing package code.
- Run online fetch/build/run followed by clean-cache offline replay.
- Run package-specific qualification when the published package contract
  requires it.
- Produce a versioned compatibility attestation without Registry write access.

### `toka-registry`

- Remain the source of truth for the dynamic official package set.
- Preserve immutable package versions, tarball URLs, and digests.
- Store append-only compatibility attestations separately from package
  artifacts.
- Accept one evidence-backed compatibility update after a coordinated run.
- Never let package test jobs write directly to the catalog.

## Compatibility evidence

Compatibility is observed for an exact compiler and package artifact; it must
not be inferred for future RCs. A future attestation should bind at least:

```json
{
  "schema": "toka.official-package-compatibility",
  "version": 1,
  "compiler_release": "v1.0.0-rc.N",
  "compiler_candidate_sha": "<40-hex>",
  "compiler_interface": "<compatibility-key>",
  "registry_revision": "<40-hex>",
  "package_identity": "official/example",
  "package_version": "<semver>",
  "package_sha256": "<64-hex>",
  "targets": ["linux-x64", "macos-arm64"],
  "status": "verified",
  "qualification_run": "https://github.com/.../actions/runs/..."
}
```

The Registry may accumulate multiple verified compiler releases for one
immutable package version. A single successful RC check must not create an
open-ended version range such as `>= rc.N`.

## Version decision rules

- **Existing artifact verifies:** keep the package version and append the new
  compatibility evidence.
- **Source must change:** publish a new package version and immutable archive,
  then register and qualify that version.
- **Public API is unchanged:** an RC-compatibility-only source correction is
  normally a patch release.
- **Public API changes:** use the appropriate SemVer minor or major release.
- **Known incompatible and not yet fixed:** retain the old artifact for older
  compilers, record the incompatibility, and do not present it as verified for
  the new RC.
- **Unknown or infrastructure failure:** do not infer compatibility and do not
  silently convert the result to success.

## Qualification policy

The routine RC compatibility check should be narrower than every package's
full release gate. Its purpose is to prove that the published package artifact
still installs and operates with the new compiler:

- verify RC asset identity and package tarball digest;
- online Registry resolution and fetch;
- build and package-owned smoke execution;
- remove fetched/build state;
- offline fetch/build/run replay;
- platform-specific smoke where the package contract requires it.

Full package qualification, real-service matrices, sanitizers, and package
release workflows run only when a package needs a new version or its contract
explicitly makes those checks part of compatibility.

## Deferred activation criteria

This plan should not become a release requirement until all of the following
exist and have been trialed without changing package resolution behavior:

1. Registry schema support for explicit official-package and publisher facts.
2. A versioned compatibility attestation schema and verifier.
3. Dynamic catalog enumeration bound to an exact Registry revision.
4. Idempotent central dispatch and a durable completion ledger.
5. Standard package-side compatibility entry points.
6. Evidence aggregation that can update the Registry without executing package
   code under Registry write credentials.
7. A warning-only trial across at least one compiler RC cycle.

Until those criteria are met, this document is a planning record only. It does
not change the RC gate, package manifests, Registry schema, package resolver,
or publication policy.
