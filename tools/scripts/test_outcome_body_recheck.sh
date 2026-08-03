#!/usr/bin/env bash
# Verify that source-less Outcome Contracts are rechecked from a retained body.

set -euo pipefail

TOKAC="${TOKAC:-./build/bin/tokac}"
CASE_DIR="tests/semantics/tki_replay/cases/outcome_001_direct_match"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/toka_outcome_recheck.XXXXXX")"
trap 'rm -rf "$TEST_DIR"' EXIT

cp "$CASE_DIR/lib.tk" "$TEST_DIR/lib.tk"
cp "$CASE_DIR/pass_replay.tk" "$TEST_DIR/main.tk"

"$TOKAC" -c "$TEST_DIR/lib.tk" -o "$TEST_DIR/lib.o"

if ! grep -Fq "init out = 42:i32" "$TEST_DIR/lib.tki"; then
    echo "FAIL: outcome provider body was not retained in its interface" >&2
    exit 1
fi
if ! grep -Fq "coordinate=unbound;" "$TEST_DIR/lib.tki"; then
    echo "FAIL: ordinary local Outcome audit did not remain coordinate-unbound" >&2
    exit 1
fi

# A resolver-owned workspace coordinate makes the narrow declaration fact
# eligible for a future witness schema.  The audit marker is still not parsed
# or trusted by the importer.
mkdir -p "$TEST_DIR/known"
cp "$CASE_DIR/lib.tk" "$TEST_DIR/known/lib.tk"
"$TOKAC" --workspace-node outcome-cdw-test --workspace-root "$TEST_DIR" \
    -c "$TEST_DIR/known/lib.tk" -o "$TEST_DIR/known/lib.o"
if ! grep -Fq "coordinate=known;" "$TEST_DIR/known/lib.tki"; then
    echo "FAIL: resolver-known Outcome audit did not report a known coordinate" >&2
    exit 1
fi

cp "$TEST_DIR/lib.tki" "$TEST_DIR/lib.tki.good"
mv "$TEST_DIR/lib.tk" "$TEST_DIR/lib.tk.source-hidden"
mv "$TEST_DIR/known/lib.tk" "$TEST_DIR/known/lib.tk.source-hidden"
cp "$CASE_DIR/pass_replay.tk" "$TEST_DIR/known/main.tk"

# The audit identity is recomputed from declarations during source-less TKI
# replay.  It must not depend on AST addresses or the provider source path.
"$TOKAC" -c "$TEST_DIR/lib.tki" -o "$TEST_DIR/replayed.o"
grep '^// @tki v2 outcome_transition:' "$TEST_DIR/lib.tki.good" \
    > "$TEST_DIR/source.identity"
grep '^// @tki v2 outcome_transition:' "$TEST_DIR/replayed.tki" \
    > "$TEST_DIR/replayed.identity"
if ! cmp -s "$TEST_DIR/source.identity" "$TEST_DIR/replayed.identity"; then
    echo "FAIL: Outcome identity changed across source-less TKI replay" >&2
    exit 1
fi

"$TOKAC" --workspace-node outcome-cdw-test --workspace-root "$TEST_DIR" \
    -c "$TEST_DIR/known/lib.tki" -o "$TEST_DIR/known/replayed.o"
grep '^// @tki v2 outcome_transition:' "$TEST_DIR/known/lib.tki" \
    > "$TEST_DIR/known/source.identity"
grep '^// @tki v2 outcome_transition:' "$TEST_DIR/known/replayed.tki" \
    > "$TEST_DIR/known/replayed.identity"
if ! cmp -s "$TEST_DIR/known/source.identity" \
    "$TEST_DIR/known/replayed.identity"; then
    echo "FAIL: known-coordinate Outcome identity changed across source-less TKI replay" >&2
    exit 1
fi

# A known coordinate is only an audit boundary.  It cannot turn a bodyless
# interface into an accepted Outcome provider.
sed -n '1,/^    Err => out: uninit$/p' "$TEST_DIR/known/lib.tki" \
    > "$TEST_DIR/known/lib.tki.stripped"
mv "$TEST_DIR/known/lib.tki.stripped" "$TEST_DIR/known/lib.tki"
if "$TOKAC" --workspace-node outcome-cdw-test --workspace-root "$TEST_DIR" \
    -c "$TEST_DIR/known/main.tk" -o "$TEST_DIR/known/main.o" \
    > "$TEST_DIR/known/bodyless.out" 2> "$TEST_DIR/known/bodyless.err"; then
    echo "FAIL: known-coordinate bodyless Outcome interface unexpectedly compiled" >&2
    exit 1
fi
if ! grep -Fq "E04631" "$TEST_DIR/known/bodyless.err"; then
    echo "FAIL: known-coordinate bodyless Outcome interface missed E04631" >&2
    sed 's/^/  | /' "$TEST_DIR/known/bodyless.err" >&2
    exit 1
fi

# Keep the signature and outcome declaration, but remove the retained
# provider body.  This models a bodyless third-party TKI, which cannot
# establish Outcome fulfilment yet.
sed -n '1,/^    Err => out: uninit$/p' "$TEST_DIR/lib.tki" \
    > "$TEST_DIR/lib.tki.stripped"
mv "$TEST_DIR/lib.tki.stripped" "$TEST_DIR/lib.tki"

if "$TOKAC" -c "$TEST_DIR/main.tk" -o "$TEST_DIR/main.o" \
    > "$TEST_DIR/out" 2> "$TEST_DIR/err"; then
    echo "FAIL: bodyless Outcome interface unexpectedly compiled" >&2
    exit 1
fi

if ! grep -Fq "E04631" "$TEST_DIR/err"; then
    echo "FAIL: bodyless Outcome interface missed E04631" >&2
    sed 's/^/  | /' "$TEST_DIR/err" >&2
    exit 1
fi

# A retained body is semantic input, not decorative source.  Replacing its
# successful construction with a bare return must fail the callee proof.
cp "$TEST_DIR/lib.tki.good" "$TEST_DIR/lib.tki"
sed 's/init out = 42:i32//' "$TEST_DIR/lib.tki" \
    > "$TEST_DIR/lib.tki.tampered"
mv "$TEST_DIR/lib.tki.tampered" "$TEST_DIR/lib.tki"

if "$TOKAC" -c "$TEST_DIR/main.tk" -o "$TEST_DIR/main.o" \
    > "$TEST_DIR/tampered.out" 2> "$TEST_DIR/tampered.err"; then
    echo "FAIL: tampered Outcome provider body unexpectedly compiled" >&2
    exit 1
fi

if ! grep -Fq "E04628" "$TEST_DIR/tampered.err"; then
    echo "FAIL: tampered Outcome provider body missed E04628" >&2
    sed 's/^/  | /' "$TEST_DIR/tampered.err" >&2
    exit 1
fi

echo "PASS: outcome source-less body recheck and coordinate audit"
