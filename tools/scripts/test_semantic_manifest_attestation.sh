#!/usr/bin/env bash
# Verify P2 producer emission before any importer is allowed to consume it.

set -euo pipefail

TOKAC="${TOKAC:-./build/bin/tokac}"
CASE_DIR="tests/semantics/tki_replay/cases/outcome_001_direct_match"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/toka_semantic_manifest_p2.XXXXXX")"
trap 'rm -rf "$TEST_DIR"' EXIT

cp "$CASE_DIR/lib.tk" "$TEST_DIR/lib.tk"
mkdir "$TEST_DIR/state"

"$TOKAC" --workspace-node semantic-manifest-p2-test \
    --workspace-root "$TEST_DIR" \
    --semantic-manifest-provenance-dir "$TEST_DIR/state" \
    -c "$TEST_DIR/lib.tk" -o "$TEST_DIR/lib.o"

for artifact in "$TEST_DIR/lib.o" "$TEST_DIR/lib.tki" \
                "$TEST_DIR/lib.tki.tsm" \
                "$TEST_DIR/state/toka-semantic-manifest-p2-local.key"; do
    if [[ ! -f "$artifact" ]]; then
        echo "FAIL: P2 producer did not emit $artifact" >&2
        exit 1
    fi
done

if grep -Fq 'init out = 42:i32' "$TEST_DIR/lib.tki"; then
    echo "FAIL: P2 producer retained a provider body in its attested TKI" >&2
    exit 1
fi

if ! grep -Fq '"version":2' "$TEST_DIR/lib.tki.tsm" ||
   ! grep -Fq '"payload_schema":"toka.outcome-fulfilment-p2"' \
       "$TEST_DIR/lib.tki.tsm" ||
   ! grep -Fq '"kind":"outcome-fulfilment"' "$TEST_DIR/lib.tki.tsm" ||
   ! grep -Fq '"kind":"local-hmac-v1"' "$TEST_DIR/lib.tki.tsm"; then
    echo "FAIL: P2 producer sidecar did not contain the fixed attestation schema" >&2
    exit 1
fi

marker=$(sed -n 's/.*"marker":"\(TOKASMAN2:[0-9a-f]*\)".*/\1/p' \
    "$TEST_DIR/lib.tki.tsm")
if [[ -z "$marker" ]] || ! grep -aFq "$marker" "$TEST_DIR/lib.o"; then
    echo "FAIL: P2 payload marker was not retained in the backing object" >&2
    exit 1
fi

cp "$TEST_DIR/lib.tki.tsm" "$TEST_DIR/first.tsm"
"$TOKAC" --workspace-node semantic-manifest-p2-test \
    --workspace-root "$TEST_DIR" \
    --semantic-manifest-provenance-dir "$TEST_DIR/state" \
    -c "$TEST_DIR/lib.tk" -o "$TEST_DIR/lib.o"
if ! cmp -s "$TEST_DIR/first.tsm" "$TEST_DIR/lib.tki.tsm"; then
    echo "FAIL: P2 producer sidecar was not deterministic" >&2
    exit 1
fi

# Existing P1 output remains unchanged when the provenance profile is absent.
"$TOKAC" --workspace-node semantic-manifest-p2-test \
    --workspace-root "$TEST_DIR" \
    -c "$TEST_DIR/lib.tk" -o "$TEST_DIR/p1.o"
if ! grep -Fq '"payload_schema":"toka.cdw1-recomputed-v1"' \
    "$TEST_DIR/p1.tki.tsm"; then
    echo "FAIL: P2 producer option changed ordinary P1 sidecar emission" >&2
    exit 1
fi

# A P2 provider is emitted into the compiler build cache so the resolver and
# linker both select the same known backing object. Its generated TKI is
# bodyless; the explicit profile must accept it only with the local receipt.
BUILD_DIR="$TEST_DIR/build"
mkdir -p "$BUILD_DIR/objects" "$BUILD_DIR/interfaces"
rm -f "$TEST_DIR/lib.tki" "$TEST_DIR/lib.tki.tsm"
provider_key=$(python3 - "$TEST_DIR/lib.tk" <<'PY'
import os
import sys

value = 14695981039346656037
for byte in os.path.realpath(sys.argv[1]).encode():
    value ^= byte
    value = (value * 1099511628211) & 0xffffffffffffffff
print(f"{value:016x}")
PY
)
provider_object="$BUILD_DIR/objects/$provider_key.o"
provider_interface="$BUILD_DIR/interfaces/$provider_key.tki"
TOKA_BUILD_DIR="$BUILD_DIR" "$TOKAC" \
    --workspace-node semantic-manifest-p2-test \
    --workspace-root "$TEST_DIR" \
    --semantic-manifest-provenance-dir "$TEST_DIR/state" \
    -c "$TEST_DIR/lib.tk" -o "$provider_object"
if [[ ! -f "$provider_interface" || ! -f "$provider_interface.tsm" ]]; then
    echo "FAIL: P2 cache provider artifacts are incomplete" >&2
    exit 1
fi
if grep -Fq 'init out = 42:i32' "$provider_interface"; then
    echo "FAIL: P2 cache provider retained a body" >&2
    exit 1
fi

cp "$CASE_DIR/pass_replay.tk" "$TEST_DIR/main.tk"
TOKA_BUILD_DIR="$BUILD_DIR" TOKA_USE_LIB_CACHE=1 "$TOKAC" \
    --validate-semantic-manifest-attestations \
    --workspace-node semantic-manifest-p2-test \
    --workspace-root "$TEST_DIR" \
    --semantic-manifest-provenance-dir "$TEST_DIR/state" \
    "$TEST_DIR/main.tk" "$provider_object" -o "$TEST_DIR/p2-app"
if ! "$TEST_DIR/p2-app"; then
    echo "FAIL: attested bodyless Outcome provider changed runtime behavior" >&2
    exit 1
fi

# The ordinary source-less path remains Level A only; P2 is never implicit.
if TOKA_BUILD_DIR="$BUILD_DIR" TOKA_USE_LIB_CACHE=1 "$TOKAC" \
    --workspace-node semantic-manifest-p2-test \
    --workspace-root "$TEST_DIR" \
    "$TEST_DIR/main.tk" "$provider_object" -o "$TEST_DIR/default-app" \
    >"$TEST_DIR/default.out" 2>"$TEST_DIR/default.err"; then
    echo "FAIL: bodyless Outcome provider was accepted without P2 profile" >&2
    exit 1
fi
if ! grep -Fq 'E04631' "$TEST_DIR/default.err"; then
    echo "FAIL: default bodyless Outcome rejection lost E04631" >&2
    exit 1
fi

# A compile-only consumer cannot retain P2's final-link obligation for a
# later arbitrary linker process.
if TOKA_BUILD_DIR="$BUILD_DIR" TOKA_USE_LIB_CACHE=1 "$TOKAC" \
    --validate-semantic-manifest-attestations \
    --workspace-node semantic-manifest-p2-test \
    --workspace-root "$TEST_DIR" \
    --semantic-manifest-provenance-dir "$TEST_DIR/state" \
    -c "$TEST_DIR/main.tk" "$provider_object" \
    -o "$TEST_DIR/p2-consumer.o" \
    >"$TEST_DIR/compile-only.out" 2>"$TEST_DIR/compile-only.err"; then
    echo "FAIL: compile-only P2 consumer retained no link obligation" >&2
    exit 1
fi
if ! grep -Fq 'E04634' "$TEST_DIR/compile-only.err" ||
   ! grep -Fq 'same compiler invocation' "$TEST_DIR/compile-only.err"; then
    echo "FAIL: compile-only P2 redline was not explicit" >&2
    sed 's/^/  | /' "$TEST_DIR/compile-only.err" >&2
    exit 1
fi

# The final-link gate rechecks that the resolver-selected object actually
# reaches LLD; cache adjacency is not enough.
if TOKA_BUILD_DIR="$BUILD_DIR" TOKA_USE_LIB_CACHE=1 "$TOKAC" \
    --validate-semantic-manifest-attestations \
    --workspace-node semantic-manifest-p2-test \
    --workspace-root "$TEST_DIR" \
    --semantic-manifest-provenance-dir "$TEST_DIR/state" \
    "$TEST_DIR/main.tk" -o "$TEST_DIR/missing-object-app" \
    >"$TEST_DIR/missing-object.out" 2>"$TEST_DIR/missing-object.err"; then
    echo "FAIL: P2 link gate accepted an omitted provider object" >&2
    exit 1
fi
if ! grep -Fq 'E04634' "$TEST_DIR/missing-object.err" ||
   ! grep -Fq 'absent from final linker inputs' "$TEST_DIR/missing-object.err"; then
    echo "FAIL: P2 link gate did not reject omitted provider object" >&2
    exit 1
fi

echo "PASS: semantic manifest P2 producer emission"
