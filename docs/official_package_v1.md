# Official Package v1 Contract

Status: `Normative convention; resolver-compatible static registry v1`

This contract defines how an officially maintained package is identified,
documented, tested, and released. It extends the existing `package.tk` /
`package.lock` protocol; it does **not** introduce TOML, JSON, version ranges,
or a second resolver.

The authoritative resolver and supply-chain rules remain
[`PACKAGE_MANAGER_DESIGN.md`](PACKAGE_MANAGER_DESIGN.md) and
[`package_manager_supply_chain_plan.md`](package_manager_supply_chain_plan.md).
Where they conflict with this document, the resolver and supply-chain rules win.

## 1. Placement and dependency direction

Official packages are separate repositories or package roots. They are not
added to `core`, `std`, or `stdx` merely because they are maintained by Toka.

An official package may depend on `core`, `std`, `stdx`, and other locked
packages. It must not require unexported compiler or standard-library internals
and must not make a standard-library module depend on it. Native dependencies
must be declared as package-level platform requirements, never silently loaded
by a pure-Toka API.

`official` is a publisher identity, not a fourth standard-library layer.
`stdx` contains bundled extensions that may be dependencies of other bundled
extensions; an official or community package may depend on `stdx`, but `stdx`
must not depend on an optional package. A capability already required by
bundled `stdx` infrastructure belongs in `stdx`, not in a duplicate
`official/*` namespace.

## 2. Static `package.tk` contract

`package.tk` remains a static Toka data file: no functions, imports, control
flow, or environment-dependent evaluation. The current resolver requires the
existing `name`, `version`, and `dependencies` shape. Official packages add
the following static metadata so humans and tools can make the same decision
from the checked-in source:

```toka
pub const PACKAGE = (
    name = "example",
    identity = "official/example",
    version = "0.1.0",
    kind = "library",
    license = "Apache-2.0",
    compiler = "1.0.0-rc.1",
    entry_modules = ("lib/official/example.tk"),
    targets = ("macos", "linux"),
    dependencies = (),
    native = (required = false)
)

pub const AI_CONTRACT = (
    schema = "toka-official-package-v1",
    summary = "One sentence describing the package's production role.",
    capabilities = ("stable public capability"),
    non_goals = ("explicitly unsupported surface"),
    safety = ("resource/security/platform boundary"),
    qualification = ("documented repeatable test command")
)
```

`name` uses the existing package-name grammar and remains the concise package
name (for example, `regex`). `identity` is the source and registry identity,
such as `official/regex`; it is also the public import path. The source-tree
convention is `official/<name>/`, with entry module
`lib/official/<name>.tk`. The current resolver does not yet accept `/` in
manifest names, so the identity must not be placed in `name` prematurely.
`version` is an exact SemVer value;
the lockfile, not a version range, resolves an installation. `compiler` is the
exact Toka release against which the package was qualified, not a hidden
compatibility range or promise.

The resolver reads `dependencies`; the native build planner additionally
consumes `native` according to the contract below. Other descriptive fields
remain static package metadata and are safely ignored by resolution.

### Native source packages

`native.required = true` opts one package into the native build path. One
static manifest selects a target block; its `sources` must be regular relative
`native/*.c` files (or macOS-only `.m` files), `pkg_config` names logical
libraries, and `frameworks` name validated macOS system frameworks, for
example:

```toka
native = (
    required = true,
    macos = (
        sources = ("native/bridge.m"),
        frameworks = ("AppKit")
    ),
    linux = (pkg_config = ("libpq",)),
    windows = (system_libraries = ("ws2_32",))
)
```

`toka build` reads this metadata only from roots verified against the current
`package.lock`. It compiles the declared C or Objective-C sources into the
consumer's private `.toka/build/native/` directory and obtains library
compiler/linker inputs through `pkg-config`. Framework names are emitted only
as the macOS linker pair `-framework <validated-name>`; v1 accepts only `-L`
and `-l` linker output from `pkg-config`. `system_libraries` supplies
validated logical linker libraries for the selected Windows target. A package
manifest is never interpreted as raw compiler or shell arguments. See
[`native_dependency_contract_v1.md`](native_dependency_contract_v1.md) for the
complete target-selection and cache-identity contract, and
[`ffi_resource_metadata_v1.md`](ffi_resource_metadata_v1.md) for optional
opaque-handle facts.
`toka publish` includes a package's `native/` directory when present, so the
same locked source that was qualified is available to a registry consumer.
The resulting native objects and libraries are linked only into a consumer
that locks the native package. A pure-Toka project, or one that locks only
`native.required = false` metadata, has no native package compiler or linker
requirement.

This is intentionally a narrow source-build contract. Prebuilt artifacts,
C++, Objective-C++, custom linker scripts, arbitrary flag injection, and
Windows support are not part of v1.

## 3. Public contract and AI-readable evidence

Every official package must contain:

- an explicit entry-module list and a stable public-import example. The
  current resolver maps `import official/name` to
  `lib/official/name.tk`; directory `mod.tk` discovery is not an implicit
  package-entry convention;
- a concise v1 scope document naming supported behavior and non-goals;
- structured errors when parsing, validation, or platform selection can fail;
- one focused qualification test command and any required platform/native
  preconditions;
- an `AI_CONTRACT` whose capabilities, limits, safety boundary, and
  qualification command agree with the prose documentation.

The metadata is a contract, not marketing. A capability absent from
`AI_CONTRACT` or the scope document is not an implied support promise.

## 4. Release and compatibility

Public API changes follow SemVer: patch releases fix behavior without changing
the public contract; minor releases add backward-compatible public surface;
major releases may break it. A release records its exact `package.lock` inputs,
license, qualification evidence, and platform/native support matrix. A package
root inside this monorepo uses the unambiguous source tag formed by replacing
`/` in its identity with `-` and appending `-v<version>` (for example,
`official-regex-v0.1.0`), and the archive name `<name>-<version>.tar.gz`. The
catalog's `source.tag`, `tarball_url`, and SHA-256 must all describe that exact
source release; root compiler tags are not package-release tags.

Package installation and integrity continue to use the existing deterministic
lock, content digest, staging, and atomic-install rules. This contract does not
weaken offline replay or supply-chain verification.

## 5. v1 stop boundary

This contract standardizes only a static, reviewable registry index: a tagged
GitHub Release archive, its SHA-256 digest, and the exact catalog entry that
names them. It does not standardize a public upload service, package signing,
binary distribution, SemVer range solving, or a general machine-readable
schema parser. `official/regex` should be the first pilot only after this
fixture remains resolver-compatible and its package-specific API contract is
reviewed.
