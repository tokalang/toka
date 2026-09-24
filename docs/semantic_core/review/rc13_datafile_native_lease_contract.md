# RC13 private `ReadDataFile` native lease contract

This is a deliberately narrow production admission for the existing source-visible `std/data_file` implementation. It does not change native ABI, thread runtime protocol, reference counting, `WalFile`, or general `Addr`/wrapper qualification.

The compiler first selects the exact trusted toolchain module, nominal declaration and physical layout (`ReadDataFile` with one private writable `Addr` field). The reviewed `std/data_file.tk` and `sys/libc.tk` **buffers actually parsed by Sema**, and the selected `open`, `clone`, `read_at`, `drop` and native declarations, must match the private schema. It does not re-read a mutable filesystem path to certify a different parsed body. Identity selects this contract; **the checked operation edge produces an instance receipt**. A same-named user shape, an arbitrary `Addr`, a direct constructor or an imported interface without this source body produces no receipt.

| Checked edge | Receipt and runtime responsibility |
| --- | --- |
| Successful `ReadDataFile::open` result | Pending `Result` receipt; the native open allocated one control block/ref. Err has no owned lease. |
| Trusted `Result.unwrap()` of that pending result | One live `ReadDataFile` lease on the returned binding. `is_ok`/`is_err` merely inspect and do not create another lease. |
| Selected `ReadDataFile.clone()` on a live lease | A distinct lease, backed by the existing native atomic retain. The source lease stays live. |
| Validated `cede` binding/capture transfer | Moves that lease; no retain. A second capture or use of the moved source is rejected. |
| Whole-binding replacement / unknown call or write | Old binding cleanup follows normal Sema/CodeGen; a new current-value receipt is required. Unknown or altered sources lose qualification. Branch join/rollback use the existing owner-recipe state. |
| Thread publication and `read_at` | Existing public-thread plan carries the exact capture witness; CodeGen verifies site, type, operation lineage and receipt. Reads still require a live binding and the existing buffer ownership/byte-storage plan. |
| Drop | One existing native release per lease; only the final release closes the fd and frees the control block. |

The admitted lineage is intentionally restricted to exact `open → unwrap → clone/cede` operations. Rewrapping the value in an unrelated `Result`, relaying through an unqualified helper, source-hidden `std/data_file`, a changed retain body, forged construction and same-named user types stay fail-closed. The source-content seals are private implementation policy, not a public type identity or a TKI fact; changing either reviewed library file requires requalification.

The dedicated gate runs the original four-worker/fifty-read conformance program unchanged. Its test-only native observation build checks open failure, replacement, early original-owner drop, unused closure, spawn failure, direct move, each release, and exactly one final close/free. Production runtime objects do not export the observation symbols. Negative, source-hidden, altered-body and fault-injection cases reject without object or LLVM IR output.
