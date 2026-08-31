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
| `sqlite` | `0.1.1` | Pass | `0.1.2` RC9 candidate passes at `98ff310` |
| `regex` | `0.2.0` | Fail: removed `Vec::get_ref` | `0.2.1` RC9 candidate passes at `e5743ab` |
| `router` | `0.1.0` | Fail: removed `Vec::get_ref` | `0.1.1` RC9 candidate passes at `879e2b7` |
| `unicode` | `0.1.1` | Fail: `E0455` error-return dependency | `0.1.2` RC9 candidate passes at `1e541b1` |
| `compress` | `0.1.0` | Fail: removed `Vec::get_ref` | `0.1.1` RC9 candidate passes at `e4b9d1d` |
| `openai_compat` | `0.1.2` | Fail: removed `Vec::get_ref` | Compile migration present; runtime double-free remains |
| `redis` | `0.2.0` | Fail: removed Vec/Mutex `get_ref` and raw-null contract | Compile migration present; pool runtime double-free remains |
| `postgres` | `0.1.0` | Fail: removed `get_ref`, raw-null contract, and cede-forwarding mismatch | `0.1.1` RC9 candidate passes at `b13d5159` |
| `gui` | `0.1.0` | Fail through locked `unicode@0.1.1` | `0.1.1` with `unicode@0.1.2` passes at `d17855b` against a candidate-only registry |

Passing means the repository's own deterministic package qualification used
the installed-toolchain `TOKA`/`TOKAC`/`TOKA_LIB` path from the exact RC9
archive, including its local package consumer and offline lock replay.

## Qualified release candidates

The following candidate archives were generated twice with the RC10
deterministic publisher and remained byte-identical. Unicode and GUI use their
existing package-specific deterministic builders. These identities are local
release evidence only: no branch, tag, archive, or catalog record has been
published.

| Package candidate | Archive SHA-256 | Installed-tree SHA-256 |
| --- | --- | --- |
| `regex@0.2.1` | `b28bb8ae499dc6587ceff5e172818b9aa8d94780412575574756dd2428a019cc` | `c0825c314e78e953e459927832448fcbff9f4551f37915d7fbd170e3308d9978` |
| `router@0.1.1` | `a1427631205a5ee77c21712fc32c167f40639b23b3d544801051d31865cca4d8` | `e8454132bb6121f629528003ca2d108dcc3038e0bb8dd8fc9af75f0c987e8240` |
| `unicode@0.1.2` | `e825d8e6a8ebb8d059b02bf35628d2d6fdc1449306225b0245a8169dfdb3f81b` | `ecec5d41a2d3604f2e2b5068a5bb5a4672e0ddf23cc1de3adb74386b570bd62d` |
| `compress@0.1.1` | `4fb588c99c4e41845333f2f4e197be40c2afb598ea957d4debc8f1848265592c` | `53ccdf1e68f09b0ef2bbea94fc1ed40b45f4d17a5942ea8b4f37b3403dc640f6` |
| `postgres@0.1.1` | `01a80afc319b142d2d4ec833b40b8d8a1389ccb3c0b69901bd0b3ce26a521903` | `5efc849c7637c041269a43a767ca5fbde1ef9bd1b4c4a26d028b94e060e9ca85` |
| `sqlite@0.1.2` | `99b55ed3eb8f4fc8ea6c8df54cc5188766c02a86d8fd8860569a9dd6230652a5` | `356d4671b872ac40594fc09352b75421598584802574ed3a9d3b15346d69eb42` |
| `gui@0.1.1` | `2cfe2671dfef006802dbf784f4c212e79702ab6bcae8461025bc7798e82411cc` | `7212880c07f71ae076979611aab2fe5df21c48a8f4e6164c4cc084085bdf3915` |

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
for a rejected choice, consume that error, then decode and consume a tool-call
event. The first call alone, the second call alone, and a compiler-only nested
`Result<Vec<Resource>, Failure>` control all pass. `MallocScribble=1` makes the
failure deterministic and stops in the final `OpenAiCompatError` string drop,
showing that the returned error's owned storage was invalidated earlier. The
package-reduced fixture remains authoritative until a package-free case
preserves the failure.

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

### `ECO-RC9-P01`: non-deterministic `toka publish` archives

RC9 `toka publish` shells out to host `tar -czf`; identical source produced
different gzip/archive bytes across invocations. RC10 commit `9cb79059` routes
the same package allowlist through `toka_package.py build-archive`, with sorted
members, normalized modes, zero mtime and uid/gid, empty owner names, atomic
replacement, and symlink rejection. The supply-chain qualification changes a
source mtime between two publishes and requires byte equality. This fix changes
packaging only; it does not affect registry resolution or package semantics.

## Required release work

1. Close `ECO-RC9-C01` and `ECO-RC9-C02` without weakening ownership checks.
2. Re-run every repaired package on Linux x64 using the exact RC9 archive; the
   macOS arm64 package and archive-only gates are complete for seven candidates.
3. Rebuild the seven candidate archives from their final tagged commits and
   require the recorded deterministic identities.
4. Append new registry records without modifying any existing version.
5. Update all retained `toka-examples` consumers to the new exact locks and
   repeat online-to-archive-only offline replay.
6. Run GUI with the patched Unicode release, plus Redis 7.4/8.2 and PostgreSQL
   16/17 real-service gates.
7. Keep all seven qualified package CI definitions pinned to exact RC9 archive
   identities; OpenAI Compat and Redis move only after their runtime gates pass.

## Stop condition

RC10 scope may freeze only when all nine package rows and all retained registry
consumers pass, the two runtime blockers are closed or explicitly block RC10,
and no new package feature or public API expansion has entered the work. RC10
then contains only adoption-proven compiler/SDK fixes and the warning-free SDK
boundary; Async/TCB, new ownership syntax, and unrelated package breadth remain
out of scope.
