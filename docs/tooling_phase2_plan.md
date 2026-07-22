# Tooling phase 2 acceptance matrix

Phase 2 turns the Toka 1.0 compiler into a persistent semantic service for
editors and AI coding agents. It changes no frozen language semantics. A phase
item is complete only when its public behavior, regression coverage, and
performance gate are all present.

| ID | Deliverable | Acceptance evidence | Status |
| --- | --- | --- | --- |
| T2-1 | Compiler semantic index and query API | Cross-module definitions/references, shadowing, typed completion, conflict-safe rename, deterministic versioned JSON | Complete |
| T2-2 | Persistent analysis session | In-memory document overlays; reverse-dependency invalidation; unchanged modules reused; revision statistics exposed | Complete |
| T2-3 | Semantic LSP core | Hover, definition, references, completion, and rename consume compiler symbol IDs; cross-module/generic/trait/impl/shadowing cases pass | Complete |
| T2-4 | Extended LSP surface | Signature help, document/workspace symbols, formatting, UTF-16 positions, cancellation-safe protocol behavior | Complete |
| T2-5 | Structured diagnostics and fixes | Versioned diagnostics contain stable severity names, primary and related spans, notes, and validated edits | Complete |
| T2-6 | AI-oriented CLI | `toka check --json`, `toka explain CODE`, and bounded semantic-context output are documented and tested | Complete |
| T2-7 | AI coding evaluation | Fixed tasks measure compile success, diagnostic-repair success, edit precision, and token/turn cost against a recorded baseline | Complete |
| T2-8 | Real project and service gates | A meaningful 5K+ line multi-module Toka project passes clean/incremental builds; cold/warm latency, memory, crash, and soak thresholds run in CI | Complete |

## Phase exit criteria

Phase 2 exits only when every matrix row is complete and the following hold:

- the pass, fail, warning, semantic replay, SDK, semantic-index, and LSP suites
  pass together;
- common single-file edits re-analyze only the affected reverse-dependency
  closure, with warm p95 latency at or below 100 ms on the reference fixture;
- semantic LSP operations have no known token-guessing fallback for supported
  AST constructs;
- machine diagnostics and fixes are deterministic and independently
  applicable;
- the 5K+ pilot completes a 100-edit fixed-seed soak without compiler/LSP
  crash, stale result, or clean/incremental disagreement; and
- the AI evaluation has a checked-in baseline and does not regress its compile
  success or repair success rates.

The latency number is a repository qualification threshold, not a universal
hardware guarantee. CI records the machine class and raw measurements so the
threshold can be revised from evidence rather than hidden behind a pass bit.
