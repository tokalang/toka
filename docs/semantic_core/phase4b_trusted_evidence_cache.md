# Phase 4B Trusted Memory Evidence Cache

Phase 4B implements the object-bound sidecar frozen in Phase 4A. It exports and
validates cached memory summaries but deliberately does not activate them for
backend contracts yet.

## Export

Every successful compile-only build that emits an interface also writes a
`.tke` sidecar after the backing object has been finalized. The exporter:

- includes only source-body summaries belonging to the root module;
- sorts functions and roots deterministically;
- rejects conflicting records for one LLVM symbol;
- records compiler/interface versions, target, and source hash;
- binds the complete sidecar to the backing object with SHA-256; and
- publishes through a temporary file so consumers never observe partial JSON.

The sidecar is replaced on every successful object rebuild. A failed export
fails that compile rather than leaving a new object paired with stale evidence.

## Import

The resolver attempts evidence loading only for a selected compiler build-cache
interface with a backing object. Validation is all-or-nothing and produces one
stable status:

- `NotApplicable`
- `Missing`
- `ReadError`
- `InvalidSchema`
- `IdentityMismatch`
- `MissingObject`
- `ObjectMismatch`
- `InvalidRecord`
- `Valid`

Only `Valid` attaches candidate summaries to the parsed module. All other
states leave the candidate map empty and compilation continues through the
ordinary interface path.

`--dump-dependencies=json` exposes `memory_evidence_status` and
`memory_evidence_reason` for cache diagnosis. These are internal build facts,
not source-visible semantics.

## Inactive-by-default Gate

Loaded candidates are stored separately from each function's active
`MemorySummary`. Phase 4B does not replace `SignatureOnly`, alter call-effect
propagation, or emit an LLVM attribute from cache evidence. This separation is
intentional: Phase 4C first proves every replay and downgrade path, and Phase
4D then activates only the experimental `nocapture` consumer.

The `MemorySummaryOrigin::TrustedCache` value is reserved for that explicit
activation. It is serialized by the existing memory-summary dump as
`trusted_cache` once active.
