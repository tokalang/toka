# Official Package Repository Migration v1

Status: `Approved extraction policy; regex, router, openai_compat, compress, sqlite, postgres, unicode, gui, and redis migrations complete`

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
second `0.1.0` release. An incubating root with no prior public monorepo
release may instead make its first standalone public release `0.1.0`.

`official/regex` completed this cutover on 2026-08-10. Its successor source is
[`tokalang/regex`](https://github.com/tokalang/regex), whose first standalone
release is [`v0.1.1`](https://github.com/tokalang/regex/releases/tag/v0.1.1).
The original monorepo tag, release asset, and catalog entry remain the
immutable source record for `0.1.0`.

`official/router` completed the same cutover on 2026-08-11. Its successor
source is [`tokalang/router`](https://github.com/tokalang/router), whose first
public release is [`v0.1.0`](https://github.com/tokalang/router/releases/tag/v0.1.0).
It had no prior public monorepo release, so this is its first source and
release record.

`official/openai_compat` completed the same cutover on 2026-08-11. Its
successor source is [`tokalang/openai_compat`](https://github.com/tokalang/openai_compat),
whose first registry-eligible release is
[`v0.1.1`](https://github.com/tokalang/openai_compat/releases/tag/v0.1.1).
The preceding `v0.1.0` source snapshot is deliberately absent from the catalog:
its archive did not use the required `toka publish` package-root layout.

`official/compress` completed the same cutover on 2026-08-13. Its canonical
source is [`tokalang/compress`](https://github.com/tokalang/compress), whose
first public release is
[`v0.1.0`](https://github.com/tokalang/compress/releases/tag/v0.1.0). It had no
prior public monorepo release, so this is its first source and release record.

`official/sqlite` completed the same cutover on 2026-08-13. Its canonical
source is [`tokalang/sqlite`](https://github.com/tokalang/sqlite), whose first
public release is
[`v0.1.0`](https://github.com/tokalang/sqlite/releases/tag/v0.1.0). It had no
prior public monorepo release, so this is its first source and release record.

`official/postgres` completed the same cutover on 2026-08-13. Its canonical
source is [`tokalang/postgres`](https://github.com/tokalang/postgres), whose
first public release is
[`v0.1.0`](https://github.com/tokalang/postgres/releases/tag/v0.1.0). It had no
prior public monorepo release, so this is its first source and release record.

`official/unicode` completed the same cutover on 2026-08-13. Its canonical
source is [`tokalang/unicode`](https://github.com/tokalang/unicode). The
registry-qualified
[`v0.1.1`](https://github.com/tokalang/unicode/releases/tag/v0.1.1)
maintenance release preserves the `v0.1.0` API, Unicode 17.0.0 data, generated
tables, and corpus while adding the complete license and reproducibility
material required by the package archive.

`official/gui` completed the same cutover on 2026-08-13. Its canonical source
is [`tokalang/gui`](https://github.com/tokalang/gui), whose first public
release is [`v0.1.0`](https://github.com/tokalang/gui/releases/tag/v0.1.0).
The canonical repository also owns the imported editor and settings demos;
release archives deliberately exclude those applications.

`official/redis` completed the same cutover on 2026-08-16. Its canonical source
is [`tokalang/redis`](https://github.com/tokalang/redis), whose first public
release is [`v0.2.0`](https://github.com/tokalang/redis/releases/tag/v0.2.0).

The new repository owns package CI, issues, releases, and its future source
tags. Its first public release is a real SemVer successor, such as `v0.1.1`.
That release receives a GitHub archive and a new immutable catalog version
record that names `tokalang/<name>` as its source. Older locks continue to
resolve their original archive.

## Completion

After the first standalone release passes its package qualification and a
fresh registry consumer replay, delete the incubating package root from
`tokalang/toka`. The compiler repository retains the package contract, while
the public `tokalang/toka-examples` repository owns cross-repository consumer
fixtures. No long-lived mirror, submodule, or dual canonical source is allowed.

`official/regex@0.1.1` passed the standalone Linux/macOS qualification and a
fresh default-registry consumer replay before its historical monorepo root was
removed. [`toka-examples/registry_regex_consumer`](https://github.com/tokalang/toka-examples/tree/main/registry_regex_consumer)
is the retained exact-version consumer fixture.

`official/router@0.1.0` passed standalone Linux and macOS qualification and a
fresh public-registry/offline consumer replay before its monorepo root was
removed. [`toka-examples/registry_router_consumer`](https://github.com/tokalang/toka-examples/tree/main/registry_router_consumer)
is the retained exact-version consumer fixture.

`official/openai_compat@0.1.1` passed standalone Linux and macOS qualification
and a fresh public-registry/offline consumer replay before its monorepo root was
removed. [`toka-examples/registry_openai_compat_consumer`](https://github.com/tokalang/toka-examples/tree/main/registry_openai_compat_consumer)
is the retained exact-version consumer fixture.

`official/compress@0.1.0` passed standalone Linux and macOS qualification and a
fresh public-registry/offline consumer replay before its monorepo root was
removed. [`toka-examples/registry_compress_consumer`](https://github.com/tokalang/toka-examples/tree/main/registry_compress_consumer)
is the retained exact-version consumer fixture.

`official/sqlite@0.1.0` passed standalone qualification and a fresh
public-registry/offline consumer replay before its monorepo root was
removed. [`toka-examples/registry_sqlite_consumer`](https://github.com/tokalang/toka-examples/tree/main/registry_sqlite_consumer)
is the retained exact-version native consumer fixture; the independently
qualified [`service-kit`](https://github.com/tokalang/toka-examples/tree/main/service-kit)
is its retained application-level consumer.

`official/postgres@0.1.0` passed standalone deterministic Linux/macOS
qualification, the PostgreSQL 16/17 TLS/SCRAM tag gate, and a fresh
public-registry/offline replay before its monorepo root was removed.
[`toka-examples/registry_postgres_consumer`](https://github.com/tokalang/toka-examples/tree/main/registry_postgres_consumer)
is the retained exact-version consumer fixture. Toka's data-access and agent
reference services materialize the same immutable lock for composition tests.

`official/unicode@0.1.1` passed standalone qualification and a fresh exact-lock
public-registry/archive-only replay before its monorepo root was removed.
[`toka-examples/registry_unicode_consumer`](https://github.com/tokalang/toka-examples/tree/main/registry_unicode_consumer)
is the retained consumer fixture. Its immutable release archive SHA-256 is
`c68569e6efbd9eb9bf85226eca68de3a0187d4300e320aeb13857be73b5ad28a`.

`official/gui@0.1.0` passed standalone macOS package qualification. Both
canonical-repository demos and
[`toka-examples/registry_gui_consumer`](https://github.com/tokalang/toka-examples/tree/main/registry_gui_consumer)
resolve exact GUI and Unicode locks, rebuild from only those two release
archives, rebuild the Objective-C source, and verify AppKit, Metal, and
QuartzCore linkage. Its immutable release archive SHA-256 is
`b17fb7d4ce6bcb8c3a533216e40ca2c63ab95e0263ceffac8e2636d2f97ce85d`.
These hosted gates do not open a window, establish a usable Metal device, or
qualify interactive input or IME behavior.

`official/redis@0.2.0` passed standalone deterministic Linux/macOS qualification,
the Redis 7.4.x/8.2.x TCP/TLS Docker matrix, and a fresh public-registry/offline
replay before its monorepo root was removed.
[`toka-examples/registry_redis_consumer`](https://github.com/tokalang/toka-examples/tree/main/registry_redis_consumer)
is the retained exact-version consumer fixture. Its immutable release archive SHA-256 is
`3d397581317dbab1fe8fb3acb7f85fc0c30be3c9526e1593327bf4e34df61e1f`. Toka's data-access
and agent reference services materialize the same immutable lock for composition tests.
