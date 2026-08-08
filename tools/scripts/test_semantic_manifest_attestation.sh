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

echo "PASS: semantic manifest P2 producer emission"
