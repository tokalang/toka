# FZ-3 Compiler Reliability Closure

Status: `Complete`

`FZ-3` closes the compiler correctness and reliability defects found by the
1.0 audit. Local implementation evidence is complemented by clean mandatory
gate results on Linux x64/arm64 and macOS x64/arm64.

## Correctness Closure

- Owned locals now carry runtime drop-live state. A successful `cede`, return
  transfer, consuming method call, spread, or closure capture clears that
  state, while untaken control-flow paths retain their cleanup obligation.
- Stack closure environments and dynamic function values participate in normal
  scope cleanup. The resource matrix checks exact drop counts across direct
  transfer, taken and untaken branches, closure capture, async suspension, and
  detached owned handoff.
- Raw may-zero pointer `.unwrap` lowers through the stable runtime panic path
  with the message `null pointer unwrap`; it no longer reaches an LLVM trap.
- Bare `[N]T(...)` array construction is outside the frozen 1.0 expression
  grammar and is rejected in Sema with `E04586` before CodeGen.
- Grouped-expression and named-initializer parser recovery now guarantees a
  valid expression or forward token progress, preventing malformed input from
  hanging or dereferencing an absent expression.

## Lexical Import Closure

Physical module discovery is now separate from the lexical namespace exposed
by an import. Selective imports and aliases govern functions, externs, globals,
shapes, type aliases, and traits consistently in source and source-less `.tki`
replay. Imported globals retain their canonical CodeGen identity, and generic
instantiation retains both definition-site and call-site lexical context.

The frozen prelude decision is deliberately narrow: only `@Encap`, `@Send`,
and `@Sync` are implicit prelude traits. Every other trait requires lexical
visibility. Core modules that do not import the prelude name their own trait
dependencies explicitly. Generic trait families retain their type arguments
when aliases are canonicalized, including associated-type lookup.

The old resolver filtering TODO represented a real public leak rather than an
unreachable path. Focused negative cases now prove that an unselected function,
extern, global, shape, alias, or trait is unavailable, while the selected and
renamed forms remain usable through both provider source and `.tki` alone.

## Determinism And Mutation Gate

`tools/scripts/audit_fz3_reliability.py` is a fixed-seed, bounded audit with a
deterministic JSON result. Seed `0x544F4B41` executes 82 checks:

- 24 representative valid and invalid core-corpus compilations;
- 32 parser mutations;
- 12 Sema mutations;
- 10 damaged source-less interface mutations;
- 4 repeated-output determinism checks.

The gate treats signals, timeouts, sanitizer reports, unexpected acceptance,
and unexpected rejection as failures. It passes with both the normal compiler
and a final-source AddressSanitizer/UndefinedBehaviorSanitizer build. Leak
checking is disabled on macOS because the platform runtime does not provide
LeakSanitizer; address and undefined-behavior checks remain active.

## Environment Reliability

Mandatory UDP, TCP, async network, and async HTTP tests now bind port zero and
query the assigned local port through the platform network layer. They no
longer depend on fixed ports or retry-based masking. Linux, macOS, and Windows
have native local-port implementations; WASI keeps an explicit unsupported
stub because it is not a 1.0 release blocker.

## Verification Snapshot

Local platform: macOS arm64.

- release compiler build: passed;
- positive suite: 317 passed, 0 failed;
- negative suite: 235 passed, 0 failed;
- warning suite: 1 passed, 0 failed;
- semantic source/source-less replay: 11 passed, 0 failed;
- semantic cache invalidation: 12 passed, 0 failed;
- path, TKI metadata/cache, unsafe/raw revalidation, excluded-syntax,
  semantic-evidence, trusted-memory-evidence, and incremental gates: passed;
- normal fixed-seed reliability audit: 82 passed, 0 failed;
- ASan/UBSan fixed-seed reliability audit: 82 passed, 0 failed;
- diff whitespace validation: passed.

The JSON reports used for the local run were emitted outside the source tree.
They are reproducible evidence, not a public compiler or cache ABI.

## Supported Platform Closure

Release-gate run `29202522704` executed all four supported target rows from
clean checkouts of revision `3ab00dff`. Linux x64/arm64 and macOS x64/arm64
all passed without ignored failures. Each row includes the complete positive,
negative, replay, cache, incremental, async, sanitizer, and package gates.

No language-design question remains open in `FZ-3`; `FZ-3-P01` is complete.

Milestone commit subject: `feat: close local Toka 1.0 compiler reliability`.
