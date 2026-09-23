# RC13 release-gate blind-spot inventory (diagnostic, not qualification)

Fixed inspected candidate: `71173047861fdcddd016166ed8a20ffcb972750c` on macOS arm64. The official release gate passed its build stage, failed its PASS/conformance stage (446/446 release PASS cases; 288/325 conformance), and **did not execute the remaining eleven stages**. The 13 quarantined PASS cases are not part of 446/446; no new exclusions were added. Independent stage commands were run on the same immutable candidate only to reveal later blockers. Their successes do not change the failed official result.

The machine-readable command results and logs are in `/Users/zhyi/GitDP/tokalang/validation/rc13-main-prequalification-xc1aeqba/unreached-stages/matrix.json` and its sibling `.log` files. QSLite, installed-SDK DX, archive smoke and relocation results came from the preceding native-r2 diagnostic records in that same validation directory.

| Official stage | Official result | Independent diagnostic on fixed candidate |
| --- | --- | --- |
| build | pass (CTest 123/123) | Same official result; does not include all conformance/DX checks. |
| pass | fail | Release PASS 446/446, conformance 288/325; recursive enum object emission crashed in one negative case. |
| fail | not run | 1/1 command passed. |
| warn | not run | 0/1: legacy `fn#(Counter#)` fixture rejected with E0496 before warning checks. |
| semantic replay | not run | 5/6 commands passed; quick handle-grammar audit stopped in PlaceIterator security fixture. |
| cache invalidation | not run | 3/4 passed; mixed-core cache exposed malformed exported `raw_take` body. |
| tooling | not run | 14/17 remaining commands passed. Three failed: AI tooling and JSON CLI initially used the warning fixture; AI authoring friction expected an older capability diagnostic. Installed-SDK DX also failed on the warning fixture. |
| incremental | not run | 1/1 command passed. |
| native build reference | not run | 3/3 commands passed, including 100-cycle sustained qualification. |
| QSLite | not run | Separate runtime and toolchain diagnostic reports both passed. |
| async | not run | 6/6 commands passed. |
| sanitizer | not run | Configure, build and reliability audit 3/3 passed; macOS leak detection was disabled for this ASan diagnostic. |
| package smoke | not run | Two remaining supply-chain/package commands passed; archive smoke 18/18 and relocation checks passed separately. |

Current WIP on `main` is not included in these numbers. It has already removed the enum recursion crash in targeted checks, migrated the shared integer-counter first error, preserved six W0408 warnings through a legal `@Callable` invocation, restored PlaceIterator security fixture reachability, corrected TKI `raw_take` source export, and migrated the Windows raw-pointer write requests. The mixed-core cache diagnostic now passes on the WIP compiler and library. A final fixed SHA must rerun the official gate; no independent diagnostic is a substitute.

After the conformance/DX repairs, a later **WIP-only** conformance pass reported 324/325. Its sole remaining positive is `io_datafile_concurrency_01`: four cloned `ReadDataFile` RAII handles are captured into worker closures, but the public thread gate has no per-instance responsibility proof for the native `Addr` handle. `@Send` alone cannot supply one. The existing runtime uses atomic retain/release; either a narrowly trusted, operation-bound lease contract or a change to the test's cross-thread clone objective requires an explicit decision. This 324/325 is not a release-gate result.
