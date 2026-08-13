# Toka 1.0 Qualification Evidence Ledger

**Status:** Historical Qualification & Discovery Ledger. RC4 Tier 1 was
complete for recorded candidate revision
`01e6e88be4aef4593fd630355c9763755ed05bd4`; this is not current-HEAD
qualification.

**Authority hierarchy:**

- Frozen 1.0 language scope and exclusions are governed by
  [`1_0_freeze_decision_list.md`](1_0_freeze_decision_list.md). The linked
  closure plan and this ledger retain exact-revision historical evidence.
- Current-HEAD blockers and the active P-1 requalification gate are tracked in
  [`semantic_contract_evolution_roadmap_rfc.md`](semantic_core/semantic_contract_evolution_roadmap_rfc.md).
- This ledger tracks empirical evidence (conformance tests, benchmark logs,
  sanitizer build smoke, micro-slices) for its recorded production candidates.

---

## 1. CI Gate & Qualification Tiering

- **Pull Request Gate (`.github/workflows/ci.yml`)**: Automated on every PR and commit on `main`. Enforces Conformance Suite (14/14), Plaintext No-OpenSSL Script (`tools/test_no_openssl.sh`), and Runtime ASan Build Smoke (`tools/build_sanitized.sh runtime-asan`).
- **Release Candidate Gate (`.github/workflows/release.yml`)**: Validated for the `v0.9.9-rc4` dry-run candidate via `tools/scripts/release_gate.py` (13-stage release qualification across Linux x64/arm64 and macOS x64/arm64); evidence: [Run 30246461701](https://github.com/tokalang/toka/actions/runs/30246461701), source `01e6e88be4aef4593fd630355c9763755ed05bd4`.
- **Manual / Scheduled Sanitizer Gate (`tools/build_sanitized.sh`)**: `runtime-tsan` and `compiler-asan` are available for manual developer validation or dedicated scheduled builds.

### Data-access interoperability evidence

- **Real-service runner (`tools/scripts/qualify_data_access_real.py`)**:
  Docker-based maintainer and CI evidence for the Tier-4 `official/redis` and
  `official/postgres` compatibility rows. It exercises Redis 7.4.x/8.2.x
  (password TCP and private-CA TLS) and PostgreSQL 16.x/17.x (private-CA TLS
  and SCRAM-SHA-256), recording exact container patches in the
  `data-access-real-service` artifact. A successful local Docker execution is
  `maintainer-run green`; the Linux CI copy is the reproducible release-gate
  artifact, not a different execution mechanism.
- **Runner eligibility is evidence, not product behavior**: a missing Docker
  daemon or a restricted sandbox that returns `EPERM` while publishing a
  loopback service exits `2` as `not-run`; it is neither a passing row nor a
  product regression. A fixture/client failure exits `1` and retains its JSON
  report.
- **Scope**: `RedisPool` / `RedisLease` and `PostgresPool` /
  `PostgresPoolLease` remain dedicated package pools. The data-access service
  example is composition evidence, not a new generic `Pool<T>` or web
  framework contract. The complete matrix and release procedure are in
  [`data_access_real_service_compatibility_v1.md`](data_access_real_service_compatibility_v1.md).
- **Recorded maintainer evidence (2026-07-30, `c6cb0b73`)**: the full
  conformance runner completed `214 Passed, 0 Failed`; the TaskHandle
  lifecycle, cede-obligation, and H/P capability ABI gates passed; and
  `build/data-access-real-service.json` reported `status: passed`. The Docker
  matrix recorded Redis 7.4.10/8.2.8 (password TCP and private-CA TLS) and
  PostgreSQL 16.14/17.10 (private-CA TLS and SCRAM-SHA-256). This is
  `maintainer-run green`, not a substitute for the required Linux CI artifact.

---

## 2. Active Gap Inventory & Release Audit

### A. Language Semantics & Ownership

| ID | Title & Summary | Domain | Priority | Scope in 1.0? | Status | Evidence Reference |
|---|---|---|---|---|---|---|
| **GAP-LANG-01** | **`Vec::get` Move vs. Borrow Semantics**<br>Calling `.get(idx)` on `@Encap` element vectors moves/drops temporary elements unless `.clone()` or explicit borrow is used. | Language | `P1` | Yes | `closed` | [vec_get_clone.tk](file:///Users/zhyi/GitDP/toka/tests/conformance/ownership/vec_get_clone.tk) & `tests/conformance/manifest.json#ownership_vec_get_clone_01` |
| **GAP-LANG-02** | **Layered Diagnostic Conformance**<br>Lock stable diagnostic codes (`E0417`, `E0443`), levels, and line/col spans without freezing text formatting. | Language | `P1` | Yes | `closed` | [mut_borrow_err.tk](file:///Users/zhyi/GitDP/toka/tests/conformance/diagnostics/mut_borrow_err.tk) & `spec/diagnostic.map.json` |
| **GAP-LANG-03a** | **Async Frame Local Lifetime Across `.await`**<br>Local variables retained across `.await` points must preserve state and execute deterministic destructors on scope exit (`drop_count == 1`). | Language | `P0` | Yes | `closed` | [async_frame_drop_across_await.tk](file:///Users/zhyi/GitDP/toka/tests/conformance/async/async_frame_drop_across_await.tk) & `tests/conformance/manifest.json` |
| **GAP-LANG-03b** | **Async Task Cancellation Destructors**<br>Rich cancellation and exactly-once destructor invocation upon task handle cancellation. | Language | `P2` | Post-1.0 | `planned` | `docs/1_0_closure_plan.md#cancellation` |
| **GAP-LANG-04** | **Handle Identity / Payload Write Separation**<br>For an existing binding, effective authority is `declaration/signature capability ∩ use-site intent ∩ PAL permission`. A handle-side `#` (`*#p`, `^#p`, `~#p`, `&#p`) authorizes only rebinding that handle; it cannot authorize bare, member, indexed, call-argument, callable, or mutable-receiver payload writes. Shared flow additionally enforces `effective-P(LHS) = declared-P(LHS) ∩ effective-P(direct RHS)` without provenance traversal at local, call, return, field, match/guard, and destructuring declaration boundaries. Whole-unique `cede` creates a fresh root whose H/P comes from its declaration, subject to the existing `$` field ceiling and same-path nullable guard rules. | Language | `P0` | Yes | `closed` | Conformance: `handle_identity_not_payload_writable_*.tk`, `call_*_cannot_supply_*.tk`, `static_call_handle_only_cannot_supply_payload.tk`, `callable_argument_cannot_forge_payload.tk`, `raw_payload_write_requires_unsafe.tk`, `method_use_site_cannot_forge_payload.tk`, `ownership_call_permission_capability_matrix_01`, `ownership_callable_argument_permission_matrix_01`, `shared_view_cannot_amplify_payload*.tk`, `cede_shared_*rebind*.tk`, `reference_field_rebind_cannot_amplify_payload.tk`, `cede_unique_readonly_source_creates_writable_owner.tk`, and guarded-nullable cases. Source-less replay: `permission_001_capability` through `permission_005_partial_cede_lifecycle` (25/25 case set). Release: [RC4 Tier 1 run 30246461701](https://github.com/tokalang/toka/actions/runs/30246461701), all four targets passed. |

**Recorded direct-source PAL closure (2026-07-27, `7ccb649e`):** nested struct and enum
`match`/`guard` reference patterns, ordinary destructuring, and
fixed-array/protocol reference iteration now preserve their direct source
storage and register PAL borrows. Enum payloads conservatively register their
enclosing enum target because no separately nameable payload path exists.
`nested_pattern_reference_*`, `enum*_payload_reference_cede_conflict`,
`destructure_reference_*`, `for_reference_fixed_array_*`,
`cede_existing_handle_only_lhs_cannot_gain_payload_write`,
`cede_shared_existing_lhs_cannot_amplify_payload`, and
`cede_shared_field_rebind_*`, `reference_field_rebind_cannot_amplify_payload`,
`index_handle_rebind_is_not_a_1_0_surface`, and
`g08_iterator_pal_protocol.tk` provide positive/disjoint/conflicting evidence.
GAP-LANG-04 is **closed for the frozen 1.0 surface**: direct-source routing,
existing-LHS non-redeclaration, whole-unique fresh roots, the bounded `$`
field ceiling, and same-path nullable guards all have conformance and
source-less replay evidence. Indexed elements have payload and partial-`cede`
operations but no independent handle-rebind surface in 1.0. A general
freeze/sealed-object referent ceiling is not represented by frozen syntax and
is therefore a 1.x design proposal, not an unresolved 1.0 implementation
gap. The recorded RC4 candidate passed the Tier 1 multi-platform Release Review in
[run 30246461701](https://github.com/tokalang/toka/actions/runs/30246461701).

### B. Compiler & Lowering

| ID | Title & Summary | Domain | Priority | Scope in 1.0? | Status | Evidence Reference |
|---|---|---|---|---|---|---|
| **GAP-COMP-01** | **Large Generic Aggregate SRET Lowering**<br>Generic struct returns like `Option<Entry<'K, 'V>>` must lower via `sret` without LLVM physreg copy errors. | Compiler | `P1` | Yes | `closed` | [sret_abi.tk](file:///Users/zhyi/GitDP/toka/tests/conformance/codegen/sret_abi.tk) & `tests/conformance/manifest.json#ir_sret_abi_01` |

### C. Async Runtime & Memory Safety

| ID | Title & Summary | Domain | Priority | Scope in 1.0? | Status | Evidence Reference |
|---|---|---|---|---|---|---|
| **GAP-RNTM-01** | **Socket Fragmentation & Chunked Streaming**<br>TCP socket reads fragmenting across CRLF, chunk headers, and trailer headers must resume cleanly without memory leaks. | Runtime | `P0` | Yes | `closed` | `tests/pass/g12_stdx_http_client_server_test.tk#Chunked Server` |
| **GAP-RNTM-02** | **Instance-Scoped Wakeup & Race Safety**<br>WaitRegistry tokens must reject stale task instances and generations, publish one ready-queue entry under concurrent helpers, and select one terminal source under cancellation races. | Runtime | `P0` | Yes | `closed` | `tests/AsyncQueuePublication.c`, `tests/AsyncSuspendRollback.c`, `tests/AsyncIdentityExhaustion.c`, and `tests/AsyncColdCancelCleanup.c` |
| **GAP-RNTM-03** | **Sanitizer Build Smoke Verification**<br>Compile C runtime objects (`toka_rt.c`) and `tokac` compiler with AddressSanitizer and ThreadSanitizer flags. | Runtime | `P1` | Yes | `closed` | [build_sanitized.sh](file:///Users/zhyi/GitDP/toka/tools/build_sanitized.sh) & `.github/workflows/ci.yml` |

### D. Standard Library (`core`/`std`/`stdx`)

| ID | Title & Summary | Domain | Priority | Scope in 1.0? | Status | Evidence Reference |
|---|---|---|---|---|---|---|
| **GAP-STDL-01** | **Owner-Carrying Buffer Stealing (`Bytes` / `Vec`)**<br>`Bytes::from_vec(cede v)` and `b#.into_vec()` must transfer underlying buffers without heap allocation. | Stdlib | `P0` | Yes | `closed` | `tests/pass/g13_stdx_net_zero_copy_bench.tk` |
| **GAP-STDL-02** | **OpenSSL Optionality & Clean Plaintext Fallback**<br>Core plaintext HTTP and TCP applications MUST compile and run cleanly without OpenSSL enabled (`-UTOKA_HAS_OPENSSL`). | Stdlib | `P1` | Yes | `closed` | [test_no_openssl.sh](file:///Users/zhyi/GitDP/toka/tools/test_no_openssl.sh) & `docs/1_0_scope.md#capability` |
| **GAP-STDL-03** | **Safe Cede Ownership Transfer in `HeaderMap::clone`**<br>`return HeaderMap(entries = cede new_entries)` eliminates `unsafe_forget` bypasses. | Stdlib | `P1` | Yes | `closed` | [http.tk:105](file:///Users/zhyi/GitDP/toka/lib/stdx/net/http.tk#L105) & `tests/pass/g12_stdx_http_client_server_test.tk#test_headermap_clone_independence` |
| **GAP-STDL-04** | **OpenSSL 3 RSA Legacy API Migration**<br>Migrate legacy `RSA_generate_key` calls in C runtime to modern OpenSSL 3 `EVP_PKEY` keygen API. | Stdlib | `P2` | Post-1.0 | `planned` | [lib/sys/toka_rt.c:2726](file:///Users/zhyi/GitDP/toka/lib/sys/toka_rt.c#L2726) |

### E. Production Vertical Micro-Slices

| ID | Title & Summary | Domain | Priority | Scope in 1.0? | Status | Evidence Reference |
|---|---|---|---|---|---|---|
| **GAP-VSLC-01** | **HTTP/1.1, HTTPS & WSS Micro-Slice**<br>Async client/server streaming, chunked transfer, TLS security verification, and WebSocket framing. | Application | `P0` | Yes | `closed` | `tests/pass/g12_stdx_http_client_server_test.tk` & `g12_stdx_https_wss_test.tk` |
| **GAP-VSLC-02** | **Synchronous Filesystem Micro-Slice**<br>File open, synchronous read/write, path existence, and directory traversal. | Application | `P1` | Yes | `closed` | [01_filesystem_slice.tk](file:///Users/zhyi/GitDP/toka/demos/vertical_slices/01_filesystem_slice.tk) |
| **GAP-VSLC-03** | **Real `stdx/serde/json` Serde Micro-Slice**<br>Reflection-based JSON serialization, typed deserialization, and malformed error handling. | Application | `P1` | Yes | `closed` | [04_json_serde_slice.tk](file:///Users/zhyi/GitDP/toka/demos/vertical_slices/04_json_serde_slice.tk) |
| **GAP-VSLC-04** | **Subprocess & OS Execution Micro-Slice**<br>Command line argument parsing, subprocess spawning, stdout capture, and wait status. | Application | `P2` | Post-1.0 | `verified` | [03_subprocess_slice.tk](file:///Users/zhyi/GitDP/toka/demos/vertical_slices/03_subprocess_slice.tk) & `tests/conformance/manifest.json#subprocess_conformance_01` |

---

## 3. Freeze Gate Exit Criteria

Toka 1.0 will enter Freeze Mode only when:
1. All P0 and P1 gaps transition to status `closed` upon completion of `docs/1_0_release_review.md` backed by a Tier 1 multi-platform execution report for the current revision. RC3 is historical evidence only after GAP-LANG-04.
2. All declared production vertical micro-slices execute cleanly.
3. PR Gate and Release Candidate Gate runs pass cleanly without regression.
