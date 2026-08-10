# Tooling phase 2 qualification

The phase-2 reference workload is the checked-in telemetry pipeline under
`tests/tooling/pilot_project`. It has 21 modules and 6,024 Toka source lines.
The generator is checked against the committed source before each qualification
run, so the workload cannot drift silently.

Run the qualification after building the SDK:

```sh
python3 tools/scripts/test_tooling_scale.py
```

The checked-in gates require:

- at least 20 modules and 5,000 source lines;
- 100 edits from seed `20260722`;
- warm end-to-end p95 analysis latency no greater than 125 ms; and
- child-process peak RSS no greater than 1,024 MiB.

Every edit must produce a fresh semantic index, recheck exactly the changed root
module, and preserve clean diagnostics. The test also changes an unopened
dependency on disk; the session must leave its fast path, invalidate that module
and the root, and agree with a final clean compiler check. LSP exit status,
stale-result count, machine class, raw cold/warm timings, and peak memory are
printed as versioned JSON in CI.

On the 2026-07-22 reference run (macOS arm64), the workload recorded 147.7 ms
cold analysis, 41.6 ms warm analysis p95, 42.0 ms warm round-trip p95, and
46.4 MiB peak RSS. These values are evidence from one machine, not portable
performance promises. The 125 ms CI gate deliberately leaves cross-runner
scheduling headroom while still bounding sustained warm-path regressions;
`gates.json` is the repository qualification contract.

The warm path is guarded rather than optimistic: it applies only when the root
overlay is the sole changed document, its import signature is unchanged, and
all on-disk dependencies retain their timestamps. Other changes use full graph
resolution. Semantic-index construction caches normalized paths and source-line
tables within each build to avoid repeated filesystem and buffer scans.

The PR gate explicitly quarantines `tests/fail/dyn_privacy.tk`. That fixture
expects the pre-1.0 research behavior described by the paper, while this phase
uses the maintainers' stated 1.0 assumption. The test remains checked in and can
still be run directly; the exclusion is centralized in
`spec/ci_quarantined_fail_tests.list` rather than being silently treated as a
pass.
