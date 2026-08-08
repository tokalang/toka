#!/usr/bin/env bash
# Verify that the explicit semantic-manifest profile survives `toka build`
# identity forwarding and remains active when the incremental plan is clean.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd -P)"
TOKAC="${TOKAC:-$ROOT_DIR/build/bin/tokac}"
TOKA="${TOKA:-$ROOT_DIR/build/bin/toka}"
TOKAC_ABS="$(cd "$(dirname "$TOKAC")" && pwd -P)/$(basename "$TOKAC")"
TOKA_ABS="$(cd "$(dirname "$TOKA")" && pwd -P)/$(basename "$TOKA")"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/toka_manifest_build.XXXXXX")"
PROJECT_DIR="$TEST_DIR/project"
trap 'rm -rf "$TEST_DIR"' EXIT

if [[ ! -x "$TOKAC_ABS" || ! -x "$TOKA_ABS" ]]; then
    echo "FAIL: tokac and toka must be built before this test" >&2
    exit 1
fi

mkdir -p "$PROJECT_DIR/src"
cat > "$PROJECT_DIR/package.tk" <<'EOF'
pub const PACKAGE = (
    name = "semantic_manifest_build_profile",
    version = "0.1.0",
    dependencies = (
    )
)
EOF
cat > "$PROJECT_DIR/build.tk" <<'EOF'
import build::{Executable, run_build}

fn main() -> i32 {
    auto app# = Executable::make(c"semantic_manifest_build_profile", c"src/main.tk")
    return run_build(app)
}
EOF
cat > "$PROJECT_DIR/src/lib.tk" <<'EOF'
pub shape BuildOutcome(
    Ok(i32) |
    Err(i32)
)

pub fn try_build(init out: i32, fail: bool) -> BuildOutcome
outcomes:
    Ok => out: init
    Err => out: uninit
{
    if fail {
        return BuildOutcome::Err(1)
    }
    init out = 42:i32
    return BuildOutcome::Ok(0)
}
EOF
cat > "$PROJECT_DIR/src/main.tk" <<'EOF'
import ./lib::{BuildOutcome, try_build}

fn main() -> i32 {
    auto value = uninit:i32
    match try_build(init value, false) {
        auto BuildOutcome::Ok(_) => return value - 42:i32
        auto BuildOutcome::Err(_) => return 1
    }
}
EOF

(cd "$PROJECT_DIR" && python3 "$ROOT_DIR/lib/toolchain/toka_package.py" fetch \
    > "$TEST_DIR/fetch.out")
WORKSPACE_NODE="$(cd "$PROJECT_DIR" && \
    python3 "$ROOT_DIR/lib/toolchain/toka_package.py" workspace-node)"
"$TOKAC_ABS" --workspace-node "$WORKSPACE_NODE" \
    --workspace-root "$PROJECT_DIR" -c "$PROJECT_DIR/src/lib.tk" \
    -o "$PROJECT_DIR/src/lib.o"
if [[ ! -f "$PROJECT_DIR/src/lib.tki.tsm" ]]; then
    echo "FAIL: known-coordinate package library did not emit a semantic manifest" >&2
    exit 1
fi
mv "$PROJECT_DIR/src/lib.tk" "$PROJECT_DIR/src/lib.tk.source-hidden"

if ! (cd "$PROJECT_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$ROOT_DIR/lib" \
    "$TOKA_ABS" build --validate-semantic-manifests \
    > "$TEST_DIR/profile-valid.out" 2> "$TEST_DIR/profile-valid.err"); then
    echo "FAIL: toka build rejected a valid semantic manifest" >&2
    sed 's/^/  | /' "$TEST_DIR/profile-valid.err" >&2
    exit 1
fi

sed 's/$/ /' "$PROJECT_DIR/src/lib.tki.tsm" \
    > "$PROJECT_DIR/src/lib.tki.tsm.tampered"
mv "$PROJECT_DIR/src/lib.tki.tsm.tampered" "$PROJECT_DIR/src/lib.tki.tsm"

if ! (cd "$PROJECT_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$ROOT_DIR/lib" \
    "$TOKA_ABS" build > "$TEST_DIR/default-clean.out" \
    2> "$TEST_DIR/default-clean.err"); then
    echo "FAIL: tampered semantic manifest changed default build acceptance" >&2
    sed 's/^/  | /' "$TEST_DIR/default-clean.err" >&2
    exit 1
fi

if (cd "$PROJECT_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$ROOT_DIR/lib" \
    "$TOKA_ABS" build --validate-semantic-manifests \
    > "$TEST_DIR/profile-tampered.out" 2> "$TEST_DIR/profile-tampered.err"); then
    echo "FAIL: semantic-manifest profile accepted a tampered sidecar on a clean build" >&2
    exit 1
fi
if ! grep -Fq "E04633" "$TEST_DIR/profile-tampered.err"; then
    echo "FAIL: clean build profile did not report E04633" >&2
    sed 's/^/  | /' "$TEST_DIR/profile-tampered.err" >&2
    exit 1
fi

echo "PASS: semantic manifest build profile"
