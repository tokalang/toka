# CDW1 Activation Preflight Audit

**Status:** P1 declaration-recomputed activation is complete as an explicit,
default-off validation profile. P2 additionally qualifies one local,
object-attested Outcome fulfilment profile. CDW1 audit comments remain
ignorable in both cases.

## Purpose

This audit decides whether the existing CDW1 prototype may be wired into TKI
import. It does not decide a bodyless-provider design or freeze a semantic
manifest payload.

## Current evidence

| Gate | State | Evidence |
|---|---|---|
| deterministic encoder and strict decoder | complete | `toka_canonical_declaration_witness` |
| known-coordinate, non-generic P1 record | complete | Outcome retained-body replay |
| source/source-less reconstructed bytes | complete | `test_outcome_body_recheck.sh` |
| audit comment cannot affect Level-A import | complete | malformed, missing, and repeated `cdw1:` checks |
| bodyless Outcome provider remains rejected in P1/default mode | complete | `E04631` retained-body boundary |
| independent semantic envelope carrier | complete | P1.0/P1.1 `.tki.tsm` + replay-surface closure |
| importer-visible declaration comparison | complete in explicit profile | `--validate-semantic-manifests` |
| atomic declaration/tamper failure matrix | complete for P1 profile | valid, missing, non-canonical, record-mismatched, and bodyless gates |
| project-build propagation and clean-cache check | complete | `toka build --validate-semantic-manifests` qualification |
| local bodyless Outcome fulfilment | complete in explicit P2 profile | object marker, exact digest, compiler-local provenance HMAC, Sema record comparison, and final-link revalidation (`E04634`) |
| P2 build propagation and clean-cache check | complete | `toka build --validate-semantic-manifest-attestations --semantic-manifest-provenance-dir <dir>` qualification |

## Artifact boundary

```text
resolved declaration
  -> CDW1 encoder
  -> @tki v2 cdw1: audit comment
  -> lexer discards comment
  -> retained-body Level-A recheck

resolver-selected source-less TKI (explicit profile only)
  -> reconstruct canonical CDW1 from declarations
  -> exact adjacent .tki.tsm + replay-surface closure validation
  -> complete record-set comparison
  -> accept profile check | E04633
```

`InterfaceV2Facts` transports CDW1 only as an ignorable comment. P1.2 never
parses that comment: it obtains raw records solely from the strict sidecar and
compares them with Sema's reconstructed records. A provider-controlled comment
therefore cannot become an authority carrier merely by being parsed later.

Resolver-owned module coordinates are available to the admitted P1 encoder,
but current `.tki` metadata is not an accepted-provenance semantic manifest
and does not bind a declaration record to an exact provider object.

## P1 profile consequence

CDW1 was not promoted in place. The explicit profile uses the separate
compiler-owned envelope and, for the recomputed-declaration class:

1. obtains the envelope through resolver-owned module identity;
2. strictly decodes the complete canonical admitted CDW1 record set;
3. reconstructs CDW1 from the imported declaration;
4. compares canonical bytes atomically; and
5. rejects absence, duplication, mismatch, unknown required data, or a stale
   envelope without retaining any partial semantic fact.

The profile is intentionally default-off: ordinary retained-body Level-A
imports still accept a missing sidecar, while an opted-in validation build
fails closed on a missing, stale, malformed, or mismatched sidecar. The
comparison still cannot accept a bodyless Outcome provider. Its only claim is
agreement about a declaration fact; Level-B fulfilment needs the separate
accepted-provenance and exact-object-binding design in the Semantic Manifest
Envelope RFC.

## P2 local-attestation consequence

The P2 profile now supplies that separate body-fulfilment path for the admitted
Outcome subset only. A source provider emits a bodyless TKI and version-2
`.tki.tsm` only when it is compiled with an explicit local provenance state
directory. The sidecar's paired declaration/fulfilment records are bound to an
IR-retained payload marker and exact object digest, signed by the local
compiler-state key, compared with Sema's reconstructed declarations, and
reloaded immediately before LLD sees the selected object. A compile-only
consumer is rejected because it cannot preserve that link obligation.

This does not promote a provider-controlled manifest into global trust: a
copied provider does not have the accepting compiler's state key, default
imports remain Level A and report `E04631`, and methods, generics, init,
unsafe wrappers, async cleanup, remote provenance, and recursive persisted
obligations remain outside P2.

## Remaining design decision

P1 and P2 are both invocation-scoped `toka build` policies. The next decision
is a distributable provenance and persisted-link-obligation design, not
default activation: it requires a migration story for legacy TKI, a producer
identity policy beyond one local compiler state, and equivalent identity
coverage for methods and generics. It does not authorize embedding CDW1 in a
source comment or trusting a provider object merely because it carries hashes.
