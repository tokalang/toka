# Official Package Repository Migration v1

Status: `Approved extraction policy; official/regex cut over; first standalone release pending`

Official packages are publisher-owned optional libraries, not a fourth
standard-library layer. This document defines the one-way migration from an
incubating `official/<name>/` package root in `tokalang/toka` to its canonical
public repository under the `tokalang` organization.

## Immutable existing releases

Existing package releases remain attached to their original source repository,
source tag, archive URL, and catalog digest forever. In particular,
`official/regex@0.1.0` remains the `tokalang/toka` release tagged
`official-regex-v0.1.0`; its catalog record is not rewritten during migration.

## Extraction gate

Before a package repository is created, its checked-in qualification must run
from a standalone checkout using either a built `TOKA_ROOT` source checkout or
the explicit installed-toolchain triple `TOKA`, `TOKAC`, and `TOKA_LIB`. The
package must also carry its own scope documentation, tests, license, static
manifest, and `AI_CONTRACT`.

## Cutover

The first standalone repository is named `tokalang/<name>` (for example,
`tokalang/regex`). Its history should be derived from the package subtree so
the source provenance remains inspectable. The default branch advances to the
next development version, such as `0.1.1-dev.0`; it must not manufacture a
second `0.1.0` release.

`official/regex` completed this cutover on 2026-08-10. Its successor source is
[`tokalang/regex`](https://github.com/tokalang/regex) on `main` at
`0.1.1-dev.0`; the original monorepo tag, release asset, and catalog entry
remain the immutable source record for `0.1.0`.

The new repository owns package CI, issues, releases, and its future source
tags. Its first public release is a real SemVer successor, such as `v0.1.1`.
That release receives a GitHub archive and a new immutable catalog version
record that names `tokalang/<name>` as its source. Older locks continue to
resolve their original archive.

## Completion

After the first standalone release passes its package qualification and a
fresh registry consumer replay, delete the incubating package root from
`tokalang/toka`. The compiler repository retains only the package contract and
a small cross-repository consumer fixture. No long-lived mirror, submodule, or
dual canonical source is allowed.
