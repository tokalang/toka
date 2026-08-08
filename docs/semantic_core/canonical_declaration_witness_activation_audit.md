# CDW1 Activation Preflight Audit

**Status:** Preflight complete; importer activation is blocked by deliberate
artifact and trust gates. CDW1 remains an audit-only codec and changes no
caller, cleanup, or object-link authority.

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
| importer-visible declaration comparison | absent by design | no production consumer reads CDW1 |
| independent semantic envelope carrier | absent | required before activation |
| atomic declaration/tamper failure matrix | absent | required before activation |

## Artifact boundary

```text
resolved declaration
  -> CDW1 encoder
  -> @tki v2 cdw1: audit comment
  -> lexer discards comment
  -> retained-body Level-A recheck
```

`InterfaceV2Facts` transports CDW1 only as an ignorable comment. The importer
has no raw-CDW1 input to compare, which is intentional: a provider-controlled
comment cannot become an authority carrier merely by being parsed later.

Resolver-owned module coordinates are available to the admitted P1 encoder,
but current `.tki` metadata is not an accepted-provenance semantic manifest
and does not bind a declaration record to an exact provider object.

## Consequence

CDW1 must **not** be promoted in place. A production activation needs a
separate compiler-owned envelope that carries the raw record outside ordinary
TKI comments. For the recomputed-declaration class, an importer would then:

1. obtain the envelope through resolver-owned module identity;
2. strictly decode exactly one required CDW1 record;
3. reconstruct CDW1 from the imported declaration;
4. compare canonical bytes atomically; and
5. reject absence, duplication, mismatch, unknown required data, or a stale
   envelope without retaining any partial semantic fact.

That comparison still cannot accept a bodyless Outcome provider. Its only
claim is agreement about a declaration fact; Level-B fulfilment needs the
separate accepted-provenance and exact-object-binding design in the Semantic
Manifest Envelope RFC.

## Remaining design decision

The next implementation proposal must choose the envelope carrier and
compatibility policy before modifying the importer:

- an explicit compiler-owned sidecar resolved with the module, initially for
  the known-coordinate, top-level, non-generic P1 subset; or
- defer all importer activation while expanding generic/method identities and
  then design the envelope at a broader boundary.

Neither option authorizes embedding CDW1 in a source comment, trusting a
provider object, or changing the bodyless `E04631` boundary.
