# RC14 `verify` performance diagnosis

**Scope:** measurement and profile instrumentation only. No contract decision,
compiler admission, LLVM IR or test timeout is changed. The accepted
call-address repair remains `174633c08a7a838340852ceb867bb36b31b031a5`.

`TOKA_PROFILE=1` now reports components of the existing `verify` interval;
the aggregate `verify` marker remains unchanged. The source is the same
5,042-line generated JSON parser matrix retained in
`validation/rc14-audit-20260926.4RXAYY/parser_matrix.tk`. Both full
compile/link runs succeeded with the same external Release build and module
search paths. Times below are wall-clock milliseconds within `tokac`, not
per-function CPU samples.

| Stage | Run 1 | Run 2 |
| --- | ---: | ---: |
| LLVM module verification | 29.8 | 29.6 |
| Memory-summary IR verification | 2.2 | 1.9 |
| Memory-contract analysis | 8,983 | 9,010 |
| Memory-contract verification | 8,909 | 9,182 |
| Entire `verify` interval | 17,924 | 18,224 |
| Native object emission | 14,602 | 16,343 |
| Compiler total | 35,794 | 37,841 |

For the retained tiny program, the same instrumented compiler measured
`verify` at **8.05 ms** within a **355 ms** compile. This hotspot is therefore
important for large generated modules, not an explanation of the fixed cost
of every small compile. LLVM's first module-verifier call is about 30 ms in
the large sample; attributing the 18-second interval to LLVM would be wrong.

The first optimization target is **duplicate full memory-contract analysis**.
`MemoryContractShadow::analyze` builds a capture-analysis clone of the entire
LLVM module and promotes eligible allocas. Immediately afterward,
`MemoryContractShadow::verify` calls `analyze` again to construct expected
records, so the same broad analysis is performed twice before the cheap
record/attribute checks. The two measured halves are correspondingly close
in cost. This is a target selection, not yet proof that cloning alone consumes
all nine seconds or authorization to remove the independent safety check.

Before changing production behavior, benchmark clone/promotion versus
record evaluation, then seek a bounded way to avoid redundant **work** while
preserving fresh record comparison, duplicate/noalias checks, emitted-attribute
checks and fail-closed fault outcomes. Do not replace the verification with
"analysis already passed," weaken a contract, or add timeouts to claim a
speedup. Measure the chosen change on both this large matrix and small
source/cache cases, and validate contract fault gates plus the fixed-candidate
test suite separately. Channel's parallel timeout remains an independent
qualification-stability item.
