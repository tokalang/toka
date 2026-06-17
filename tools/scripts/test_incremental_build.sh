#!/usr/bin/env bash
# Test Toka incremental compilation, JSON manifest schema, dirty checks and rebuild plans.

set -e

TOKAC="${TOKAC:-./build/bin/tokac}"
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

PYTHON_BUILD_DRIVER="./tools/scripts/toka_build.py"
chmod +x "$PYTHON_BUILD_DRIVER"

TEST_DIR="./tmp/incremental_test"
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

# Helper for absolute path resolving in bash
resolve_abs_path() {
    python3 -c "import os; print(os.path.realpath('$1').replace('\\\\', '/'))"
}

MAIN_ABS="$(resolve_abs_path "$TEST_DIR/main.tk")"
LIB_ABS="$(resolve_abs_path "$TEST_DIR/lib.tk")"

echo "=================================================="
echo "Running Toka Incremental Build Tests..."
echo "=================================================="

# 0. Set up initial demo package files
cat << 'EOF' > "$TEST_DIR/lib.tk"
pub fn get_val() -> i32 {
    return 42
}
EOF

cat << 'EOF' > "$TEST_DIR/main.tk"
import ./lib::{get_val}
fn main() -> i32 {
    auto v = get_val()
    return 0
}
EOF

# 1. Verify manifest_version: 1.0.0 exists in dump JSON
echo "Test 0: Verifying manifest_version field in tokac JSON dump"
"$TOKAC_ABS" --dump-dependencies=json "$TEST_DIR/main.tk" > "$TEST_DIR/dump_init.json"
if ! grep -q '"manifest_version": "1.0.0"' "$TEST_DIR/dump_init.json"; then
    echo "FAIL: manifest_version missing or incorrect in JSON dump"
    cat "$TEST_DIR/dump_init.json"
    exit 1
fi
echo "PASS: Test 0"

# 2. Test 1: First Build
echo "Test 1: Performing first build and checking plan status"
python3 "$PYTHON_BUILD_DRIVER" --plan -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app" "$TEST_DIR/main.tk" > "$TEST_DIR/plan_1.json"

python3 -c '
import json, sys
data = json.load(sys.stdin)
assert data["status"] == "dirty", f"First build plan status should be dirty, got {data}"
assert len(data["dirty_roots"]) == 1, "Should have 1 dirty root"
modules = data["dirty_modules"]
assert len(modules) >= 2, "Should record all modules in plan"
for path, info in modules.items():
    assert info["reason"] == "first build", f"Expected first build reason, got {info}"
' < "$TEST_DIR/plan_1.json"

# Run the build
python3 "$PYTHON_BUILD_DRIVER" --build -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app" "$TEST_DIR/main.tk" > "$TEST_DIR/build_1.log"

if [ ! -f "$TEST_DIR/app" ]; then
    echo "FAIL: Executable target not compiled in first build"
    cat "$TEST_DIR/build_1.log"
    exit 1
fi
if [ ! -f "$TEST_DIR/manifest.json" ]; then
    echo "FAIL: manifest.json was not persisted after build"
    exit 1
fi

# Run compiled target
"$TEST_DIR/app"
echo "PASS: Test 1"

# 3. Test 2: Zero Rebuild on No Changes
echo "Test 2: Verifying zero-rebuild clean status on no changes"
python3 "$PYTHON_BUILD_DRIVER" --plan -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app" "$TEST_DIR/main.tk" > "$TEST_DIR/plan_2.json"

python3 -c '
import json, sys
data = json.load(sys.stdin)
assert data["status"] == "clean", f"Plan should be clean, got {data}"
assert len(data["dirty_roots"]) == 0
assert len(data["dirty_modules"]) == 0
' < "$TEST_DIR/plan_2.json"

# Check build skip
python3 "$PYTHON_BUILD_DRIVER" --build -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app" "$TEST_DIR/main.tk" > "$TEST_DIR/build_2.log"
if ! grep -q "All targets are clean. Nothing to compile!" "$TEST_DIR/build_2.log"; then
    echo "FAIL: Expected rebuild bypass message, got:"
    cat "$TEST_DIR/build_2.log"
    exit 1
fi
echo "PASS: Test 2"

# 4. Test 3: Modify Dependency Rebuild
echo "Test 3: Checking dependency change contagion and incremental rebuild"
# Modify lib.tk values
cat << 'EOF' > "$TEST_DIR/lib.tk"
pub fn get_val() -> i32 {
    return 99
}
EOF
# main.tk is kept unchanged to verify dependency contagion (hash of main.tk remains the same, only lib.tk hash changes)

python3 "$PYTHON_BUILD_DRIVER" --plan -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app" "$TEST_DIR/main.tk" > "$TEST_DIR/plan_3.json"

python3 -c "
import json, sys
data = json.load(sys.stdin)
assert data['status'] == 'dirty', f'Plan should be dirty, got {data}'
modules = data['dirty_modules']
assert '${LIB_ABS}' in modules, 'lib.tk should be dirty'
assert modules['${LIB_ABS}']['reason'] == 'hash changed', f'lib.tk should have hash changed reason, got {modules}'

assert '${MAIN_ABS}' in modules, 'main.tk should be dirty by dependency contagion'
assert modules['${MAIN_ABS}']['reason'] == 'dependency changed', f'main.tk reason should be dependency changed, got {modules}'
assert '${LIB_ABS}' in modules['${MAIN_ABS}']['dirty_deps'], 'main.tk dirty_deps should trace back to lib.tk'
" < "$TEST_DIR/plan_3.json"

# Re-build to consolidate
python3 "$PYTHON_BUILD_DRIVER" --build -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app" "$TEST_DIR/main.tk" > "$TEST_DIR/build_3.log"
"$TEST_DIR/app"

# Verify it becomes clean again
python3 "$PYTHON_BUILD_DRIVER" --plan -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app" "$TEST_DIR/main.tk" > "$TEST_DIR/plan_3_clean.json"
python3 -c '
import json, sys
data = json.load(sys.stdin)
assert data["status"] == "clean", f"Plan should become clean after compile, got {data}"
' < "$TEST_DIR/plan_3_clean.json"
echo "PASS: Test 3"

# 5. Test 4: Missing Output Rebuild
echo "Test 4: Checking rebuild triggering when target output is deleted"
rm -f "$TEST_DIR/app"

python3 "$PYTHON_BUILD_DRIVER" --plan -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app" "$TEST_DIR/main.tk" > "$TEST_DIR/plan_4.json"

python3 -c "
import json, sys
data = json.load(sys.stdin)
assert data['status'] == 'dirty', f'Plan should be dirty after output deletion, got {data}'
assert '${MAIN_ABS}' in data['dirty_modules'], 'main.tk root should be dirty'
assert data['dirty_modules']['${MAIN_ABS}']['reason'] == 'missing output', f'Expected missing output reason, got {data}'
" < "$TEST_DIR/plan_4.json"

# Re-build
python3 "$PYTHON_BUILD_DRIVER" --build -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app" "$TEST_DIR/main.tk" >/dev/null
if [ ! -f "$TEST_DIR/app" ]; then
    echo "FAIL: Failed to compile missing output"
    exit 1
fi
echo "PASS: Test 4"

# 6. Test 5: Compiler target/version changed rebuild
echo "Test 5: Checking target/version configuration change rejection and clean rebuild"
# Alter compiler version in manifest.json to force target/version mismatch
python3 -c "
import json
p = '$TEST_DIR/manifest.json'
d = json.load(open(p))
for k, v in d['modules'].items():
    v['compiler_version'] = '99.9.9'
json.dump(d, open(p, 'w'), indent=2)
"

python3 "$PYTHON_BUILD_DRIVER" --plan -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app" "$TEST_DIR/main.tk" > "$TEST_DIR/plan_5.json"

python3 -c '
import json, sys
data = json.load(sys.stdin)
assert data["status"] == "dirty", f"Plan should be dirty after version alteration, got {data}"
modules = data["dirty_modules"]
for k, v in modules.items():
    assert v["reason"] == "version/target changed", f"Expected version/target changed, got {v}"
' < "$TEST_DIR/plan_5.json"

# Rebuild and assert clean
python3 "$PYTHON_BUILD_DRIVER" --build -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app" "$TEST_DIR/main.tk" >/dev/null
python3 "$PYTHON_BUILD_DRIVER" --plan -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app" "$TEST_DIR/main.tk" > "$TEST_DIR/plan_5_clean.json"
python3 -c '
import json, sys
data = json.load(sys.stdin)
assert data["status"] == "clean", f"Plan should return to clean, got {data}"
' < "$TEST_DIR/plan_5_clean.json"
echo "PASS: Test 5"

# 7. Test 6: Outputs change and shlex validation
echo "Test 6: Checking outputs path change detection and shlex parsing"
# Test 6.1: shlex parsing with spaces in output path argument
python3 "$PYTHON_BUILD_DRIVER" --build -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o \"$TEST_DIR/app space\"" "$TEST_DIR/main.tk" > "$TEST_DIR/build_6_space.log"
if [ ! -f "$TEST_DIR/app space" ]; then
    echo "FAIL: Failed to compile and parse space argument using shlex"
    cat "$TEST_DIR/build_6_space.log"
    exit 1
fi
rm -f "$TEST_DIR/app space"

# Test 6.2: Build with app1, then plan with app2 to assert dirty (outputs changed)
python3 "$PYTHON_BUILD_DRIVER" --build -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app1" "$TEST_DIR/main.tk" >/dev/null
if [ ! -f "$TEST_DIR/app1" ]; then
    echo "FAIL: Expected app1 output to be compiled"
    exit 1
fi

# Query plan with compiler-args changed to app2 (WITHOUT deleting app1)
python3 "$PYTHON_BUILD_DRIVER" --plan -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app2" "$TEST_DIR/main.tk" > "$TEST_DIR/plan_6_clash.json"

python3 -c "
import json, sys
data = json.load(sys.stdin)
assert data['status'] == 'dirty', f'Plan should be dirty after outputs change, got {data}'
assert '${MAIN_ABS}' in data['dirty_modules'], 'main.tk should be dirty'
assert data['dirty_modules']['${MAIN_ABS}']['reason'] == 'outputs changed', f'Expected outputs changed reason, got {data}'
" < "$TEST_DIR/plan_6_clash.json"

# Rebuild and assert app2 is generated
python3 "$PYTHON_BUILD_DRIVER" --build -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app2" "$TEST_DIR/main.tk" >/dev/null
if [ ! -f "$TEST_DIR/app2" ]; then
    echo "FAIL: Failed to compile and generate app2"
    exit 1
fi

# Assert plan is now clean
python3 "$PYTHON_BUILD_DRIVER" --plan -m "$TEST_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_DIR/app2" "$TEST_DIR/main.tk" > "$TEST_DIR/plan_6_clean.json"
python3 -c '
import json, sys
data = json.load(sys.stdin)
assert data["status"] == "clean", f"Plan should return to clean under app2 output, got {data}"
' < "$TEST_DIR/plan_6_clean.json"

echo "PASS: Test 6"

# Clean up
rm -rf "$TEST_DIR"
echo "=================================================="
echo "All Toka Incremental Build Tests PASSED!"
echo "=================================================="
