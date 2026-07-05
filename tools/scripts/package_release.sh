#!/usr/bin/env bash
set -e

# Usage: ./package_release.sh [version]
VERSION=${1:-"v0.9.8-07"}
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

# Clean old directory
rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}/bin"
mkdir -p "${PACKAGE_DIR}/lib"

# Verify and copy binaries
MISSING_BINARIES=0
EXPECTED_BINS=()

if [ "$OS" = "windows" ]; then
    EXPECTED_BINS=("tokac.exe" "toka.exe" "tokafmt.exe" "tokalsp.exe")
else
    EXPECTED_BINS=("tokac" "toka" "tokafmt" "tokalsp")
fi

for bin in "${EXPECTED_BINS[@]}"; do
    if [ ! -f "build/bin/${bin}" ]; then
        echo "❌ Error: Required binary 'build/bin/${bin}' not found!"
        MISSING_BINARIES=1
    fi
done

if [ "$MISSING_BINARIES" -ne 0 ]; then
    echo "❌ Error: Packaging aborted due to missing core binaries."
    exit 1
fi

for bin in "${EXPECTED_BINS[@]}"; do
    cp -a "build/bin/${bin}" "${PACKAGE_DIR}/bin/"
done

# Copy standard library
cp -a lib/* "${PACKAGE_DIR}/lib/"

# Copy meta files
cp README.md "${PACKAGE_DIR}/" || true
cp LICENSE "${PACKAGE_DIR}/" || true

# Archive
cd build
tar -czvf "${PACKAGE_NAME}.tar.gz" "${PACKAGE_NAME}"
cd ..

echo ""
echo "✅ Packaged successfully: build/${PACKAGE_NAME}.tar.gz"
