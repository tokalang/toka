# Ephemeral Semantic Diff Preview v1

`tools/scripts/semantic_diff_preview.py` compares two explicit root-source
snapshots without changing either snapshot or the current workspace:

```sh
toka preview --base /work/base/main.tk --candidate /work/candidate/main.tk
```

The repository script is also directly runnable for SDK integration tests or
tools that choose their compiler binary explicitly:

```sh
python3 tools/scripts/semantic_diff_preview.py \
  --tokac /opt/toka/bin/tokac \
  --base /work/base/main.tk --candidate /work/candidate/main.tk
```

It emits exactly one deterministic JSON document with schema
`toka.semantic-diff-preview`, version `1`. The frozen envelope is described by
[`schemas/toka.semantic-diff-preview.v1.schema.json`](../schemas/toka.semantic-diff-preview.v1.schema.json).

## Semantics and scope

Preview runs fresh, read-only compiler checks for both inputs:

- `--diagnostics-json --check-only` for diagnostics;
- `--semantic-index=json --check-only` for public declaration contracts; and
- `--semantic-evidence=json --check-only` for compiler decision facts.

The commands intentionally remain separate: their JSON protocols are
independently versioned and the compiler rejects ambiguous combined output
modes. A source error is normal Preview input, not a Preview failure. In that
case diagnostics and evidence remain available while the semantic index may be
unavailable; `analyses` reports that fact explicitly.

Version 1 compares facts rooted in the supplied source files:

- added, removed, and changed root diagnostics;
- added, removed, and changed public root symbols/contracts;
- the H/P, flow, morphology, nullability, and return-dependency projection of
  those contracts under `capabilities`;
- public raw-pointer/`unsafe-raw` contract surface under `unsafe_surface`; and
- compiler evidence whose primary or origin location is a root source.

Imported modules are used by the compiler exactly as normal resolution requires
but are not themselves treated as two directory trees to diff. That deliberate
boundary keeps v1 meaningful for an editor's base/candidate buffer pair and
avoids hiding a write-capable workspace copier behind a command called
“ephemeral”.

`inputs` records source digests. `summary.read_only` means the Preview command
does not write input sources, the workspace, compiler objects, interfaces, or
an overlay cache. It is a per-request process orchestration layer, not the
long-lived Overlay Session planned for a later phase.

## Consumer contract

Consumers must read `analyses` before treating a missing section as “no
change”. A missing semantic index after an invalid candidate means unavailable,
not that its public API was removed. Locations use `$root` for the relevant
input source, making comparison independent of whether base and candidate live
at different filesystem paths. Repository paths are made repository-relative;
other non-root paths are represented as `$external`.

The command exits `0` when both input files and `tokac` are available, even if
either program does not type-check. It exits `2` only when Preview itself cannot
start (for example a missing snapshot or compiler binary).
