#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOKAC="${TOKAC:-$REPO_ROOT/build/bin/tokac}"
if [[ "$TOKAC" != /* ]]; then
    TOKAC="$REPO_ROOT/$TOKAC"
fi

WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/toka-place-security.XXXXXX")"
trap 'rm -rf "$WORK_ROOT"' EXIT
mkdir -p "$WORK_ROOT/lib/std"
ln -s "$REPO_ROOT/lib/core" "$WORK_ROOT/lib/core"
ln -s "$REPO_ROOT/lib/sys" "$WORK_ROOT/lib/sys"

cat > "$WORK_ROOT/main.tk" <<'EOF'
import std/vec::{ProbeIter}

fn main() -> i32 { return 0 }
EOF

# Control: the canonical toolchain std/vec provider may yield a place rooted
# in its self-backed storage.
cat > "$WORK_ROOT/lib/std/vec.tk" <<'EOF'
import core/traits::{@PlaceIterator, __PlaceOutcome}

pub shape ProbeIter(&buf: [i32])

impl ProbeIter@PlaceIterator {
    type Item = i32
    pub fn next_place(self#) -> __PlaceOutcome<Item> <- self {
        unsafe {
            return __place_hit<Item>('(self.buf[0]))
        }
    }
}
EOF

(
    cd "$WORK_ROOT"
    TOKA_LIB="$WORK_ROOT/lib" "$TOKAC" --check-only main.tk \
        > good.out 2> good.err
)

# Redline: the same canonical provider cannot return a place rooted in a local
# whose lifetime ends with next_place.
cat > "$WORK_ROOT/lib/std/vec.tk" <<'EOF'
import core/traits::{@PlaceIterator, __PlaceOutcome}

pub shape ProbeIter(&buf: [i32])
shape LocalBox(value: i32 = 0)

impl ProbeIter@PlaceIterator {
    type Item = i32
    pub fn next_place(self#) -> __PlaceOutcome<Item> <- self {
        auto local = LocalBox(value = 7)
        return __place_hit<Item>('(local.value))
    }
}
EOF

if (
    cd "$WORK_ROOT"
    TOKA_LIB="$WORK_ROOT/lib" "$TOKAC" --check-only main.tk \
        > bad.out 2> bad.err
); then
    echo "FAIL: canonical next_place accepted a local-stack place"
    exit 1
fi
if ! grep -Fq "error[E04648]" "$WORK_ROOT/bad.err"; then
    echo "FAIL: dangling place provider missed E04648"
    cat "$WORK_ROOT/bad.err"
    exit 1
fi

echo "PASS: canonical PlaceIterator provenance redline"
