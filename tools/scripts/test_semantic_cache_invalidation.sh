#!/usr/bin/env bash
# Verify that semantic-only source changes invalidate cached interfaces.

set -euo pipefail

TOKAC="${TOKAC:-./build/bin/tokac}"
CASE_ROOT="${CASE_ROOT:-tests/semantics/tki_cache/cases}"
WORK_ROOT="${WORK_ROOT:-/tmp/toka_semantic_cache}"

if [[ "$TOKAC" = /* ]]; then
    TOKAC_ABS="$TOKAC"
elif [[ "$TOKAC" = */* ]]; then
    TOKAC_ABS="$(cd "$(dirname "$TOKAC")" && pwd)/$(basename "$TOKAC")"
else
    TOKAC_ABS="$(command -v "$TOKAC" 2>/dev/null || echo "$TOKAC")"
    if [[ "$TOKAC_ABS" != /* ]]; then
        TOKAC_ABS="./$TOKAC"
    fi
fi

if [ ! -x "$TOKAC_ABS" ]; then
    echo "error: tokac not found or not executable: $TOKAC_ABS" >&2
    exit 1
fi

rm -rf "$WORK_ROOT"
mkdir -p "$WORK_ROOT"

passed=0
failed=0
case_count=0

extract_expected_codes() {
    local file="$1"
    sed -n 's/.*EXPECT_ERROR:[[:space:]]*\(E[0-9][0-9]*\).*/\1/p' "$file" | sort -u
}

run_case() {
    local case_dir="$1"
    local case_name
    case_name="$(basename "$case_dir")"
    local work_dir="$WORK_ROOT/$case_name"
    local build_dir="$work_dir/build"
    mkdir -p "$build_dir/objects" "$build_dir/interfaces"

    local required
    for required in before.tk after.tk main.tk fail_main.tk; do
        if [ ! -f "$case_dir/$required" ]; then
            echo "FAIL $case_name: missing $required"
            failed=$((failed + 1))
            return
        fi
    done
    cp "$case_dir"/*.tk "$work_dir"/

    cp "$work_dir/before.tk" "$work_dir/lib.tk"
    if ! TOKA_BUILD_DIR="$build_dir" "$TOKAC_ABS" -c "$work_dir/lib.tk" \
        --emit-interface -o "$build_dir/objects/lib.o" \
        > "$work_dir/before.out" 2> "$work_dir/before.err"; then
        echo "FAIL $case_name: before.tk did not compile"
        sed 's/^/  | /' "$work_dir/before.err"
        failed=$((failed + 1))
        return
    fi

    if ! TOKA_BUILD_DIR="$build_dir" "$TOKAC_ABS" -c "$work_dir/fail_main.tk" \
        -o "$work_dir/before_main.o" \
        > "$work_dir/before_main.out" 2> "$work_dir/before_main.err"; then
        echo "FAIL $case_name: discriminating consumer already fails before the semantic change"
        sed 's/^/  | /' "$work_dir/before_main.err"
        failed=$((failed + 1))
        return
    fi

    cp "$work_dir/after.tk" "$work_dir/lib.tk"
    if ! TOKA_BUILD_DIR="$build_dir" "$TOKAC_ABS" \
        --dump-dependencies=json -c "$work_dir/main.tk" \
        > "$work_dir/deps.json" 2> "$work_dir/deps.err"; then
        echo "FAIL $case_name: dependency inspection failed"
        sed 's/^/  | /' "$work_dir/deps.err"
        failed=$((failed + 1))
        return
    fi

    if ! python3 -c '
import json, sys
data = json.load(open(sys.argv[1]))
modules = data.get("modules", {})
matches = [m for path, m in modules.items() if path.endswith("/lib.tk")]
assert len(matches) == 1, "expected one fallback lib.tk module"
module = matches[0]
assert module.get("fallback_triggered") is True, module
assert module.get("cache_status") == "SourceHashMismatch", module
' "$work_dir/deps.json"; then
        echo "FAIL $case_name: semantic change did not report SourceHashMismatch"
        cat "$work_dir/deps.json"
        failed=$((failed + 1))
        return
    fi

    local expected_codes
    expected_codes="$(extract_expected_codes "$work_dir/fail_main.tk")"
    if [ -z "$expected_codes" ]; then
        echo "FAIL $case_name: fail_main.tk has no EXPECT_ERROR"
        failed=$((failed + 1))
        return
    fi

    if TOKA_BUILD_DIR="$build_dir" "$TOKAC_ABS" -c "$work_dir/fail_main.tk" \
        -o "$work_dir/fail_main.o" > "$work_dir/fail.out" 2> "$work_dir/fail.err"; then
        echo "FAIL $case_name: stale interface hid the new semantic constraint"
        failed=$((failed + 1))
        return
    fi

    local code
    while read -r code; do
        [ -n "$code" ] || continue
        if ! grep -Fq -- "$code" "$work_dir/fail.err"; then
            echo "FAIL $case_name: expected diagnostic $code after fallback"
            sed 's/^/  | /' "$work_dir/fail.err"
            failed=$((failed + 1))
            return
        fi
    done <<< "$expected_codes"

    if ! TOKA_BUILD_DIR="$build_dir" "$TOKAC_ABS" -c "$work_dir/lib.tk" \
        --emit-interface -o "$build_dir/objects/lib.o" \
        > "$work_dir/after.out" 2> "$work_dir/after.err"; then
        echo "FAIL $case_name: after.tk did not compile into a fresh interface"
        sed 's/^/  | /' "$work_dir/after.err"
        failed=$((failed + 1))
        return
    fi

    local fresh_tki
    fresh_tki="$(grep -lE '^// @meta source_path: .*/lib\.tk$' \
        "$build_dir"/interfaces/*.tki | head -n 1)"
    if [ -z "$fresh_tki" ]; then
        echo "FAIL $case_name: fresh lib interface was not emitted"
        failed=$((failed + 1))
        return
    fi
    cp "$fresh_tki" "$work_dir/lib.tki"
    mv "$work_dir/lib.tk" "$work_dir/lib.tk.source-hidden"
    if TOKA_BUILD_DIR="$build_dir" "$TOKAC_ABS" -c "$work_dir/fail_main.tk" \
        -o "$work_dir/fail_tki.o" \
        > "$work_dir/fail_tki.out" 2> "$work_dir/fail_tki.err"; then
        echo "FAIL $case_name: fresh source-less interface lost the new constraint"
        failed=$((failed + 1))
        return
    fi

    while read -r code; do
        [ -n "$code" ] || continue
        if ! grep -Fq -- "$code" "$work_dir/fail_tki.err"; then
            echo "FAIL $case_name: source-less replay did not report $code"
            sed 's/^/  | /' "$work_dir/fail_tki.err"
            failed=$((failed + 1))
            return
        fi
    done <<< "$expected_codes"

    echo "PASS $case_name"
    passed=$((passed + 1))
}

for case_dir in "$CASE_ROOT"/*; do
    [ -d "$case_dir" ] || continue
    case_count=$((case_count + 1))
    run_case "$case_dir"
done

if [ "$case_count" -eq 0 ]; then
    echo "FAIL: no semantic cache cases found under $CASE_ROOT"
    failed=$((failed + 1))
fi

echo "----------------------------------------"
echo "Semantic cache cases passed: $passed"
echo "Semantic cache cases failed: $failed"

if [ "$failed" -ne 0 ]; then
    exit 1
fi
