# CDW1 Activation Preflight Audit

**Status:** P1 declaration-recomputed activation is complete as an explicit,
default-off validation profile. CDW1 audit comments remain ignorable; the
profile validates only a separate sidecar and changes no caller, cleanup, or
object-link authority.

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
| bodyless Outcome provider remains rejected | complete | `E04631` retained-body boundary |
| independent semantic envelope carrier | complete | P1.0/P1.1 `.tki.tsm` + replay-surface closure |
| importer-visible declaration comparison | complete in explicit profile | `--validate-semantic-manifests` |
| atomic declaration/tamper failure matrix | complete for P1 profile | valid, missing, non-canonical, record-mismatched, and bodyless gates |

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

## Remaining design decision

The next decision is whether a future release should make this P1 profile part
of a package/build policy. That requires a migration story for legacy TKI and
equivalent identity coverage for methods and generics. It does not authorize
embedding CDW1 in a source comment, trusting a provider object, or changing
the bodyless `E04631` boundary.
