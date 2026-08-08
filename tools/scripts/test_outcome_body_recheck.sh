#!/usr/bin/env bash
# Verify that source-less Outcome Contracts are rechecked from a retained body.

set -euo pipefail

TOKAC="${TOKAC:-./build/bin/tokac}"
CDW1_CHECK="${CDW1_CHECK:-./build/bin/toka_canonical_declaration_witness}"
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
if ! grep -Fq "type-domain=unavailable;" "$TEST_DIR/lib.tki"; then
    echo "FAIL: coordinate-unbound Outcome audit did not close its type domain" >&2
    exit 1
fi
if grep -q '^// @tki v2 cdw1:' "$TEST_DIR/lib.tki"; then
    echo "FAIL: coordinate-unbound Outcome interface emitted a CDW1 prototype" >&2
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
if ! grep -Fq "type-domain=canonical-v1;" "$TEST_DIR/known/lib.tki"; then
    echo "FAIL: concrete known-coordinate Outcome types were not canonicalized" >&2
    exit 1
fi
if [[ "$(grep -c '^// @tki v2 cdw1:' "$TEST_DIR/known/lib.tki")" != "1" ]]; then
    echo "FAIL: known-coordinate Outcome interface did not emit exactly one CDW1 prototype" >&2
    exit 1
fi
if ! grep -Eq '^// @tki v2 cdw1: 746f6b612e6465636c61726174696f6e2d7769746e65737300000100000001' \
    "$TEST_DIR/known/lib.tki"; then
    echo "FAIL: CDW1 prototype missed the canonical magic, version, or record count" >&2
    exit 1
fi

# A nominal init formal must carry its defining coordinate inside the
# candidate type identity, and preserve it across source-less replay.
mkdir -p "$TEST_DIR/nominal"
cp "$CASE_DIR/lib_nominal.tk" "$TEST_DIR/nominal/lib.tk"
"$TOKAC" --workspace-node outcome-cdw-test --workspace-root "$TEST_DIR" \
    -c "$TEST_DIR/nominal/lib.tk" -o "$TEST_DIR/nominal/lib.o"
if ! grep -Fq "type-domain=canonical-v1;" "$TEST_DIR/nominal/lib.tki"; then
    echo "FAIL: known nominal Outcome formal missed canonical type domain" >&2
    exit 1
fi
if ! grep -Fq "name=6:Packet;" "$TEST_DIR/nominal/lib.tki"; then
    echo "FAIL: canonical Outcome type identity missed nominal Packet definition" >&2
    exit 1
fi

# Strong aliases are not silently encoded as their temporary synthetic shape.
# Until aliases receive a stable definition identity, the P1 type domain must
# reject them even under an otherwise known workspace coordinate.
mkdir -p "$TEST_DIR/strong-alias"
cp "$CASE_DIR/lib_strong_alias.tk" "$TEST_DIR/strong-alias/lib.tk"
"$TOKAC" --workspace-node outcome-cdw-test --workspace-root "$TEST_DIR" \
    -c "$TEST_DIR/strong-alias/lib.tk" -o "$TEST_DIR/strong-alias/lib.o"
if ! grep -Fq "type-domain=unavailable;" \
    "$TEST_DIR/strong-alias/lib.tki"; then
    echo "FAIL: strong-alias Outcome type was silently admitted to P1" >&2
    exit 1
fi
if grep -q '^// @tki v2 cdw1:' "$TEST_DIR/strong-alias/lib.tki"; then
    echo "FAIL: strong-alias Outcome interface emitted a CDW1 prototype" >&2
    exit 1
fi

cp "$TEST_DIR/lib.tki" "$TEST_DIR/lib.tki.good"
mv "$TEST_DIR/lib.tk" "$TEST_DIR/lib.tk.source-hidden"
mv "$TEST_DIR/known/lib.tk" "$TEST_DIR/known/lib.tk.source-hidden"
mv "$TEST_DIR/nominal/lib.tk" "$TEST_DIR/nominal/lib.tk.source-hidden"
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
grep '^// @tki v2 cdw1:' "$TEST_DIR/known/lib.tki" \
    > "$TEST_DIR/known/source.cdw1"
grep '^// @tki v2 cdw1:' "$TEST_DIR/known/replayed.tki" \
    > "$TEST_DIR/known/replayed.cdw1"
if ! cmp -s "$TEST_DIR/known/source.cdw1" "$TEST_DIR/known/replayed.cdw1"; then
    echo "FAIL: CDW1 prototype changed across source-less TKI replay" >&2
    exit 1
fi
# The test-only codec reads the exported bytes after TKI replay has ignored the
# comment. Source bytes, standalone canonical decoding, and the declaration-
# reconstructed source-less bytes must all agree.
sed 's/^\/\/ @tki v2 cdw1: //' "$TEST_DIR/known/source.cdw1" \
    > "$TEST_DIR/known/source.cdw1.hex"
sed 's/^\/\/ @tki v2 cdw1: //' "$TEST_DIR/known/replayed.cdw1" \
    > "$TEST_DIR/known/replayed.cdw1.hex"
"$CDW1_CHECK" --hex-file "$TEST_DIR/known/source.cdw1.hex" \
    > "$TEST_DIR/known/decoded.cdw1.hex"
if ! cmp -s "$TEST_DIR/known/decoded.cdw1.hex" \
    "$TEST_DIR/known/replayed.cdw1.hex"; then
    echo "FAIL: standalone CDW1 codec disagreed with declaration replay" >&2
    exit 1
fi

"$TOKAC" --workspace-node outcome-cdw-test --workspace-root "$TEST_DIR" \
    -c "$TEST_DIR/nominal/lib.tki" -o "$TEST_DIR/nominal/replayed.o"
grep '^// @tki v2 outcome_transition:' "$TEST_DIR/nominal/lib.tki" \
    > "$TEST_DIR/nominal/source.identity"
grep '^// @tki v2 outcome_transition:' "$TEST_DIR/nominal/replayed.tki" \
    > "$TEST_DIR/nominal/replayed.identity"
if ! cmp -s "$TEST_DIR/nominal/source.identity" \
    "$TEST_DIR/nominal/replayed.identity"; then
    echo "FAIL: nominal Outcome type identity changed across source-less TKI replay" >&2
    exit 1
fi
grep '^// @tki v2 cdw1:' "$TEST_DIR/nominal/lib.tki" \
    > "$TEST_DIR/nominal/source.cdw1"
grep '^// @tki v2 cdw1:' "$TEST_DIR/nominal/replayed.tki" \
    > "$TEST_DIR/nominal/replayed.cdw1"
if ! cmp -s "$TEST_DIR/nominal/source.cdw1" "$TEST_DIR/nominal/replayed.cdw1"; then
    echo "FAIL: nominal CDW1 prototype changed across source-less TKI replay" >&2
    exit 1
fi

# CDW1 is transported as an audit comment only. A malformed payload cannot
# affect source-less retained-body replay or caller acceptance.
cp "$TEST_DIR/known/lib.tki" "$TEST_DIR/known/lib.tki.cdw.good"
sed '/^\/\/ @tki v2 cdw1:/s|.*|// @tki v2 cdw1: malformed|' \
    "$TEST_DIR/known/lib.tki" > "$TEST_DIR/known/lib.tki.cdw.tampered"
mv "$TEST_DIR/known/lib.tki.cdw.tampered" "$TEST_DIR/known/lib.tki"
sed 's/^\/\/ @tki v2 cdw1: //' "$TEST_DIR/known/lib.tki" \
    > "$TEST_DIR/known/malformed.cdw1.hex"
if "$CDW1_CHECK" --hex-file "$TEST_DIR/known/malformed.cdw1.hex" \
    > "$TEST_DIR/known/malformed.cdw1.out" \
    2> "$TEST_DIR/known/malformed.cdw1.err"; then
    echo "FAIL: standalone CDW1 codec accepted malformed audit bytes" >&2
    exit 1
fi
if ! "$TOKAC" --workspace-node outcome-cdw-test --workspace-root "$TEST_DIR" \
    -c "$TEST_DIR/known/main.tk" -o "$TEST_DIR/known/comment-ignored.o" \
    > "$TEST_DIR/known/comment-ignored.out" \
    2> "$TEST_DIR/known/comment-ignored.err"; then
    echo "FAIL: malformed CDW1 audit comment changed caller acceptance" >&2
    sed 's/^/  | /' "$TEST_DIR/known/comment-ignored.err" >&2
    exit 1
fi
mv "$TEST_DIR/known/lib.tki.cdw.good" "$TEST_DIR/known/lib.tki"

# Missing and repeated audit comments are equally non-authoritative.  They
# cannot change retained-body source-less caller acceptance before a distinct
# manifest carrier and atomic declaration-comparison gate exist.
cp "$TEST_DIR/known/lib.tki" "$TEST_DIR/known/lib.tki.cdw.good"
sed '/^\/\/ @tki v2 cdw1:/d' "$TEST_DIR/known/lib.tki" \
    > "$TEST_DIR/known/lib.tki.cdw.missing"
mv "$TEST_DIR/known/lib.tki.cdw.missing" "$TEST_DIR/known/lib.tki"
if ! "$TOKAC" --workspace-node outcome-cdw-test --workspace-root "$TEST_DIR" \
    -c "$TEST_DIR/known/main.tk" -o "$TEST_DIR/known/comment-missing.o" \
    > "$TEST_DIR/known/comment-missing.out" \
    2> "$TEST_DIR/known/comment-missing.err"; then
    echo "FAIL: missing CDW1 audit comment changed caller acceptance" >&2
    sed 's/^/  | /' "$TEST_DIR/known/comment-missing.err" >&2
    exit 1
fi
mv "$TEST_DIR/known/lib.tki.cdw.good" "$TEST_DIR/known/lib.tki"

cp "$TEST_DIR/known/lib.tki" "$TEST_DIR/known/lib.tki.cdw.good"
grep '^// @tki v2 cdw1:' "$TEST_DIR/known/lib.tki" \
    >> "$TEST_DIR/known/lib.tki"
if ! "$TOKAC" --workspace-node outcome-cdw-test --workspace-root "$TEST_DIR" \
    -c "$TEST_DIR/known/main.tk" -o "$TEST_DIR/known/comment-duplicate.o" \
    > "$TEST_DIR/known/comment-duplicate.out" \
    2> "$TEST_DIR/known/comment-duplicate.err"; then
    echo "FAIL: duplicate CDW1 audit comments changed caller acceptance" >&2
    sed 's/^/  | /' "$TEST_DIR/known/comment-duplicate.err" >&2
    exit 1
fi
mv "$TEST_DIR/known/lib.tki.cdw.good" "$TEST_DIR/known/lib.tki"

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
