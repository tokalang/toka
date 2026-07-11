#!/usr/bin/env bash
set -euo pipefail

TOKAC="${TOKAC:-./build/bin/tokac}"
WORK_ROOT="${WORK_ROOT:-/tmp/toka_semantic_evidence}"
CASE_DIR="tests/semantics/tki_replay/cases/pal_call_001_alias"
FAIL_CASE="$CASE_DIR/fail_mut_read_alias.tk"
PASS_CASE="$CASE_DIR/pass_read_read.tk"
ASYNC_CASE="tests/semantics/tki_replay/cases/async_suspend_001_return_deps/fail_start_borrowed_view.tk"
OBSERVATIONAL_CASE="tests/semantics/tki_replay/cases/own_resource_002_spread_generic/pass_use_public_api.tk"

rm -rf "$WORK_ROOT"
mkdir -p "$WORK_ROOT"

run_expected_failure() {
    local suffix="$1"
    set +e
    "$TOKAC" --dump-semantic-evidence=json -c "$FAIL_CASE" \
        -o "$WORK_ROOT/fail_$suffix.o" \
        > "$WORK_ROOT/fail_$suffix.json" \
        2> "$WORK_ROOT/fail_$suffix.err"
    local status=$?
    set -e
    if [ "$status" -eq 0 ]; then
        echo "FAIL: semantic evidence negative case compiled successfully"
        exit 1
    fi
}

run_expected_failure first
run_expected_failure second

cmp "$WORK_ROOT/fail_first.json" "$WORK_ROOT/fail_second.json"
python3 tools/scripts/compare_semantic_evidence.py \
    "$WORK_ROOT/fail_first.json" "$WORK_ROOT/fail_second.json" "$FAIL_CASE"
grep -Fq '"schema":"toka.semantic-evidence","version":1' \
    "$WORK_ROOT/fail_first.json"
grep -Fq '"decision":"Reject","reason":"OverlappingExclusiveAccess"' \
    "$WORK_ROOT/fail_first.json"
grep -Fq 'conflicting borrow originates here' "$WORK_ROOT/fail_first.err"

"$TOKAC" --dump-semantic-evidence=json -c "$PASS_CASE" \
    -o "$WORK_ROOT/pass.o" > "$WORK_ROOT/pass.json" \
    2> "$WORK_ROOT/pass.err"
grep -Fq '"rule":"PAL-CALL-001"' "$WORK_ROOT/pass.json"
grep -Fq '"decision":"Allow","reason":"CompatibleSharedAccess"' \
    "$WORK_ROOT/pass.json"

set +e
"$TOKAC" --dump-semantic-evidence=json -c "$ASYNC_CASE" \
    -o "$WORK_ROOT/async.o" > "$WORK_ROOT/async.json" \
    2> "$WORK_ROOT/async.err"
async_status=$?
set -e
if [ "$async_status" -eq 0 ]; then
    echo "FAIL: async evidence negative case compiled successfully"
    exit 1
fi
python3 tools/scripts/compare_semantic_evidence.py \
    "$WORK_ROOT/async.json" "$WORK_ROOT/async.json" "$ASYNC_CASE"
grep -Fq 'execution-boundary parameter declared here' "$WORK_ROOT/async.err"
grep -Fq 'borrowed dependency originates here' "$WORK_ROOT/async.err"

"$TOKAC" -c "$OBSERVATIONAL_CASE" -o "$WORK_ROOT/plain.o" \
    > "$WORK_ROOT/plain.out" 2> "$WORK_ROOT/plain.err"
"$TOKAC" --dump-semantic-evidence=json -c "$OBSERVATIONAL_CASE" \
    -o "$WORK_ROOT/evidence.o" > "$WORK_ROOT/observational.json" \
    2> "$WORK_ROOT/evidence.err"
cmp "$WORK_ROOT/plain.o" "$WORK_ROOT/evidence.o"

echo "Semantic evidence tests PASSED"
