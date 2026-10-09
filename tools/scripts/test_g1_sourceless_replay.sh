#!/usr/bin/env bash
# tools/scripts/test_g1_sourceless_replay.sh
# 0.14 G1: 3-Module Sourceless Replay Verification
# Module A exports trait + Mock -> mod_a.tki + mod_a.o
# Module B imports A, exports generic consumer -> mod_b.tki + mod_b.o
# Module C imports A & B -> compiled with ONLY .tki and .o (source .tk hidden)

set -euo pipefail

TOKAC="${TOKAC:-./build/bin/tokac}"
FIXTURES_DIR="tests/fixtures/g1_sourceless"

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

export TOKA_LIB="${TOKA_LIB:-$(pwd)/lib}"

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/toka_g1_sourceless.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT

echo "=== G1 Sourceless Replay Test ==="
echo "Work dir: $WORK_DIR"

cp "$FIXTURES_DIR"/mod_a.tk "$WORK_DIR"/
cp "$FIXTURES_DIR"/mod_b.tk "$WORK_DIR"/
cp "$FIXTURES_DIR"/main.tk "$WORK_DIR"/

cd "$WORK_DIR"

echo "Step 1: Compiling mod_a.tk to mod_a.o and mod_a.tki..."
"$TOKAC_ABS" -c mod_a.tk -o mod_a.o

if [ ! -f mod_a.tki ] || [ ! -f mod_a.o ]; then
    echo "ERROR: mod_a.tki or mod_a.o was not emitted" >&2
    exit 1
fi

echo "Step 2: Compiling mod_b.tk to mod_b.o and mod_b.tki..."
"$TOKAC_ABS" -c mod_b.tk -o mod_b.o

if [ ! -f mod_b.tki ] || [ ! -f mod_b.o ]; then
    echo "ERROR: mod_b.tki or mod_b.o was not emitted" >&2
    exit 1
fi

echo "Step 3: Hiding source files mod_a.tk and mod_b.tk..."
mv mod_a.tk mod_a.tk.hidden
mv mod_b.tk mod_b.tk.hidden

test ! -f mod_a.tk
test ! -f mod_b.tk

echo "Step 4: Compiling main.tk with ONLY .tki and .o artifacts..."
"$TOKAC_ABS" main.tk mod_a.o mod_b.o -o main_bin

if [ ! -x main_bin ]; then
    echo "ERROR: main_bin was not produced" >&2
    exit 1
fi

echo "Step 5: Executing main_bin..."
./main_bin

echo "=== G1 Sourceless Replay Passed Successfully ==="
