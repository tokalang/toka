# Toka 0.9.9 release-closure plan

Status: `Complete`

The 0.9.9 line qualifies the language, compiler, SDK, semantic service, and
machine-facing tooling intended for Toka 1.0 without changing the public
version to 1.0. The first candidate is `0.9.9-01`. Completing this plan freezes
the implementation evidence; it does not itself authorize a `v1.0.0` tag.

No task in this plan may add syntax or expand the frozen language surface.

| ID | Status | Deliverable | Exit evidence |
| --- | --- | --- | --- |
| `R9-1` | `Complete` | Establish the 0.9.9 candidate line | Compiler, interface, SDK tools, diagnostics, package defaults, and documentation report `0.9.9-01` |
| `R9-2` | `Complete` | Close generic-template module identity | `semantic_core/generic_shape_identity_closure.md`: same-name layouts, generic methods, nominal mismatch, source-less TKI, and cache regeneration are isolated |
| `R9-3` | `Complete` | Unify the release gate | The schema-version-2 gate has 13 fail-closed stages including explicit research-fixture quarantine lists, SDK installation, semantic index, LSP, AI contracts, scale/soak, sanitizer, sustained applications, and package smoke; all six tooling sub-gates and deterministic count aggregation pass |
| `R9-4` | `Complete` | Produce current-candidate evidence | Revision `ca8181129c6d726f1295f5546171e18360b05bcb` passed all 13 stages with `source_dirty: false` on Linux x64/arm64 and macOS x64/arm64 in release-gate run `29910583851` |
| `R9-5` | `Complete` | Reconcile and freeze the ledger | FZ-5, native-build, tooling, release notes, tags, and reports identify historical versus current evidence without contradiction; no 1.0 version, tag, release, or archive was created |

## Exit conditions

- `FZ-3-R02` has direct source/TKI/cache evidence and is no longer pending.
- The release gate is fail-closed and has no silent test exclusions.
- Any research fixture exclusion is named in a checked-in list with rationale.
- `tokac`, `toka`, `tokafmt`, and `tokalsp` agree on `0.9.9-01`.
- The candidate package contains and exercises all four tools.
- The candidate revision has a clean native report and is ready for the
  Linux x64/arm64 and macOS x64/arm64 matrix.
- No `1.0.0` version or tag is created by this phase.

The frozen code candidate is revision
`ca8181129c6d726f1295f5546171e18360b05bcb`. Release-gate run
`29910583851` produced four clean schema-version-2 reports, each with all 13
stages passing. This completes the 1.0 engineering-closure evidence while the
public version remains `0.9.9-01`; it is not a final 1.0 release act.

## Superseded follow-up: 0.9.9-02 adoption validation

On 2026-08-10, the maintainers chose to begin the `1.0.0-rc.1` candidate
qualification directly rather than create a further 0.9.9 release. The
remaining adoption targets below move into
[`1_0_rc1_release_plan.md`](1_0_rc1_release_plan.md). This does not change
the historical 0.9.9-01 evidence recorded above.

The originally planned follow-up validates the frozen implementation in real work rather than
adding language features. Its completion targets are:

- two representative internal applications build, test, package, and run on
  the supported Linux and macOS target families;
- Go, Python, C/C++, Zig, and Rust integration boundaries are exercised where
  the applications require them, with reusable package or FFI examples for
  the paths actually used;
- AI-assisted editing is measured against the checked-in structured
  diagnostics, semantic context, formatting, and coding-task contracts, with
  regressions kept in the release gate;
- a 30-day sustained adoption window closes with no known P0/P1 correctness,
  safety, reproducibility, or delivery blocker.

The exact two application choices are an owner decision made when the phase
starts. They do not reopen the frozen 1.0 language surface.
