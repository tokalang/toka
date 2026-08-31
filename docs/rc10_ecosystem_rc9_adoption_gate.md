# RC10 Official Ecosystem RC9 Adoption Gate

**Status:** In progress. This ledger records qualification against the exact
published `v1.0.0-rc.9` SDK. It does not authorize RC10, change language
semantics, or rewrite an immutable registry record.

## Boundary

- Candidate toolchain: `v1.0.0-rc.9` /
  `7a02b373d57656ea3a6bc924e0db201c190f4607`.
- Compiler-interface key: `0.9.9-15`; RC8 `.tki`, object, and semantic-cache
  artifacts are prohibited inputs.
- macOS arm64 qualification uses the public archive with SHA-256
  `d13622ffad63db32479db78095bf66b96b4976c0d15aa6c30257e676d66f64ac`.
- Existing registry versions and archive digests are immutable. A package that
  needs source changes receives a new patch version; a package that passes
  unchanged receives evidence only.
- `official/mysql@0.1.0` is absent from the current catalog after its explicit
  withdrawal. Its stale example consumer is outside this nine-package gate.

## Exact published-package baseline

| Package | Catalog version | Exact-tag result on RC9 | Current repair result |
| --- | --- | --- | --- |
| `sqlite` | `0.1.1` | Pass | No package source change required |
| `regex` | `0.2.0` | Fail: removed `Vec::get_ref` | Pass on `feat/rc9-compat` at `8f69f5e` |
| `router` | `0.1.0` | Fail: removed `Vec::get_ref` | Pass on `feat/rc9-compat` at `07e1ebf` |
| `unicode` | `0.1.1` | Fail: `E0455` error-return dependency | Pass on `feat/rc9-compat` at `edf0a23` |
| `compress` | `0.1.0` | Fail: removed `Vec::get_ref` | Pass on `feat/rc9-compat` at `fbb9c65` |
| `openai_compat` | `0.1.2` | Fail: removed `Vec::get_ref` | Compile migration present; runtime double-free remains |
| `redis` | `0.2.0` | Fail: removed Vec/Mutex `get_ref` and raw-null contract | Compile migration present; pool runtime double-free remains |
| `postgres` | `0.1.0` | Fail: removed `get_ref`, raw-null contract, and cede-forwarding mismatch | Pass on `feat/rc9-compat` at `c86dd660` |
| `gui` | `0.1.0` | Fail through locked `unicode@0.1.1` | Direct `get_ref` migration present; full gate waits for patched Unicode |

Passing means the repository's own deterministic package qualification used
the installed-toolchain `TOKA`/`TOKAC`/`TOKA_LIB` path from the exact RC9
archive, including its local package consumer and offline lock replay.

## Public registry-consumer baseline

Each retained consumer was copied to a clean temporary directory, resolved
online from `https://pkg.tokalang.dev`, reduced to only its cached release
archives, then fetched and built with `TOKA_OFFLINE=1`. `package.lock` had to
remain byte-identical.

| Consumer lock | Result |
| --- | --- |
| `regex@0.1.1` | Pass; consumer is stale relative to catalog `0.2.0` |
| `sqlite@0.1.1` | Pass |
| `router@0.1.0` | Fail: removed `get_ref` |
| `unicode@0.1.1` | Fail: `E0455` |
| `openai_compat@0.1.2` | Fail: removed `get_ref` |
| `compress@0.1.0` | Fail: removed `get_ref` |
| `postgres@0.1.0` | Fail: removed `get_ref` and cede-forwarding contract |
| `redis@0.2.0` | Fail: removed Vec/Mutex `get_ref` |
| `gui@0.1.0 + unicode@0.1.1` | Fail through Unicode `E0455` |

The two passing consumers prove that registry resolution, digest validation,
archive-only replay, and RC9 package builds are operational. The failures are
package-version failures, not a catalog transport failure.

## RC10 blockers discovered by adoption

### `ECO-RC9-C01`: owning-result cleanup in OpenAI Compat

The package compiles after replacing `get_ref`, but its full qualification
aborts with a double free. A reduced sequence is: decode an owning error result
for a rejected choice, then decode and consume a tool-call event. A single tool
decode and a generic `Result<Vec<Resource>, Failure>` control both pass. The
package fixture remains the authoritative reproducer until a smaller compiler-
only case preserves the failure.

### `ECO-RC9-C02`: async pool cleanup in Redis

`tests/pool_v1.tk` compiles after the API and nullable-raw migration, then
aborts with a string double free in `reuse_client_async.resume`. The failure
survives explicit Result error-pattern consumption and is therefore not
accepted as a test-spelling issue. PostgreSQL's analogous pool fixture passes,
which bounds the investigation to Redis lease/pool cleanup composition.

### `ECO-RC9-W01`: SDK and package warning noise

The RC9 SDK's `lib/build.tk` and networking modules emit existing `W0408`
mutable-call recommendations during ordinary package builds. Several package
sources and tests add their own `W0408` records. RC10 must distinguish and
remove SDK-origin warnings and should publish no compatibility patch whose own
qualification introduces new warnings.

## Required release work

1. Close `ECO-RC9-C01` and `ECO-RC9-C02` without weakening ownership checks.
2. Re-run every repaired package on macOS arm64 and Linux x64 using exact RC9
   archives.
3. Assign patch versions only after source qualification passes; build release
   archives and record immutable SHA-256 values.
4. Append new registry records without modifying any existing version.
5. Update all retained `toka-examples` consumers to the new exact locks and
   repeat online-to-archive-only offline replay.
6. Run GUI with the patched Unicode release, plus Redis 7.4/8.2 and PostgreSQL
   16/17 real-service gates.
7. Update package CI from RC1/RC4/floating `main` inputs to exact RC9 archive
   identities.

## Stop condition

RC10 scope may freeze only when all nine package rows and all retained registry
consumers pass, the two runtime blockers are closed or explicitly block RC10,
and no new package feature or public API expansion has entered the work. RC10
then contains only adoption-proven compiler/SDK fixes and the warning-free SDK
boundary; Async/TCB, new ownership syntax, and unrelated package breadth remain
out of scope.
