#!/usr/bin/env bash
# Run source-less .tki semantic replay tests.

set -euo pipefail

TOKAC="${TOKAC:-./build/bin/tokac}"
CASE_ROOT="${CASE_ROOT:-tests/semantics/tki_replay/cases}"
WORK_ROOT="${WORK_ROOT:-/tmp/toka_semantic_replay}"
EVIDENCE_COMPARE="${EVIDENCE_COMPARE:-tools/scripts/compare_semantic_evidence.py}"

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

extract_expected_codes() {
    local file="$1"
    sed -n 's/.*EXPECT_ERROR:[[:space:]]*\(E[0-9][0-9]*\).*/\1/p' "$file" | sort -u
}

extract_expected_tki() {
    local file="$1"
    sed -n 's/.*EXPECT_TKI:[[:space:]]*//p' "$file"
}

run_case() {
    local case_dir="$1"
    local case_name
    case_name="$(basename "$case_dir")"
    local work_dir="$WORK_ROOT/$case_name"
    mkdir -p "$work_dir"

    cp "$case_dir"/*.tk "$work_dir"/

    local lib_src="$work_dir/lib.tk"
    local lib_obj="$work_dir/lib.o"
    local lib_tki="$work_dir/lib.tki"
    local lib_hidden="$work_dir/lib.tk.source-hidden"

    if ! "$TOKAC_ABS" -c "$lib_src" -o "$lib_obj" > "$work_dir/lib.out" 2> "$work_dir/lib.err"; then
        echo "FAIL $case_name: provider compilation failed"
        sed 's/^/  | /' "$work_dir/lib.err"
        failed=$((failed + 1))
        return
    fi
    if [ ! -f "$lib_tki" ]; then
        echo "FAIL $case_name: provider did not emit lib.tki"
        failed=$((failed + 1))
        return
    fi

    local expected_tki
    while IFS= read -r expected_tki; do
        [ -n "$expected_tki" ] || continue
        if ! grep -Fq -- "$expected_tki" "$lib_tki"; then
            echo "FAIL $case_name: generated lib.tki is missing: $expected_tki"
            failed=$((failed + 1))
            return
        fi
    done < <(extract_expected_tki "$lib_src")

    local held_tki="$work_dir/lib.tki.replay-held"
    mv "$lib_tki" "$held_tki"
    mkdir -p "$work_dir/source-build/interfaces" "$work_dir/source-build/objects"
    local source_consumer
    for source_consumer in "$work_dir"/pass_*.tk; do
        [ -e "$source_consumer" ] || continue
        local source_stem
        source_stem="$(basename "$source_consumer" .tk)"
        if ! TOKA_BUILD_DIR="$work_dir/source-build" TOKA_USE_LIB_CACHE=0 \
            "$TOKAC_ABS" --dump-semantic-evidence=json -c "$source_consumer" \
            -o "$work_dir/$source_stem.source.o" \
            > "$work_dir/$source_stem.source.out" \
            2> "$work_dir/$source_stem.source.err"; then
            echo "FAIL $case_name/$source_stem: source-backed compilation failed"
            sed 's/^/  | /' "$work_dir/$source_stem.source.err"
            failed=$((failed + 1))
            return
        fi
    done

    for source_consumer in "$work_dir"/fail_*.tk; do
        [ -e "$source_consumer" ] || continue
        local source_stem
        source_stem="$(basename "$source_consumer" .tk)"
        local source_expected_codes
        source_expected_codes="$(extract_expected_codes "$source_consumer")"
        if TOKA_BUILD_DIR="$work_dir/source-build" TOKA_USE_LIB_CACHE=0 \
            "$TOKAC_ABS" --dump-semantic-evidence=json -c "$source_consumer" \
            -o "$work_dir/$source_stem.source.o" \
            > "$work_dir/$source_stem.source.out" \
            2> "$work_dir/$source_stem.source.err"; then
            echo "FAIL $case_name/$source_stem: source-backed negative case passed"
            failed=$((failed + 1))
            return
        fi
        local source_code
        while read -r source_code; do
            [ -n "$source_code" ] || continue
            if ! grep -Fq -- "$source_code" "$work_dir/$source_stem.source.err"; then
                echo "FAIL $case_name/$source_stem: source-backed path missed $source_code"
                sed 's/^/  | /' "$work_dir/$source_stem.source.err"
                failed=$((failed + 1))
                return
            fi
        done <<< "$source_expected_codes"
    done
    mv "$held_tki" "$lib_tki"

    mv "$lib_src" "$lib_hidden"

    local case_failed=0
    local consumer_count=0
    local consumer
    for consumer in "$work_dir"/pass_*.tk; do
        [ -e "$consumer" ] || continue
        consumer_count=$((consumer_count + 1))
        local stem
        stem="$(basename "$consumer" .tk)"
        local exe="$work_dir/$stem.exe"
        local compile_cmd=("$TOKAC_ABS" "--dump-semantic-evidence=json" "$consumer" "$lib_obj" "-o" "$exe")
        local compile_only=0
        if grep -q "COMPILE_ONLY" "$consumer"; then
            compile_only=1
            exe="$work_dir/$stem.o"
            compile_cmd=("$TOKAC_ABS" "--dump-semantic-evidence=json" "-c" "$consumer" "-o" "$exe")
        fi
        if ! "${compile_cmd[@]}" > "$work_dir/$stem.out" 2> "$work_dir/$stem.err"; then
            echo "FAIL $case_name/$stem: expected pass but compilation failed"
            sed 's/^/  | /' "$work_dir/$stem.err"
            case_failed=1
            continue
        fi
        if ! python3 "$EVIDENCE_COMPARE" \
            "$work_dir/$stem.source.out" "$work_dir/$stem.out" "$consumer"; then
            echo "FAIL $case_name/$stem: source/interface semantic evidence differs"
            case_failed=1
            continue
        fi
        if [ "$compile_only" -eq 1 ]; then
            continue
        fi
        if ! "$exe" > "$work_dir/$stem.run.out" 2> "$work_dir/$stem.run.err"; then
            echo "FAIL $case_name/$stem: executable returned failure"
            sed 's/^/  | /' "$work_dir/$stem.run.err"
            case_failed=1
        fi
    done

    for consumer in "$work_dir"/fail_*.tk; do
        [ -e "$consumer" ] || continue
        consumer_count=$((consumer_count + 1))
        local stem
        stem="$(basename "$consumer" .tk)"
        local exe="$work_dir/$stem.exe"
        local expected_codes
        expected_codes="$(extract_expected_codes "$consumer")"
        if [ -z "$expected_codes" ]; then
            echo "FAIL $case_name/$stem: no EXPECT_ERROR diagnostic declared"
            case_failed=1
            continue
        fi
        if "$TOKAC_ABS" --dump-semantic-evidence=json "$consumer" "$lib_obj" -o "$exe" > "$work_dir/$stem.out" 2> "$work_dir/$stem.err"; then
            echo "FAIL $case_name/$stem: expected failure but compilation passed"
            case_failed=1
            continue
        fi

        local combined="$work_dir/$stem.combined"
        cat "$work_dir/$stem.err" "$work_dir/$stem.out" > "$combined"
        local code
        while read -r code; do
            [ -n "$code" ] || continue
            if ! grep -Fq -- "$code" "$combined"; then
                echo "FAIL $case_name/$stem: expected diagnostic $code"
                sed 's/^/  | /' "$combined"
                case_failed=1
            fi
        done <<< "$expected_codes"
        if ! python3 "$EVIDENCE_COMPARE" \
            "$work_dir/$stem.source.out" "$work_dir/$stem.out" "$consumer"; then
            echo "FAIL $case_name/$stem: source/interface semantic evidence differs"
            case_failed=1
        fi
    done

    if [ "$consumer_count" -eq 0 ]; then
        echo "FAIL $case_name: no pass_*.tk or fail_*.tk consumers"
        case_failed=1
    fi

    if [ "$case_failed" -eq 0 ]; then
        echo "PASS $case_name"
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
}

for case_dir in "$CASE_ROOT"/*; do
    [ -d "$case_dir" ] || continue
    run_case "$case_dir"
done

echo "----------------------------------------"
echo "Semantic replay cases passed: $passed"
echo "Semantic replay cases failed: $failed"

if [ "$failed" -ne 0 ]; then
    exit 1
fi
