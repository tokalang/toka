# Semantic Manifest Envelope P2 Execution Plan

**Status:** P2.0--P2.3 implemented and qualified for the stated local
Outcome subset. P2 extends the P1 declaration-comparison transport with one
narrow, body-derived Outcome fulfilment record. It is not a general proof
format, package-signature system, or a change to ordinary source-less replay.

## Objective

P2 closes one Level-B path end to end:

```text
checked source Outcome provider
  + compiler-local provenance key
  + exact emitted object
    -> object-bound `.tki.tsm` attestation
    -> bodyless source-less import under an explicit profile
    -> revalidation immediately before the same compiler invocation links
```

The admitted provider remains the existing P1 Outcome subset: a known
resolver coordinate, top-level non-generic function, one whole-place `init`
formal, and direct nominal return-enum cases. P2 adds no source spelling and
does not broaden caller authority beyond that already reconstructed from the
declaration.

## Provenance boundary

P2's initial accepted provenance is a compiler-local state directory supplied
explicitly by `--semantic-manifest-provenance-dir <absolute-dir>`. The
compiler creates an owner-only random key there and signs a canonical P2
attestation with HMAC-SHA-256. The state key is not emitted beside a package,
TKI, object, or build cache; a copied sidecar without the accepting compiler's
state key is not provenance.

This is deliberately a **local receipt** profile. It lets one compiler owner
reuse a checked provider object safely across source-less builds. It does not
claim to authenticate a distributable third-party package; a public-key or
registry-backed producer policy is later work and cannot be smuggled in as a
path, metadata field, or manifest label.

## P2 envelope

P2 replaces the P1 sidecar for an attested provider with version `2` of the
same compiler-owned `.tki.tsm` carrier. Its fixed payload schema is
`toka.outcome-fulfilment-p2`. It contains:

- the P1 identity fields and replay-surface dependency-closure digest;
- exactly one declaration-recomputed `outcome-transition` record and one
  `outcome-fulfilment` record for every admitted CDW1 subject;
- the exact object SHA-256;
- a `TOKASMAN2:<payload-digest>` marker retained in that object; and
- a consumer-owned `local-hmac-v1` provenance classification, key identity,
  and signature over the canonical interface, closure, payload, object, and
  marker binding.

The consumer rejects the complete record set on any unknown field, version,
classification, duplicate subject, non-canonical byte sequence, interface or
closure mismatch, marker/object mismatch, unavailable key, or invalid
signature. A CDW1 pair must be identical: the declaration record states the
contract, while the fulfilment record says that the producer compiler checked
the corresponding body before emitting the exact object.

## Link-obligation boundary

Validation before semantic analysis is only a provisional candidate so that
the caller can follow the normal direct-match Outcome flow. After Sema has
reconstructed all CDW1 declarations, the compiler atomically validates the
full P2 envelope before CodeGen. It then retains a link obligation containing
the resolved module, `.tki`, `.tsm`, expected object, expected records,
target, closure, and accepting provenance state.

Immediately before LLD receives inputs, the compiler reloads and validates
each obligation and verifies that the exact canonical object path is among the
selected linker inputs. This closes object replacement after import. A
compile-only consumer cannot carry that obligation to a future arbitrary
linker invocation, so P2 rejects that use rather than pretending the check
survives. P2 likewise does not admit a provider whose own semantic fulfilment
depends on another bodyless P2 provider; Level-A retained-body dependencies
remain covered by the existing replay-surface closure. Recursive P2 object
obligations require a later payload revision with a persisted consumer-link
contract.

## Slices and acceptance gates

### P2.0 — schema, marker, and local-provenance codec

Implement strict version-2 canonical encoding/decoding, state-key creation
and verification, exact object digest/marker binding, and unit tamper cases.
No importer accepts a bodyless provider in this slice.

### P2.1 — producer evidence

For a compile-only source provider with `--emit-interface`, prepare the
attestation from checked `OutcomeTransition` IR, retain the payload marker in
the object, then write the final sidecar only after object emission. P1 output
is unchanged when no provenance directory is requested.

### P2.2 — importer and final-link consumer

`--validate-semantic-manifest-attestations` requires a provenance directory.
It permits only a provisionally validated bodyless Outcome interface, checks
its declaration-reconstructed record set after Sema, rejects standalone or
unbound interfaces, and revalidates exact selected objects immediately before
linking. Default imports and `--validate-semantic-manifests` keep their P1
and Level-A behavior.

### P2.3 — build propagation and qualification

Propagate both P2 options through `toka build`, including the clean-plan
check. The qualification runner covers valid source/retained-body/bodyless
attested parity; missing, malformed, relabelled, stale-interface,
closure-mismatched, payload-mismatched, key/signature, marker, and object
failures; ordinary third-party TKI rejection; the compile-only-consumer
redline; and final-link object-input omission.

**Implemented evidence:** `toka build` forwards both options through its
build-program compilation and incremental driver. The driver performs the
same no-write `tokac --check-only` preflight under the P2 profile before a
clean plan can return, and enables compiler-cache selection only for that
explicit profile. `test_semantic_manifest_attestation.sh` covers the direct
producer/importer/link path plus structural, classification, object, marker,
provenance, default-Level-A, compile-only, and omitted-link-input failures.
`test_semantic_manifest_attestation_build.sh` covers producer-to-clean-build
propagation and rejects a tampered cached provider object with `E04634`.

## Explicit exclusions

- no default enablement, package syntax, lockfile assertion, or public key
  distribution policy;
- no methods, generic Outcome functions, field paths, init contracts, unsafe
  wrappers, async cleanup, or multiple body-derived record kinds;
- no acceptance of an arbitrary standalone `.tki + .o + .tsm` merely because
  it has matching hashes; and
- no deferred or external linker accepting a P2 caller without a persisted,
  revalidated obligation.

## P2 completion boundary

P2 is complete when the local compiler-state provenance key is shared by the
producer and accepting build. A copied `.tki + .o + .tsm` without that key
fails closed; ordinary source-less replay still reports `E04631`. The next
payload revision, if needed, must separately design a distributable producer
identity (for example a public-key or registry policy), persisted recursive
link obligations, and equivalent method/generic identities. None of those are
implicit in the local HMAC receipt.
