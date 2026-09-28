# 0.10.0 macOS x64 qualification timeout diagnosis

Date: 2026-09-28. This is diagnostic evidence, **not release qualification**.
The failed four-target qualification remains [run 36315791887](https://github.com/tokalang/toka/actions/runs/36315791887) at `100b9fd62a6b9e12dcb1f5b8a4f2282dc088cb3f`.

## Controlled run

[Run 36365782807](https://github.com/tokalang/toka/actions/runs/36365782807) used a native `macos-15-intel` runner. It checked out the exact `100b9fd62a6b9e12dcb1f5b8a4f2282dc088cb3f` source, built `tokac` version `0.10.0`, prepared the native runtime, then overlaid only the two diagnostic test drivers from `aa940483c99cb1fdb84e66a558decab62f738670`. The diagnostic diff was those drivers plus the diagnostic workflow; fixture hashes, source tree, compiler hash, and both driver hashes are recorded in the uploaded artifact. Its `qualification` field is `false`.

The drivers printed a flushed JSON start/end record for each subprocess, including full command, PID, monotonic start time, elapsed time, return code, phase, and existing subprocess timeout. The two scripts ran serially with a diagnostic-only 20-minute outer limit apiece. Test inputs, assertions, and subprocess timeouts were unchanged.

| Driver | Result | Wall time, first start to last end | Child processes | Compile time | Link time | Runtime | Longest child |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| DataFile lease | pass | 180.375 s | 71/71 ended | 165.597 s in 62 compile checks; 13.187 s in 3 compile/link calls | — | 0.109 s in 5 runs | 5.226 s compile/link |
| RwMutex unlock | pass | 152.021 s | 88/88 ended | 148.603 s in 79 compile checks | 3.309 s in 5 links | 0.062 s in 4 runs | 2.798 s compile |

DataFile also spent 1.340 s inspecting runtime symbols. Child times total 180.233 s and 151.974 s respectively; the remainder is Python driver overhead. Neither script had a subprocess timeout, unmatched start record, or unexpected exit code. Nonzero exits were expected negative compiler checks, and both full test summaries passed. RwMutex completed all 15 rejection/parity/rollback cases, 6 check-only controls, and 4 counted runtime cases. DataFile completed the original 4×50 runtime, three exact-count/cleanup modes, direct cede, ten parity rejections, twenty negative cases, eight fault checks, and the source-hidden case.

The first RwMutex diagnostic driver labeled 15 `--emit-llvm` compiler invocations as `compile+link`; the table classifies them as compile using the recorded command. A follow-up driver edit fixes that label without changing execution.

## Conclusion and narrow correction

The original 180-second CTest limit was an entire-script budget. DataFile alone took 180.375 seconds in this serial run. RwMutex finished in 152.021 seconds serially, while the earlier full-suite run exhausted 180 seconds; that difference is consistent with full-suite load. The logs rule out a stuck subprocess in this controlled run, but cannot prove every future run will have the same timing.

Set the CTest total budget for just these two drivers to 300 seconds. This leaves about 66% headroom over DataFile's measured duration and about 97% over RwMutex's. Existing compiler/runtime subprocess limits and all coverage remain unchanged. The change needs a new, fixed-SHA hosted four-target qualification before any release decision; the diagnostic pass does not replace the failed gate.

Artifact SHA-256 values (downloaded from run 36365782807):

```text
datafile.log      88dd6c61bad3ec73549c5294bcebc34865306237998e64ced6ce95ca74a058dc
rw-unlock.log     5bafdbb51f745a7c61b4909790336c4756a163719f405d8699e9aa934bf32285
fixtures.sha256   2cad20a765e0cbf4f6dc6f2e8188157ac959a32d272d8de1c52c64eee9aeaa9e
identity.txt      a9ded5113e9f8af3de92f4034a2d680d3e420fba95e5406f6107ad8a6ed89dfc
outcome.json      e6be3585f9c7e5021adb63c91c3c505ce8c6fc9cd96ca0c7fc094d0a3559cc62
```
