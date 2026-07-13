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

TOKA="${TOKA:-./build/bin/toka}"
if [[ "$TOKA" = /* ]]; then
    TOKA_ABS="$TOKA"
elif [[ "$TOKA" = */* ]]; then
    TOKA_ABS="$(cd "$(dirname "$TOKA")" && pwd)/$(basename "$TOKA")"
else
    TOKA_ABS="$(command -v "$TOKA" 2>/dev/null || echo "$TOKA")"
    if [[ "$TOKA_ABS" != /* ]]; then
        TOKA_ABS="./$TOKA"
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

WORKSPACE_DIR="$(resolve_abs_path .)"
export TOKA_LIB="$WORKSPACE_DIR/lib"
PYTHON_BUILD_DRIVER_ABS="$WORKSPACE_DIR/tools/scripts/toka_build.py"
MAIN_ABS="$(resolve_abs_path "$TEST_DIR/main.tk")"
LIB_ABS="$(resolve_abs_path "$TEST_DIR/lib.tk")"

assert_plans_equal() {
    python3 -c '
import json, sys
def compare(p1_path, p2_path):
    with open(p1_path) as f:
        p1 = json.load(f)
    with open(p2_path) as f:
        p2 = json.load(f)
    if p1.get("status") != p2.get("status"):
        raise ValueError("status mismatch: %s vs %s" % (p1.get("status"), p2.get("status")))
    r1 = sorted(p1.get("dirty_roots", []))
    r2 = sorted(p2.get("dirty_roots", []))
    if r1 != r2:
        raise ValueError(f"dirty_roots mismatch: {r1} vs {r2}")
    m1_keys = sorted(p1.get("dirty_modules", {}).keys())
    m2_keys = sorted(p2.get("dirty_modules", {}).keys())
    if m1_keys != m2_keys:
        raise ValueError(f"dirty_modules keys mismatch: {m1_keys} vs {m2_keys}")
    m1 = p1.get("dirty_modules", {})
    m2 = p2.get("dirty_modules", {})
    for k in m1:
        v1 = m1[k]
        v2 = m2[k]
        if v1.get("reason") != v2.get("reason"):
            raise ValueError("Module %s reason mismatch: %s vs %s" % (k, v1.get("reason"), v2.get("reason")))
        d1 = sorted(v1.get("dirty_deps", []))
        d2 = sorted(v2.get("dirty_deps", []))
        if d1 != d2:
            raise ValueError(f"Module {k} dirty_deps mismatch: {d1} vs {d2}")
compare(sys.argv[1], sys.argv[2])
print("Plans are equal.")
' "$1" "$2"
}

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

cat << 'EOF' > "$TEST_DIR/build.tk"
import build::{Executable, run_build}
fn main() -> i32 {
    auto app# = Executable::make(c"app", c"main.tk")
    return run_build(app)
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
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --plan -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app" main.tk) > "$TEST_DIR/plan_1.json"
(cd "$TEST_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$WORKSPACE_DIR/lib" "$TOKA_ABS" build --plan -m manifest.json) > "$TEST_DIR/plan_1_native.json"
assert_plans_equal "$TEST_DIR/plan_1.json" "$TEST_DIR/plan_1_native.json"

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
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --build -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app" main.tk) > "$TEST_DIR/build_1.log"

if [ ! -f "$TEST_DIR/target/debug/app" ]; then
    echo "FAIL: Executable target not compiled in first build"
    cat "$TEST_DIR/build_1.log"
    exit 1
fi
if [ ! -f "$TEST_DIR/manifest.json" ]; then
    echo "FAIL: manifest.json was not persisted after build"
    exit 1
fi

# Run compiled target
"$TEST_DIR/target/debug/app"
echo "PASS: Test 1"

# 3. Test 2: Zero Rebuild on No Changes
echo "Test 2: Verifying zero-rebuild clean status on no changes"
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --plan -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app" main.tk) > "$TEST_DIR/plan_2.json"
(cd "$TEST_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$WORKSPACE_DIR/lib" "$TOKA_ABS" build --plan -m manifest.json) > "$TEST_DIR/plan_2_native.json"
assert_plans_equal "$TEST_DIR/plan_2.json" "$TEST_DIR/plan_2_native.json"

python3 -c '
import json, sys
data = json.load(sys.stdin)
assert data["status"] == "clean", f"Plan should be clean, got {data}"
assert len(data["dirty_roots"]) == 0
assert len(data["dirty_modules"]) == 0
' < "$TEST_DIR/plan_2.json"

# Check build skip
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --build -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app" main.tk) > "$TEST_DIR/build_2.log"
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

(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --plan -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app" main.tk) > "$TEST_DIR/plan_3.json"
(cd "$TEST_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$WORKSPACE_DIR/lib" "$TOKA_ABS" build --plan -m manifest.json) > "$TEST_DIR/plan_3_native.json"
assert_plans_equal "$TEST_DIR/plan_3.json" "$TEST_DIR/plan_3_native.json"

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
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --build -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app" main.tk) > "$TEST_DIR/build_3.log"
"$TEST_DIR/target/debug/app"

# Verify it becomes clean again
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --plan -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app" main.tk) > "$TEST_DIR/plan_3_clean.json"
(cd "$TEST_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$WORKSPACE_DIR/lib" "$TOKA_ABS" build --plan -m manifest.json) > "$TEST_DIR/plan_3_clean_native.json"
assert_plans_equal "$TEST_DIR/plan_3_clean.json" "$TEST_DIR/plan_3_clean_native.json"
echo "PASS: Test 3"

# 5. Test 4: Missing Output Rebuild
echo "Test 4: Checking rebuild triggering when target output is deleted"
rm -f "$TEST_DIR/target/debug/app"

(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --plan -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app" main.tk) > "$TEST_DIR/plan_4.json"
(cd "$TEST_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$WORKSPACE_DIR/lib" "$TOKA_ABS" build --plan -m manifest.json) > "$TEST_DIR/plan_4_native.json"
assert_plans_equal "$TEST_DIR/plan_4.json" "$TEST_DIR/plan_4_native.json"

python3 -c "
import json, sys
data = json.load(sys.stdin)
assert data['status'] == 'dirty', f'Plan should be dirty after output deletion, got {data}'
assert '${MAIN_ABS}' in data['dirty_modules'], 'main.tk root should be dirty'
assert data['dirty_modules']['${MAIN_ABS}']['reason'] == 'missing output', f'Expected missing output reason, got {data}'
" < "$TEST_DIR/plan_4.json"

# Re-build
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --build -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app" main.tk) >/dev/null
if [ ! -f "$TEST_DIR/target/debug/app" ]; then
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

(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --plan -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app" main.tk) > "$TEST_DIR/plan_5.json"
(cd "$TEST_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$WORKSPACE_DIR/lib" "$TOKA_ABS" build --plan -m manifest.json) > "$TEST_DIR/plan_5_native.json"
assert_plans_equal "$TEST_DIR/plan_5.json" "$TEST_DIR/plan_5_native.json"

python3 -c '
import json, sys
data = json.load(sys.stdin)
assert data["status"] == "dirty", f"Plan should be dirty after version alteration, got {data}"
modules = data["dirty_modules"]
for k, v in modules.items():
    assert v["reason"] == "version/target changed", f"Expected version/target changed, got {v}"
' < "$TEST_DIR/plan_5.json"

# Rebuild and assert clean
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --build -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app" main.tk) >/dev/null
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --plan -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app" main.tk) > "$TEST_DIR/plan_5_clean.json"
(cd "$TEST_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$WORKSPACE_DIR/lib" "$TOKA_ABS" build --plan -m manifest.json) > "$TEST_DIR/plan_5_clean_native.json"
assert_plans_equal "$TEST_DIR/plan_5_clean.json" "$TEST_DIR/plan_5_clean_native.json"
echo "PASS: Test 5"

# 7. Test 6: Outputs change and shlex validation
echo "Test 6: Checking outputs path change detection and shlex parsing"
# Test 6.1: shlex parsing with spaces in output path argument
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --build -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o \"target/debug/app space\"" main.tk) > "$TEST_DIR/build_6_space.log"
if [ ! -f "$TEST_DIR/target/debug/app space" ]; then
    echo "FAIL: Failed to compile and parse space argument using shlex"
    cat "$TEST_DIR/build_6_space.log"
    exit 1
fi
rm -f "$TEST_DIR/target/debug/app space"

# Test 6.2: Build with app1, then plan with app2 to assert dirty (outputs changed)
# Write build.tk to target app1
cat << 'EOF' > "$TEST_DIR/build.tk"
import build::{Executable, run_build}
fn main() -> i32 {
    auto app# = Executable::make(c"app1", c"main.tk")
    return run_build(app)
}
EOF

(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --build -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app1" main.tk) >/dev/null
if [ ! -f "$TEST_DIR/target/debug/app1" ]; then
    echo "FAIL: Expected app1 output to be compiled"
    exit 1
fi

# Overwrite build.tk to target app2 before querying plan
cat << 'EOF' > "$TEST_DIR/build.tk"
import build::{Executable, run_build}
fn main() -> i32 {
    auto app# = Executable::make(c"app2", c"main.tk")
    return run_build(app)
}
EOF

# Query plan with compiler-args changed to app2 (WITHOUT deleting app1)
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --plan -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app2" main.tk) > "$TEST_DIR/plan_6_clash.json"
(cd "$TEST_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$WORKSPACE_DIR/lib" "$TOKA_ABS" build --plan -m manifest.json) > "$TEST_DIR/plan_6_clash_native.json"
assert_plans_equal "$TEST_DIR/plan_6_clash.json" "$TEST_DIR/plan_6_clash_native.json"

python3 -c "
import json, sys
data = json.load(sys.stdin)
assert data['status'] == 'dirty', f'Plan should be dirty after outputs change, got {data}'
assert '${MAIN_ABS}' in data['dirty_modules'], 'main.tk should be dirty'
assert data['dirty_modules']['${MAIN_ABS}']['reason'] == 'outputs changed', f'Expected outputs changed reason, got {data}'
" < "$TEST_DIR/plan_6_clash.json"

# Rebuild and assert app2 is generated
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --build -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app2" main.tk) >/dev/null
if [ ! -f "$TEST_DIR/target/debug/app2" ]; then
    echo "FAIL: Failed to compile and generate app2"
    exit 1
fi

# Assert plan is now clean
(cd "$TEST_DIR" && python3 "$PYTHON_BUILD_DRIVER_ABS" --plan -m manifest.json --tokac "$TOKAC_ABS" --compiler-args "-o target/debug/app2" main.tk) > "$TEST_DIR/plan_6_clean.json"
(cd "$TEST_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$WORKSPACE_DIR/lib" "$TOKA_ABS" build --plan -m manifest.json) > "$TEST_DIR/plan_6_clean_native.json"
assert_plans_equal "$TEST_DIR/plan_6_clean.json" "$TEST_DIR/plan_6_clean_native.json"

echo "PASS: Test 6"

# 8. Test 7: Toka Build official entry integration and project fixture check
echo "Test 7: Verifying toka build official integration with project fixture"
FIXTURE_DIR="tests/fixtures/incremental_project"
rm -rf "$FIXTURE_DIR/.toka"
rm -rf "$FIXTURE_DIR/target"

# Test 7.1: First build via toka build
echo "Test 7.1: Performing first build via toka build"
(cd "$FIXTURE_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="../../../lib" "$TOKA_ABS" build) > "$TEST_DIR/toka_build_1.log"

if [ ! -f "$FIXTURE_DIR/target/debug/incremental_project" ]; then
    echo "FAIL: executable target not built via toka build"
    cat "$TEST_DIR/toka_build_1.log"
    exit 1
fi
if [ ! -f "$FIXTURE_DIR/.toka/build/manifest.json" ]; then
    echo "FAIL: manifest.json was not created in .toka/build/"
    exit 1
fi
echo "PASS: Test 7.1"

# Test 7.2: Second build (zero rebuild skip)
echo "Test 7.2: Verifying zero-rebuild clean status on second toka build"
(cd "$FIXTURE_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="../../../lib" "$TOKA_ABS" build) > "$TEST_DIR/toka_build_2.log"
if ! grep -q "All targets are clean. Nothing to compile!" "$TEST_DIR/toka_build_2.log"; then
    echo "FAIL: Expected clean bypass from toka build, got:"
    cat "$TEST_DIR/toka_build_2.log"
    exit 1
fi
echo "PASS: Test 7.2"

# Test 7.3: Modify dependency module trigger rebuild
echo "Test 7.3: Modifying dependency src/lib.tk and verifying rebuild"
# Backup and modify src/lib.tk
cp "$FIXTURE_DIR/src/lib.tk" "$FIXTURE_DIR/src/lib.tk.bak"
cat << 'EOF' > "$FIXTURE_DIR/src/lib.tk"
pub fn get_val() -> i32 {
    return 999
}
EOF

# Run toka build
(cd "$FIXTURE_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="../../../lib" "$TOKA_ABS" build) > "$TEST_DIR/toka_build_3.log"
if grep -q "All targets are clean. Nothing to compile!" "$TEST_DIR/toka_build_3.log"; then
    echo "FAIL: Expected rebuild to trigger on dependency change, but got clean skip"
    cat "$TEST_DIR/toka_build_3.log"
    exit 1
fi

# Restore lib.tk
mv "$FIXTURE_DIR/src/lib.tk.bak" "$FIXTURE_DIR/src/lib.tk"
# Rebuild to return to clean state
(cd "$FIXTURE_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="../../../lib" "$TOKA_ABS" build) >/dev/null

echo "PASS: Test 7.3"

# Test 7.4: Delete output trigger rebuild
echo "Test 7.4: Deleting build output and verifying rebuild"
rm -f "$FIXTURE_DIR/target/debug/incremental_project"
(cd "$FIXTURE_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="../../../lib" "$TOKA_ABS" build) > "$TEST_DIR/toka_build_4.log"
if grep -q "All targets are clean. Nothing to compile!" "$TEST_DIR/toka_build_4.log"; then
    echo "FAIL: Expected rebuild on missing target executable, but got clean skip"
    exit 1
fi
if [ ! -f "$FIXTURE_DIR/target/debug/incremental_project" ]; then
    echo "FAIL: Failed to compile missing output"
    exit 1
fi
echo "PASS: Test 7.4"

# Clean up fixture builds
rm -rf "$FIXTURE_DIR/.toka"
rm -rf "$FIXTURE_DIR/target"
echo "PASS: Test 7"

# 9. Test 8: Residue build.tki poisoning self-skip test
echo "Test 8: Checking build.tki residue self-skip poisoning"
TEST_8_DIR="$TEST_DIR/test_8_project"
mkdir -p "$TEST_8_DIR"
WORKSPACE_DIR="$(resolve_abs_path .)"

# Generate fake stale build.tki in the root directory
cat << 'EOF' > "$TEST_8_DIR/build.tki"
@meta
compiler_version: 0.0.0
format_version: 1.0.0
target_triple: x86_64-unknown-linux-gnu
source_hash: 1234567890abcdef
content_hash: 1234567890abcdef
@symbols
EOF

# Generate actual build.tk that imports standard library build and calls its symbol
cat << 'EOF' > "$TEST_8_DIR/build.tk"
import build::{Executable};
fn main() -> i32 {
    auto e# = Executable::make(c"test", c"src/main.tk")
    return 0
}
EOF

# Compile build.tk using tokac under its own directory CWD
(cd "$TEST_8_DIR" && TOKA_LIB="$WORKSPACE_DIR/lib" "$TOKAC_ABS" -o build_exe build.tk > build.log 2>&1)
if [ $? -ne 0 ]; then
    echo "FAIL: Test 8 compilation failed, local build.tki was not properly skipped. Log:"
    cat "$TEST_8_DIR/build.log"
    exit 1
fi

if [ ! -f "$TEST_8_DIR/build_exe" ]; then
    echo "FAIL: Test 8 did not generate build_exe"
    exit 1
fi

# Run the generated executable
(cd "$TEST_8_DIR" && ./build_exe)
echo "PASS: Test 8"

# 10. Test 9: Three-tier topology compilation order check
echo "Test 9: Verifying dependency compilation order matching topological sorted post-order"
TEST_9_DIR="$TEST_DIR/test_9_project"
mkdir -p "$TEST_9_DIR"

# Create leaf.tk
cat << 'EOF' > "$TEST_9_DIR/leaf.tk"
pub fn leaf_func() -> i32 {
    return 100
}
EOF

# Create mid.tk which depends on leaf
cat << 'EOF' > "$TEST_9_DIR/mid.tk"
import ./leaf::{leaf_func}
pub fn mid_func() -> i32 {
    return leaf_func() + 50
}
EOF

# Create root.tk which depends on mid
cat << 'EOF' > "$TEST_9_DIR/root.tk"
import ./mid::{mid_func}
fn main() -> i32 {
    auto v = mid_func()
    return v
}
EOF

# Resolve absolute paths
LEAF_ABS="$(resolve_abs_path "$TEST_9_DIR/leaf.tk")"
MID_ABS="$(resolve_abs_path "$TEST_9_DIR/mid.tk")"
ROOT_ABS="$(resolve_abs_path "$TEST_9_DIR/root.tk")"

# Perform first full build
python3 "$PYTHON_BUILD_DRIVER" --build -m "$TEST_9_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_9_DIR/app" "$TEST_9_DIR/root.tk" > "$TEST_9_DIR/build_1.log" 2>&1
if [ ! -f "$TEST_9_DIR/app" ]; then
    echo "FAIL: Test 9 first build failed. Log:"
    cat "$TEST_9_DIR/build_1.log"
    exit 1
fi

# Assert run output is 150 (check exit code)
set +e
"$TEST_9_DIR/app"
EXIT_CODE=$?
set -e
if [ $EXIT_CODE -ne 150 ]; then
    echo "FAIL: Test 9 first run output mismatch: expected 150, got $EXIT_CODE"
    exit 1
fi

# Modify leaf.tk to trigger incremental rebuild of leaf and mid
# Since leaf changes, both leaf and mid are dirty.
# Leaf should compile before mid!
cat << 'EOF' > "$TEST_9_DIR/leaf.tk"
pub fn leaf_func() -> i32 {
    return 200
}
EOF

# Run toka_build.py with --build and capture logs
python3 "$PYTHON_BUILD_DRIVER" --build -m "$TEST_9_DIR/manifest.json" --tokac "$TOKAC_ABS" --compiler-args "-o $TEST_9_DIR/app" "$TEST_9_DIR/root.tk" > "$TEST_9_DIR/build_2.log" 2>&1
if [ $? -ne 0 ]; then
    echo "FAIL: Test 9 incremental build failed. Log:"
    cat "$TEST_9_DIR/build_2.log"
    exit 1
fi

# Validate order in the logs:
# The log for "Compiling dependency submodule: ...leaf.tk" must appear BEFORE "Compiling dependency submodule: ...mid.tk"
# We can check the line numbers of these messages in build_2.log
LEAF_LINE=$(grep -n "Compiling dependency submodule:.*leaf.tk" "$TEST_9_DIR/build_2.log" | cut -d: -f1 || echo "")
MID_LINE=$(grep -n "Compiling dependency submodule:.*mid.tk" "$TEST_9_DIR/build_2.log" | cut -d: -f1 || echo "")

if [ -z "$LEAF_LINE" ] || [ -z "$MID_LINE" ]; then
    echo "FAIL: Expected both leaf.tk and mid.tk to be recompiled as dirty dependency submodules, log:"
    cat "$TEST_9_DIR/build_2.log"
    exit 1
fi

if [ "$LEAF_LINE" -ge "$MID_LINE" ]; then
    echo "FAIL: Compilation order violation. leaf.tk ($LEAF_LINE) should be compiled before mid.tk ($MID_LINE), log:"
    cat "$TEST_9_DIR/build_2.log"
    exit 1
fi

# Run again and assert output is 250 (check exit code)
set +e
"$TEST_9_DIR/app"
EXIT_CODE=$?
set -e
if [ $EXIT_CODE -ne 250 ]; then
    echo "FAIL: Test 9 run 2 output mismatch: expected 250, got $EXIT_CODE"
    exit 1
fi

echo "PASS: Test 9"

# 11. Test 10: Native build reference application smoke and source-less replay
echo "Test 10: Running native build reference application qualification"
tools/scripts/test_native_build_reference.sh
python3 tools/scripts/qualify_native_build.py \
    --cycles "${TOKA_NATIVE_BUILD_SMOKE_CYCLES:-3}" \
    --report "$TEST_DIR/native_build_reference_report.json"
echo "PASS: Test 10"

# Clean up
rm -rf "$TEST_DIR"
echo "=================================================="
echo "All Toka Incremental Build Tests PASSED!"
echo "=================================================="
