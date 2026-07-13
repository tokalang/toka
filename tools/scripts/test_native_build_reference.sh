#!/usr/bin/env bash
# Verify the multi-module native build facade and same-version source-less replay.

set -euo pipefail

TOKAC="${TOKAC:-./build/bin/tokac}"
WORK_ROOT="${WORK_ROOT:-tmp/native_build_reference_replay}"
ROOT_DIR="$(pwd)"
TOKAC_ABS="$(cd "$(dirname "$TOKAC")" && pwd)/$(basename "$TOKAC")"
TEST_LIB="$ROOT_DIR/$WORK_ROOT/lib"

rm -rf "$WORK_ROOT"
mkdir -p "$WORK_ROOT/lib/build/internal"

cp lib/build.tk "$WORK_ROOT/lib/build.tk"
cp lib/build/project.tk "$WORK_ROOT/lib/build/project.tk"
cp lib/build/internal/codec.tk "$WORK_ROOT/lib/build/internal/codec.tk"
cp lib/build/internal/support.tk "$WORK_ROOT/lib/build/internal/support.tk"

for module in core std stdx sys prim hal toolchain; do
    ln -s "$ROOT_DIR/lib/$module" "$WORK_ROOT/lib/$module"
done

TOKA_LIB="$TEST_LIB" "$TOKAC_ABS" -c \
    "$WORK_ROOT/lib/build/internal/support.tk" \
    -o "$WORK_ROOT/lib/build/internal/support.o"
TOKA_LIB="$TEST_LIB" "$TOKAC_ABS" -c \
    "$WORK_ROOT/lib/build/internal/codec.tk" \
    -o "$WORK_ROOT/lib/build/internal/codec.o"
TOKA_LIB="$TEST_LIB" "$TOKAC_ABS" -c \
    "$WORK_ROOT/lib/build/project.tk" \
    -o "$WORK_ROOT/lib/build/project.o"
TOKA_LIB="$TEST_LIB" "$TOKAC_ABS" -c \
    "$WORK_ROOT/lib/build.tk" \
    -o "$WORK_ROOT/lib/build.o"

for interface in \
    "$WORK_ROOT/lib/build.tki" \
    "$WORK_ROOT/lib/build/project.tki" \
    "$WORK_ROOT/lib/build/internal/codec.tki" \
    "$WORK_ROOT/lib/build/internal/support.tki"; do
    if [ ! -f "$interface" ]; then
        echo "FAIL: expected interface not generated: $interface"
        exit 1
    fi
done

if ! grep -q "pub import build/project" "$WORK_ROOT/lib/build.tki"; then
    echo "FAIL: build facade interface lost its project re-export"
    exit 1
fi

mv "$WORK_ROOT/lib/build.tk" "$WORK_ROOT/lib/build.tk.hidden"
mv "$WORK_ROOT/lib/build/project.tk" "$WORK_ROOT/lib/build/project.tk.hidden"
mv "$WORK_ROOT/lib/build/internal/codec.tk" "$WORK_ROOT/lib/build/internal/codec.tk.hidden"
mv "$WORK_ROOT/lib/build/internal/support.tk" "$WORK_ROOT/lib/build/internal/support.tk.hidden"

cat > "$WORK_ROOT/compile_only.tk" <<'EOF'
import build::{Executable, run_build}

fn invoke_build() -> i32 {
    auto app = Executable::make(c"reference", c"main.tk")
    return run_build(app)
}
EOF

(cd "$WORK_ROOT" && TOKA_LIB="$TEST_LIB" "$TOKAC_ABS" -c compile_only.tk \
    -o compile_only.o)

cp "$WORK_ROOT/lib/build.tki" "$WORK_ROOT/lib/build.tki.valid"
sed 's/compiler_version: .*/compiler_version: 99.0.reference/' \
    "$WORK_ROOT/lib/build.tki.valid" > "$WORK_ROOT/lib/build.tki"
if (cd "$WORK_ROOT" && TOKA_LIB="$TEST_LIB" "$TOKAC_ABS" -c compile_only.tk \
    -o stale.o > stale.out 2> stale.err); then
    echo "FAIL: source-less stale build facade interface was accepted"
    exit 1
fi

printf '@broken\n' > "$WORK_ROOT/lib/build.tki"
if (cd "$WORK_ROOT" && TOKA_LIB="$TEST_LIB" "$TOKAC_ABS" -c compile_only.tk \
    -o malformed.o > malformed.out 2> malformed.err); then
    echo "FAIL: source-less malformed build facade interface was accepted"
    exit 1
fi
mv "$WORK_ROOT/lib/build.tki.valid" "$WORK_ROOT/lib/build.tki"

echo "PASS: native build facade source-less replay"
