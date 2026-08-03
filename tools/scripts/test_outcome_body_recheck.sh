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
cp "$TEST_DIR/lib.tki" "$TEST_DIR/lib.tki.good"

# Keep the signature and outcome declaration, but remove the retained
# provider body.  This models a bodyless third-party TKI, which cannot
# establish Outcome fulfilment yet.
sed -n '1,/^    Err => out: uninit$/p' "$TEST_DIR/lib.tki" \
    > "$TEST_DIR/lib.tki.stripped"
mv "$TEST_DIR/lib.tki.stripped" "$TEST_DIR/lib.tki"
mv "$TEST_DIR/lib.tk" "$TEST_DIR/lib.tk.source-hidden"

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

echo "PASS: outcome source-less body recheck"
