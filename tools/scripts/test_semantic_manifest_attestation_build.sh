#!/usr/bin/env bash
# Verify that P2's provenance and final-link profile survive `toka build`,
# including the incremental driver's clean-plan preflight.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd -P)"
TOKAC="${TOKAC:-$ROOT_DIR/build/bin/tokac}"
TOKA="${TOKA:-$ROOT_DIR/build/bin/toka}"
TOKAC_ABS="$(cd "$(dirname "$TOKAC")" && pwd -P)/$(basename "$TOKAC")"
TOKA_ABS="$(cd "$(dirname "$TOKA")" && pwd -P)/$(basename "$TOKA")"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/toka_manifest_p2_build.XXXXXX")"
PROJECT_DIR="$TEST_DIR/project"
STATE_DIR="$TEST_DIR/compiler-state"
trap 'rm -rf "$TEST_DIR"' EXIT

if [[ ! -x "$TOKAC_ABS" || ! -x "$TOKA_ABS" ]]; then
    echo "FAIL: tokac and toka must be built before this test" >&2
    exit 1
fi

mkdir -p "$PROJECT_DIR/src" "$STATE_DIR"
cat > "$PROJECT_DIR/package.tk" <<'EOF'
pub const PACKAGE = (
    name = "semantic_manifest_p2_build_profile",
    version = "0.1.0",
    dependencies = (
    )
)
EOF
cat > "$PROJECT_DIR/build.tk" <<'EOF'
import build::{Executable, run_build}

fn main() -> i32 {
    auto app# = Executable::make(c"semantic_manifest_p2_build_profile", c"src/main.tk")
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

P2_ARGS=(--validate-semantic-manifest-attestations
         --semantic-manifest-provenance-dir "$STATE_DIR")
if ! (cd "$PROJECT_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$ROOT_DIR/lib" \
    "$TOKA_ABS" build "${P2_ARGS[@]}" \
    > "$TEST_DIR/build-first.out" 2> "$TEST_DIR/build-first.err"); then
    echo "FAIL: toka build could not produce and consume a P2 provider" >&2
    sed 's/^/  | /' "$TEST_DIR/build-first.err" >&2
    exit 1
fi

tsm_files=("$PROJECT_DIR"/.toka/build/interfaces/*.tki.tsm)
if [[ ${#tsm_files[@]} -ne 1 ]] ||
   ! grep -Fq '"payload_schema":"toka.outcome-fulfilment-p2"' \
       "${tsm_files[0]}"; then
    echo "FAIL: toka build did not produce a P2 provider sidecar" >&2
    exit 1
fi
object_files=("$PROJECT_DIR"/.toka/build/objects/*.o)
if [[ ${#object_files[@]} -ne 1 ]]; then
    echo "FAIL: toka build did not retain the P2 provider object" >&2
    exit 1
fi

# Force the next clean plan to select the cached bodyless interface.
cp "${tsm_files[0]%.tsm}" "$PROJECT_DIR/src/lib.tki"
mv "$PROJECT_DIR/src/lib.tk" "$PROJECT_DIR/src/lib.tk.source-hidden"
if ! (cd "$PROJECT_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$ROOT_DIR/lib" \
    "$TOKA_ABS" build "${P2_ARGS[@]}" \
    > "$TEST_DIR/build-clean.out" 2> "$TEST_DIR/build-clean.err"); then
    echo "FAIL: clean toka build rejected a valid P2 cache provider" >&2
    sed 's/^/  | /' "$TEST_DIR/build-clean.err" >&2
    exit 1
fi

printf 'tampered' >> "${object_files[0]}"
if (cd "$PROJECT_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$ROOT_DIR/lib" \
    "$TOKA_ABS" build "${P2_ARGS[@]}" \
    > "$TEST_DIR/build-tampered.out" 2> "$TEST_DIR/build-tampered.err"); then
    echo "FAIL: P2 profile accepted a tampered cache object on a clean build" >&2
    exit 1
fi
if ! grep -Fq 'E04634' "$TEST_DIR/build-tampered.err" ||
   ! grep -Fq 'ObjectMismatch' "$TEST_DIR/build-tampered.err"; then
    echo "FAIL: clean P2 build did not report the object attestation failure" >&2
    sed 's/^/  | /' "$TEST_DIR/build-tampered.err" >&2
    exit 1
fi

# The profile is invocation-scoped: default source-less replay remains Level A
# and rejects a bodyless provider rather than trusting its sidecar implicitly.
if (cd "$PROJECT_DIR" && TOKAC="$TOKAC_ABS" TOKA_LIB="$ROOT_DIR/lib" \
    "$TOKA_ABS" build > "$TEST_DIR/default-clean.out" \
    2> "$TEST_DIR/default-clean.err"); then
    echo "FAIL: default clean build implicitly consumed a P2 sidecar" >&2
    exit 1
fi
if ! grep -Fq 'E04631' "$TEST_DIR/default-clean.err"; then
    echo "FAIL: default clean build lost the Level-A bodyless boundary" >&2
    sed 's/^/  | /' "$TEST_DIR/default-clean.err" >&2
    exit 1
fi

echo "PASS: semantic manifest P2 build profile"
