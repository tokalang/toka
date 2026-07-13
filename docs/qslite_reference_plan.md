# QSLite Reference Application Plan

Status: `Complete`

QSLite is Toka's second sustained reference application for the 1.0 closure.
It is a small embedded, single-file row store rooted at `examples/qslite`.
Its purpose is to expose real compiler, core-library, package, and developer
experience defects through persistent state and recovery. It is not intended
to compete with SQLite or to justify new language syntax.

The work belongs to `FZ-5`. Any change to accepted-program meaning, ownership,
PAL, effects, async, or the frozen 1.0 surface requires a separate owner
decision. QSLite must adapt to the frozen language unless it proves a compiler
correctness defect or a missing operation makes the bounded workload
impossible.

## 1. Frozen Product Boundary

The 1.0 qualification surface is deliberately narrow:

- one database file and one logical table of rows;
- rows have an unsigned integer key and owned UTF-8 text value;
- create/open, insert or replace, get, delete, ordered scan, and close/reopen;
- deterministic on-disk encoding with a version header and per-record
  integrity check;
- malformed, truncated, unsupported-version, and checksum-invalid input
  returns a typed error without exposing partially decoded state;
- a CLI drives scripted workloads, while the storage API remains importable by
  another Toka module.

There is no SQL grammar in the qualification boundary. Joins, indexes,
transactions, concurrency, schema migration, networking, and query
optimization are `Post1.0` application work. The name QSLite describes the
bounded query/storage application; it does not create a compatibility promise
with SQLite files or SQL syntax.

## 2. Required Workloads

The maintained qualification must exercise all of the following:

1. create a database, write multiple rows, replace a row, read and scan;
2. close the process, reopen the same file, and reproduce identical logical
   results;
3. delete rows, compact or rewrite persistent state, reopen, and confirm that
   removed data does not return;
4. reject malformed header, truncation, invalid lengths, invalid UTF-8 where
   applicable, unsupported versions, and checksum mismatch;
5. repeat deterministic mixed operations from a fixed seed and compare the
   final database bytes and logical report across two clean runs;
6. build from source, replay valid same-version `.tki`, perform no-op and
   changed incremental builds, and consume QSLite through a locked local
   package with offline replay.

The workload report records schema version, seed, operation count, reopen
count, row count, database digest, stage results, compiler revision, and
platform. Timing is informative only and is excluded from deterministic
comparison.

## 3. Stage Ledger

Only `Pending`, `InProgress`, `Blocked`, `Complete`, and `Deferred` are valid.

| Stage | Status | Deliverable | Exit evidence |
| --- | --- | --- | --- |
| `QS-0` | `Complete` | Freeze product boundary, finding policy, and stop conditions | This plan and its `FZ-5` index entry |
| `QS-1` | `Complete` | Implement the smallest persistent vertical slice | Public storage API and CLI create, replace, read, scan, delete, rewrite, and reopen one database file |
| `QS-2` | `Complete` | Add bounded corruption and sustained-state qualification | 300 fixed-seed operations, 313 reopens, 10 corruption cases, and deterministic final bytes pass |
| `QS-3` | `Complete` | Qualify compiler, TKI, incremental, package-lock, and offline paths | Source-less TKI execution, first/no-op/recovery incremental builds, locked build, and offline replay pass |
| `QS-4` | `Complete` | Audit findings and stop the direction | Compiler findings have minimized regressions or application-level replay evidence; remaining path ergonomics is deferred |

Each stage is committed independently. Evidence belongs in scripts or tests,
not only in prose. A compiler defect found by QSLite receives a minimized
normal-suite regression before the application workaround is accepted.

## 4. Finding Classification

- Compiler crash, verifier failure, miscompile, resource violation, or
  source/TKI divergence: `FZ-5` blocker.
- Acceptance or ownership ambiguity: stop for owner decision; implementation
  does not invent a rule.
- Missing deterministic error, filesystem primitive, or byte/text operation:
  close within the frozen library contract when narrowly implementable;
  otherwise record an ergonomics blocker.
- Awkward but correct expression: record with concrete call-site evidence;
  it does not automatically justify syntax or semantic expansion.
- Pathological behavior that prevents the bounded workload from completing:
  correctness/reliability blocker. Ordinary optimization remains post-1.0.
- Database features outside section 1: `Deferred`, not evidence that Toka 1.0
  is incomplete.

## 5. Stop Conditions

This direction stops when `QS-1` through `QS-4` are complete and:

- two clean fixed-seed runs are byte-for-byte deterministic;
- every declared failure preserves the prior valid database or fails before
  mutable state becomes visible;
- source, same-version TKI, incremental, lockfile, and offline paths agree;
- the normal release gate retains its result after all compiler/library fixes;
- no open finding is a crash, miscompile, safety issue, semantic ambiguity, or
  required-workload blocker.

Further SQL surface, storage engines, concurrency, performance tuning, or
workload growth requires a new observed defect class or a separate post-1.0
goal. Once QSLite closes, the next 1.0 action is a fresh clean Linux/macOS
x64/arm64 RC matrix; QSLite development must not invalidate that matrix after
it begins.

## 6. Completion Evidence

Run the two replayable qualifications from the repository root:

```console
python3 tools/scripts/qualify_qslite.py
python3 tools/scripts/qualify_qslite_toolchain.py
```

The sustained-state report records 300 operations, 313 process-level reopens,
48 final rows, 10 rejected corruption cases, and database digest
`201029c02d60f133dbed7ef539a12261d6ae2e383b4ae9930eaa4be5ad248001`.
The toolchain report records successful source-less TKI execution,
first/no-op/recovery incremental builds, locked package build, and offline lock
replay. Both reports are deterministic apart from the explicitly recorded
compiler revision.

QSLite exposed the following implementation defects without requiring a
language-design change:

- imported source globals could receive duplicate external definitions in a
  source-less multi-object link; imported definitions now use ODR linkage;
- generic specializations of trusted system declarations could lose their
  declaration provenance and trip the public raw-interface redline; trusted
  provenance now follows the declaration scope, while forged or untrusted TKI
  remains rejected;
- `Vec<Resource>` cloning suppressed required element clones inside the
  container's own `clone` method, and indexed pointer lowering could use the
  container rather than element stride; the narrowed clone guard and element
  addressing have a normal positive-suite regression;
- a raw-pointer rvalue used directly as a member base was spilled and the
  spill address was treated as the pointee; member lowering now consumes the
  returned pointer value directly, covered by the chained `get_ref(0).key`
  assertion in the resource-vector regression.

The qualification canonicalizes temporary paths because macOS may expose the
same directory through `/var` and `/private/var`. This is deferred as tool
ergonomics: it does not change build identity, package integrity, or program
semantics. No crash, miscompile, ownership ambiguity, source/TKI divergence, or
required-workload blocker remains open. QSLite is therefore stopped at this
boundary; the next work is the fresh supported-platform RC matrix.
