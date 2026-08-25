#!/bin/sh
set -e

# Canonical Toka Language Installer
# This script installs the Toka language toolchain (tokac, toka, tokafmt + stdlib).

echo "Installing Toka Language..."

OS=$(uname -s)
ARCH=$(uname -m)

case "$OS" in
  Darwin) OS="macos" ;;
  Linux) OS="linux" ;;
  MINGW*|MSYS*|CYGWIN*) OS="windows" ;;
  *)
    echo "Unsupported operating system: $OS"
    exit 1
    ;;
esac

if [ "$ARCH" = "x86_64" ]; then
  ARCH="x64"
elif [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
  ARCH="arm64"
else
  echo "Unsupported architecture: $ARCH"
  exit 1
fi

# Pass an exact tag for a release candidate. The unqualified path deliberately
# follows GitHub's stable Latest release instead of guessing a prerelease.
VERSION=${1:-"latest"}
if [ "$VERSION" = "latest" ]; then
  echo "Fetching latest version..."
  if ! LATEST_URL=$(curl -fsSL --retry 3 -o /dev/null -w '%{url_effective}' \
      https://github.com/tokalang/toka/releases/latest); then
    echo "Could not determine the latest Toka release. Pass an exact release tag instead."
    exit 1
  fi
  VERSION=${LATEST_URL##*/}
fi

case "$VERSION" in
  v*) ;;
  *)
    echo "Invalid release tag '$VERSION'. Pass a tag such as v1.0.0-rc.8."
    exit 1
    ;;
esac

TARBALL="toka-${VERSION}-${OS}-${ARCH}.tar.gz"
DOWNLOAD_URL="https://github.com/tokalang/toka/releases/download/${VERSION}/${TARBALL}"

TOKA_DIR="$HOME/.toka"
TMP_DIR=$(mktemp -d)
STAGING_DIR="${TOKA_DIR}.staging.$$"
BACKUP_DIR="${TOKA_DIR}.backup.$$"

cleanup() {
  rm -rf "$TMP_DIR"
  if [ -n "${STAGING_DIR:-}" ]; then
    rm -rf "$STAGING_DIR"
  fi
}
trap cleanup EXIT HUP INT TERM

# Download
echo "Downloading $TARBALL from $DOWNLOAD_URL..."
curl --fail --location --retry 3 --silent --show-error \
  -o "${TMP_DIR}/${TARBALL}" "$DOWNLOAD_URL"

# Extract and validate before touching an existing installation.
echo "Extracting and validating..."
tar -xzf "${TMP_DIR}/${TARBALL}" -C "$TMP_DIR" || { echo "Extraction failed."; exit 1; }

INNER_DIR="${TMP_DIR}/toka-${VERSION}-${OS}-${ARCH}"
if [ ! -d "$INNER_DIR" ]; then
  echo "Expected directory $INNER_DIR not found. Installation failed."
  exit 1
fi
BIN_SUFFIX=""
if [ "$OS" = "windows" ]; then
  BIN_SUFFIX=".exe"
fi
for tool in tokac toka tokafmt tokalsp; do
  if [ ! -f "${INNER_DIR}/bin/${tool}${BIN_SUFFIX}" ]; then
    echo "Release archive is missing required SDK tool: ${tool}${BIN_SUFFIX}"
    exit 1
  fi
done
if [ ! -d "${INNER_DIR}/lib" ]; then
  echo "Release archive is missing the Toka standard library."
  exit 1
fi

rm -rf "$STAGING_DIR" "$BACKUP_DIR"
mkdir -p "$STAGING_DIR"
cp -a "${INNER_DIR}/." "$STAGING_DIR/"

if [ -e "$TOKA_DIR" ] || [ -L "$TOKA_DIR" ]; then
  mv "$TOKA_DIR" "$BACKUP_DIR"
fi
if ! mv "$STAGING_DIR" "$TOKA_DIR"; then
  echo "Could not activate the new Toka installation."
  if [ -e "$BACKUP_DIR" ] || [ -L "$BACKUP_DIR" ]; then
    mv "$BACKUP_DIR" "$TOKA_DIR"
  fi
  exit 1
fi
STAGING_DIR=""
rm -rf "$BACKUP_DIR"

echo "Toka Language has been installed to ${TOKA_DIR}."

# Add to path
BIN_DIR="${TOKA_DIR}/bin"
export PATH="$BIN_DIR:$PATH"

PROFILE_FILE=""
if [ -n "$BASH_VERSION" ]; then
  if [ -f "$HOME/.bash_profile" ]; then PROFILE_FILE="$HOME/.bash_profile"; 
  elif [ -f "$HOME/.bashrc" ]; then PROFILE_FILE="$HOME/.bashrc"; fi
elif [ -n "$ZSH_VERSION" ] || [ "$(basename "$SHELL")" = "zsh" ]; then
  PROFILE_FILE="$HOME/.zshrc"
fi

# Fallback profile search
if [ -z "$PROFILE_FILE" ]; then
  if [ -f "$HOME/.zshrc" ]; then PROFILE_FILE="$HOME/.zshrc"
  elif [ -f "$HOME/.bash_profile" ]; then PROFILE_FILE="$HOME/.bash_profile"
  elif [ -f "$HOME/.bashrc" ]; then PROFILE_FILE="$HOME/.bashrc"
  fi
fi

if [ -n "$PROFILE_FILE" ]; then
  if ! grep -q 'export PATH="\$HOME/.toka/bin:\$PATH"' "$PROFILE_FILE"; then
    echo "export PATH=\"\$HOME/.toka/bin:\$PATH\"" >> "$PROFILE_FILE"
    echo "Added \$HOME/.toka/bin to PATH in $PROFILE_FILE."
  fi
  if ! grep -q 'export TOKA_LIB="\$HOME/.toka/lib"' "$PROFILE_FILE"; then
    echo "export TOKA_LIB=\"\$HOME/.toka/lib\"" >> "$PROFILE_FILE"
    echo "Added TOKA_LIB to $PROFILE_FILE."
  fi
  echo "Please restart your shell or run: source $PROFILE_FILE"
else
  echo "Please add $BIN_DIR to your PATH and set TOKA_LIB=$HOME/.toka/lib manually."
fi

echo ""
echo "Welcome to Toka! Run 'tokac --help' to get started."
