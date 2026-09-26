# RC14 memory-contract preparation acceptance

**Status:** Accepted within the bounded optimization scope at
`4222f188823ea35e5c010984b3c45e0653cb4ca9` after independent incremental
review. This is **not** RC14 release qualification. Base measurement
checkpoint: `735290b8`.

## Scope and invariant

The expensive part of `MemoryContractShadow::analyze` is preparation of a
capture-analysis LLVM module. On the retained 5,042-line JSON matrix, one
pre-optimization run spent **0.17/0.16 s** cloning, **9.31/9.65 s** in
mem2reg, and about **1.3 ms** generating records on the initial/verification
passes. The old `verify()` called `analyze()` again and repeated preparation.

This candidate retains a one-shot, per-shadow prepared capture module for the
immediately following verification. Verification independently generates the
expected records from current summaries and IR, compares every record, and
still checks duplicate records, the separate noalias gate, and actual emitted
IR attributes. It does **not** reuse the first `Records` as the expected
answer. Preparation is consumed on verification; a later verification falls
back to fresh preparation rather than using a cross-phase cache.

Reuse requires the same module and ordered source modules, the same borrow
check mode, and exact IR and relevant summary/formal-name snapshots. Any
change rejects before consulting the prepared capture module. Snapshotting is
content comparison, not Module-pointer equality alone. No ABI, TKI, language
contract, or ownership decision was changed.

## Measurement

Two instrumented optimized compile/link runs of the same matrix both
succeeded. The old instrumented run measured `verify` at **19.38 s** and
compiler total at **38.27 s**. Optimized runs measured `verify` at **10.66 s**
and **10.27 s**, with compiler totals **30.67 s** and **30.40 s**. The saved
verification pass is about **8.7–9.1 s**; observed total improvement is about
**7.6–7.9 s**, roughly **20–21%** for this sample, not a universal compiler
speedup. Object emission still takes about 15–16 s and was not changed.

The tiny sample's `verify` grew from **8.05 ms** to **10.99 ms** because the
content seal costs more than a second preparation on a tiny module. A
source-hidden consumer measured **8.31 ms** versus **10.61 ms** for `verify`,
while total compilation was approximately unchanged (**329 ms** versus
**330 ms**). The large-module gain and small-module overhead must both remain
visible in subsequent evaluation.

The independent reviewer measured the same large matrix in object-emission
mode: `verify` **18.48 → 9.76 s** and whole command **37.09 → 28.63 s**
(about **23%** for that pair). Small source and source-hidden samples each
added roughly **2–3 ms** of verification overhead. These are distinct samples,
not figures to average into a universal speedup.

## Equivalence and fault evidence

An independent Release compiler built from `735290b8` compiled the same
source fixture, large JSON matrix, and source-hidden TKI consumer. For each,
baseline and optimized contract JSON SHA-256 values matched; corresponding
object files were byte-identical. Both experimental `nocapture` and
`readonly` modes also produced matching object hashes. Normal source and
source-hidden checks did not change diagnostics or contract decisions.

| Compared case | Contract JSON SHA-256 | Object SHA-256 |
| --- | --- | --- |
| Source fixture | `1279c53b416b300ad1287e1bc1889705ab4ad3960fbffdee70d276d35a10cd7c` | `133bbbe2ab2c93fc5b7bee1f47f6d2f91b889d26e18ba90eeeed3d49f9ab449b` |
| JSON matrix | `55aace143c48eb52b1e9cf4f02c1c861bf3b733a8f6d51e9f4fc95dc8355cd81` | `1302055c44d791bfe8276108a5b6333526c68c05b06d4f95e6cdf91e5a0b299f` |
| Source-hidden consumer | `90c085cd62cbf86163a4e8feba98fbb3c8c3ee3e67ae71d78eaaccd9c548b282` | `6e7d28015ad73818626db84a123ba9b81971964daf08445b7f45b2c3b112636b` |

The registered `toka_memory_contract_preparation` test covers source and
source-hidden records, object-byte parity with/without JSON dumping, disabled
borrow checking, and object/IR no-artifact rejection for test-only faults:
changed record, IR, summary, mode, and pre-existing erroneous IR attribute.
Those five injected faults all returned error 1 and produced no artifact in
the local run. Six related CTest gates, including source-hidden execution,
RC13 replay, whole-value generics, RC14 call-address and authority, passed
**6/6**. The complete tool build passed.

Independent review reran two registered gates **2/2**, checked **10** further
API boundaries—including a second verification preparing afresh, changed IR,
summary, formal name and module list, duplicate records and erroneous IR
attributes—and compared five old/new source, source-hidden and experimental
mode pairs. Every contract JSON and object pair was byte-identical. The older,
unregistered `test_memory_contract_shadow.py` generic assertion fails on both
baseline and candidate; it is a separate baseline test issue, not counted as
passed or attributed to this optimization.

Full PASS passed **459/459**, full FAIL **480/480**, and conformance
**325/325**. The first no-exclusion serial CTest attempt was interrupted:
the host wall clock jumped by hours, leading to five recorded timeouts. Those
five gates subsequently passed **5/5** in isolation; that evidence was not
called a complete-suite pass.

After scoped acceptance, the unchanged implementation was rerun in one
complete, no-exclusion CTest invocation under a declared stable budget:
`caffeinate -i -m -s ctest --test-dir <external Release build>
--output-on-failure -j 1`. It ran outside the filesystem/process sandbox so
existing harnesses could use `/bin/ps`; all test-specific timeouts and
assertions were unchanged. The result was **126/126 passed**, exit 0, in
**2,727.50 s**. `toka_channel_storage` passed inside that run in **44.96 s**.
This establishes the complete local CTest gate for this candidate under the
stated budget; it does not prove the cause of the earlier parallel Channel
timeout or by itself grant RC14 release qualification.

This package is closed. Do not add a small-module heuristic, backend
optimization-level change or another safety-proof shortcut to this accepted
slice.
