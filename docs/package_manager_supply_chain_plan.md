# Package Manager Supply-Chain Closure

Status: `InProgress`

This document governs the bounded hardening of the existing Toka package
manager. It implements the deterministic and integrity requirements already
stated in `PACKAGE_MANAGER_DESIGN.md`; it does not add language syntax or a
general semantic-version constraint solver.

## Scope

The closure covers:

- structured process execution for Git, HTTP transfer, archive creation, and
  registry operations;
- deterministic `package.lock` generation and strict replay;
- SHA-256 verification for downloaded archives and installed content;
- staging, bounded safe extraction, atomic installation, and failure rollback;
- recursive exact dependency resolution with cycle and conflict detection;
- offline replay from a verified package cache.

Exact registry versions, Git tags/commits, and local paths are supported.
`latest` may be accepted only when registry or Git resolution produces an
immutable version or commit before the lock is written. Version ranges and
backtracking resolution are Post-1.0 package-format work.

## Lock Format

The machine-generated format is UTF-8, line oriented, and deterministic:

```text
toka-lock-v1
package<TAB>alias<TAB>kind<TAB>locator<TAB>resolved<TAB>archive_sha256<TAB>content_sha256<TAB>dependencies
```

Rules:

- entries are sorted by alias;
- fields may not contain tabs, newlines, or NUL bytes; filesystem paths are
  canonicalized before they enter the lock and may not traverse package roots;
- `kind` is `path`, `git`, or `registry`;
- `resolved` is the canonical path, exact Git commit, or exact registry
  version;
- `archive_sha256` is `-` for path/Git sources and mandatory for registry
  archives;
- `content_sha256` is the deterministic installed tree digest;
- dependencies are sorted aliases joined by commas, or `-` when empty;
- the complete file is written to a sibling temporary file and atomically
  renamed over `package.lock`;
- malformed, duplicate, incomplete, or unverifiable entries invalidate the
  lock as a whole.

The format is compiler/tool-version-bound and does not create a cross-version
ABI promise.

## Content Digest

The content digest is SHA-256 over every regular file below the package root,
ordered by normalized relative path. Each record hashes the path byte length,
path bytes, file byte length, and file bytes. Directories carry no payload.
Symbolic links, hard links, devices, sockets, FIFOs, absolute paths, path
traversal, and control characters are rejected.

The `.git` directory, `.toka` work state, and `package.lock` are excluded from
the package content digest. Source files and the child `package.tk` are not.

## Installation Transaction

Registry and Git sources are materialized below a unique staging directory in
`.toka/staging`. A candidate is verified before becoming visible under
`.toka/packages`. Installation uses a same-filesystem atomic rename. Existing
verified installations are reused; mismatched installations are never silently
trusted or overwritten in place.

Archive extraction is delegated to the bundled
`lib/toolchain/toka_safe_extract.py`. It accepts regular files and directories
only, rejects escaping paths and links, and enforces configurable entry,
per-file, and total-size limits before writing any member.

## Resolution

Resolution starts from direct entries in the root `package.tk`. Every fetched
child `package.tk` is parsed with the same restricted dependency parser.

- repeated aliases must resolve to identical kind, locator, and immutable
  revision or resolution fails;
- active-stack repetition is a dependency cycle and fails with the full alias
  chain;
- the flattened lock contains direct and transitive nodes;
- lock replay must not access the network when every required archive/source
  is present and verified in the cache;
- missing or corrupt offline material fails without mutating the installation
  or lockfile.

## Work Ledger

| Stage | Status | Exit evidence |
| --- | --- | --- |
| `PM-0` | `Complete` | Lock schema, digest, transaction, resolver, and stop conditions are frozen in this document |
| `PM-1` | `Complete` | Package-manager subprocesses use structured argv; CLI qualification covers paths containing spaces and error status propagation |
| `PM-2` | `Complete` | Known SHA-256 vectors, deterministic content changes, strict graph validation, and atomic lock replacement pass focused tests |
| `PM-3` | `Complete` | Traversal, links, absolute paths, hierarchy conflicts, corrupt archives, and size violations are rejected before atomic visibility |
| `PM-4` | `Complete` | Direct/transitive resolve, conflict/cycle handling, offline reinstall, cache corruption, rollback, and transactional retirement tests pass |

## Stop Conditions

This direction stops when all `PM-*` stages are complete, the offline
end-to-end fixture produces byte-identical locks on two runs, every declared
failure leaves the previous lock and package installation unchanged, and the
normal compiler/release gates retain their previous results. Registry service
implementation, package signing, semver ranges, binary packages, and ecosystem
hosting are outside this closure.
