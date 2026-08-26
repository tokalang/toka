# RC9 CI Gate Design — Draft / Non-normative

**Status:** Local M0 design only. This file does not modify or authorize any
GitHub workflow.

## Goal

Separate fast branch feedback from immutable release qualification while
retaining exact-SHA evidence.

## Proposed tiers

| Tier | Trigger | Intended coverage |
| --- | --- | --- |
| T1 developer | Local edit/commit | Build, targeted regression, classifier, selected replay |
| T2 main | Push/PR | Linux x64 broad gate; macOS/Linux ARM build plus focused ABI/replay; bounded tooling |
| T3 RC | Annotated RC tag or explicit immutable-SHA dispatch | Four native targets, complete release gate, archives and qualification summary |
| T4 promotion | Protected manual approval | Clean archive replay, checksum verification, public prerelease promotion |

Windows dogfood remains scheduled/manual or RC feedback unless it becomes a
declared blocking target.

## Evidence reuse rule

Evidence may be reused only when it binds the same commit SHA, compiler
provenance, workflow schema, target identity, and source-clean state. A tag run
should consume valid T2 attestations where policy permits and run only missing
release-specific stages; it must not silently trust an earlier branch name or
mutable artifact.

## Recovery requirements

- Every release workflow remains manually dispatchable by exact SHA and tag
  label.
- A rerun startup failure must not erase the original attempt evidence.
- LSP/tooling failures upload process exit status, signal, stderr, and the
  failing fixture identity.
- Tag creation, draft creation, and public promotion remain distinct states.
- No workflow change is made while RC8 evidence is active.

## RC8 accounting hold

The repository baseline document still describes RC8 as untagged/pending,
while the release object now exists as annotated tag object
`6dbb398e394bf53328721aeb4a956899b305ab38`, peeled to
`997713f4828b43a5b82aa3363d99a37e9e6f2417`. M0 records the discrepancy but
does not edit RC8 plans or audits. They must be reconciled only with the final
hosted qualification and replay receipts.

## M0 success receipt

M0 produces this design, local validation scripts, and testable diagnostics.
It does not change `.github/workflows`, dispatch hosted work, or claim reduced
runner time without a later measured qualification.
