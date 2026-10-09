#!/usr/bin/env bash
set -e

# Usage: ./package_release.sh [version]
VERSION=${1:-"v0.13.1"}
if [[ ! "$VERSION" =~ ^v0\.(11|12|13)\.(0|[1-9][0-9]*)$ ]]; then
    echo "Release label must be a canonical v0.11.x/v0.12.x/v0.13.x tag" >&2
    exit 1
fi
OS=${OS:-""}
ARCH=${ARCH:-""}
UNAME_S=$(uname -s)
UNAME_M=$(uname -m)

if [ -z "$OS" ]; then
    if [ "$UNAME_S" = "Darwin" ]; then
        OS="macos"
    elif [ "$UNAME_S" = "Linux" ]; then
        OS="linux"
    elif [[ "$UNAME_S" == MINGW* || "$UNAME_S" == CYGWIN* || "$UNAME_S" == MSYS* ]]; then
        OS="windows"
    else
        echo "Unsupported OS: $UNAME_S"
        exit 1
    fi
fi

if [ -z "$ARCH" ]; then
    if [ "$UNAME_M" = "x86_64" ]; then
        ARCH="x64"
    elif [ "$UNAME_M" = "arm64" ] || [ "$UNAME_M" = "aarch64" ]; then
        ARCH="arm64"
    else
        echo "Unsupported Arch: $UNAME_M"
        exit 1
    fi
fi

PACKAGE_NAME="toka-${VERSION}-${OS}-${ARCH}"
PACKAGE_DIR="build/${PACKAGE_NAME}"

echo "Packaging ${PACKAGE_NAME}..."

# A release archive needs the source-side runtime objects consumed by the
# installed SDK. Prepare them explicitly instead of depending on a prior test
# helper having populated lib/sys.
if [ ! -f "lib/sys/toka_rt.o" ] || [ ! -f "lib/sys/llvm_shim.o" ]; then
    python3 tools/scripts/test_pass.py --prepare-runtime-only
fi
for runtime_object in lib/sys/toka_rt.o lib/sys/llvm_shim.o; do
    if [ ! -f "$runtime_object" ]; then
        echo "Error: required runtime object '$runtime_object' was not prepared."
        exit 1
    fi
done

# Clean old directory
rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}/bin"
mkdir -p "${PACKAGE_DIR}/lib"

# Verify and copy binaries
BINARY_BUILD_DIR=${BINARY_BUILD_DIR:-build}
MISSING_BINARIES=0
EXPECTED_BINS=()

if [ "$OS" = "windows" ]; then
    EXPECTED_BINS=("tokac.exe" "toka.exe" "tokafmt.exe" "tokalsp.exe")
else
    EXPECTED_BINS=("tokac" "toka" "tokafmt" "tokalsp")
fi

for bin in "${EXPECTED_BINS[@]}"; do
    if [ ! -f "${BINARY_BUILD_DIR}/bin/${bin}" ]; then
        echo "❌ Error: Required binary 'build/bin/${bin}' not found!"
        MISSING_BINARIES=1
    fi
done

if [ "$MISSING_BINARIES" -ne 0 ]; then
    echo "❌ Error: Packaging aborted due to missing core binaries."
    exit 1
fi

for bin in "${EXPECTED_BINS[@]}"; do
    cp -a "${BINARY_BUILD_DIR}/bin/${bin}" "${PACKAGE_DIR}/bin/"
done

# Copy standard library
cp -a lib/* "${PACKAGE_DIR}/lib/"
find "${PACKAGE_DIR}" -type d -name __pycache__ -prune -exec rm -rf {} +
find "${PACKAGE_DIR}" -type f -name '*.pyc' -delete

# These SDK helpers are invoked by the installed manager. Keep the release
# archive's toolchain directory in lockstep with CMake install().
for helper in toka_build.py semantic_diff_preview.py; do
    if [ ! -f "tools/scripts/${helper}" ]; then
        echo "Error: Required SDK helper 'tools/scripts/${helper}' not found!"
        exit 1
    fi
    cp -a "tools/scripts/${helper}" "${PACKAGE_DIR}/lib/toolchain/"
done

# The release-matched completion card is the compact, offline entry point for
# AI-assisted authors. Keep the README link valid in a release archive.
if [ ! -f "docs/ai_completion_card.md" ]; then
    echo "Error: Required AI completion card 'docs/ai_completion_card.md' not found!"
    exit 1
fi
mkdir -p "${PACKAGE_DIR}/docs"
cp -a docs/ai_completion_card.md "${PACKAGE_DIR}/docs/"

if [[ "$VERSION" == v0.13.* ]]; then
    cp docs/package_entry_contract.md docs/package_entry_example.md docs/diagnostic_d1_d2_migration.md docs/toka_test_lock_codes.md "${PACKAGE_DIR}/docs/"
fi

# Copy meta files
cp README.md "${PACKAGE_DIR}/" || true
cp LICENSE "${PACKAGE_DIR}/" || true

# Bind standard 0.13 archives to the source and all four real binaries.
if [[ "$VERSION" == v0.13.* ]]; then
    python3 - "$PACKAGE_DIR" "$VERSION" "$BINARY_BUILD_DIR" <<'PYCODE'
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]);label=sys.argv[2]
if 'BUILD_TESTING:BOOL=OFF' not in (Path(sys.argv[3])/'CMakeCache.txt').read_text():raise SystemExit('0.13 distribution requires a standard BUILD_TESTING=OFF build')
revision=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
dirty=bool(subprocess.check_output(['git','-c','core.fsmonitor=false','status','--porcelain','--untracked-files=no']).strip())
if dirty:raise SystemExit('0.13 package requires a frozen clean tracked candidate')
tools={}
for name in ('tokac','toka','tokafmt','tokalsp'):
    path=root/'bin'/name;r=subprocess.run([str(path.resolve()),'--version'],capture_output=True)
    if r.returncode!=0 or label[1:].encode() not in r.stdout+r.stderr:raise SystemExit('tool identity mismatch: '+name)
    tools[name]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'stdout':r.stdout.decode(),'stderr':r.stderr.decode(),'exit_code':r.returncode}
(root/'sdk.json').write_text(json.dumps({'schema':'toka.sdk-identity','version':1,'version_label':label,'candidate_revision':revision,'source_dirty':False,'build_testing':False,'tools':tools},indent=2)+'\n')
PYCODE
fi

# Archive
cd build
tar -czvf "${PACKAGE_NAME}.tar.gz" "${PACKAGE_NAME}"
cd ..

echo ""
echo "✅ Packaged successfully: build/${PACKAGE_NAME}.tar.gz"
