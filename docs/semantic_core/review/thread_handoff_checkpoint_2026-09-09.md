# Thread source checkpoint: acceptance is narrower than the commit

Date: 2026-09-09
Parent: `2876f03c` (accepted nullable sync migration)
Status: reproducible implementation checkpoint, NOT a binding freeze

## Accepted scope

The user accepted the three source-layer P1 fixes and the private source subset
documented in `thread_handoff_source_progress.md`: actual invoke/cleanup mode,
nominal/structural result cleanup, non-test compilation, sealed source plans,
and the registered source/runtime/fault tests. The independent source CTest
passed 1/1 in 48.21 seconds. No public thread API is enabled by this acceptance.

The earlier independent A/B core and nullable sync commits remain accepted on
their own boundaries. This checkpoint does not move any previous freeze ref.

## Unaccepted binding-base dependencies included for reproducibility

The source subset uses shared AST, Sema, CodeGen and build files already modified
by the binding work. This commit intentionally preserves that implementation
base rather than presenting a source-only commit that cannot be reproduced.
Inclusion does **not** grant acceptance to these groups:

1. Initialization/whole-binding assignment activation, planner facts, cleanup
   dispositions, transaction rollback and their integration/oracle migration.
2. Callable environment, parameter/local-bound propagation, on-demand factory
   summaries, structural/static/raw source propagation and integration tests.
3. Dedicated raw_take syntax, qualification/lowering and minimal Vec::pop
   integration. Individual accepted primitive tests do not qualify every Vec
   element, generic ABI or the entire binding matrix.
4. Enum payload cleanup and canonical owning-string facts. Their accepted
   subpatches do not qualify upstream declaration parsing or generic ABI.
5. Unsafe writable raw construction and the five string buffer migrations.
   Existing std/thread raw construction remains an intermediate implementation,
   not the accepted A/B environment/result handoff integration.
6. Triage reports, progress records and negative-harness changes. Historical
   counts in these files are not a new full-suite result for this checkpoint.

`explicit_call_boundary_cede_stage1_binding.md` therefore retains its
not-ready-for-freeze status. No full PASS/FAIL or cross-platform release gate
has been rerun for the private source acceptance.

## Reproduce the accepted subset

Use an external build directory and LLVM 20. From this source checkout:

```sh
cmake -S . -B /absolute/external/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build /absolute/external/build --target tokac -j 4
ctest --test-dir /absolute/external/build --output-on-failure -R '^toka_thread_handoff_source$'
```

CTest supplies the source-library path. The source runner links the versioned
runtime explicitly; the private entry does not silently replace std/thread.
For a non-test check, configure a separate external directory with
`-DBUILD_TESTING=OFF`; build tokac and confirm the private CLI option is rejected.

## Next independent slice

Integrate std/thread with the accepted A/B packet, control and result-lease
contracts. Preserve create-failure ownership, the worker-finishes-before-create
race, recoverable explicit join/detach failures, implicit-drop fatal policy,
and exact-once environment/result cleanup. No further private-probe expansion.
Run the directed responsibility matrix first; run one full integration round
only after that candidate converges. Public integration, binding, Stage 1 and
release qualification remain pending.
