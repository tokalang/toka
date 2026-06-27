#!/bin/sh
set -e

# Toka Language Installer
# This script installs the Toka language toolchain (tokac, toka, tokafmt + stdlib).

echo "Installing Toka Language..."

OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

if [ "$OS" = "darwin" ]; then
  OS="macos"
elif echo "$OS" | grep -qE "(mingw|msys|cygwin)"; then
  OS="windows"
fi

if [ "$ARCH" = "x86_64" ]; then
  ARCH="x64"
elif [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
  ARCH="arm64"
fi

# We use 'latest' endpoint or we let user specify version.
# For now, it curls latest.
RELEASE_URL="https://github.com/tokalang/toka/releases/latest/download"

# To specify a specific version for beta, use the argument passed or latest
VERSION=${1:-"latest"}
if [ "$VERSION" = "latest" ]; then
  echo "Fetching latest version..."
  LATEST_URL=$(curl -sL -o /dev/null -w %{url_effective} https://github.com/tokalang/toka/releases/latest)
  VERSION=${LATEST_URL##*/}
  if [ -z "$VERSION" ] || [ "$VERSION" = "latest" ]; then
    # Fallback if redirect fails
    VERSION="v0.9.8-06"
    echo "Warning: Could not determine latest version. Defaulting to $VERSION"
  fi
fi

TARBALL="toka-${VERSION}-${OS}-${ARCH}.tar.gz"
DOWNLOAD_URL="https://github.com/tokalang/toka/releases/download/${VERSION}/${TARBALL}"

TOKA_DIR="$HOME/.toka"
TMP_DIR=$(mktemp -d)

# Download
echo "Downloading $TARBALL from $DOWNLOAD_URL..."
curl -# -L -o "${TMP_DIR}/${TARBALL}" "$DOWNLOAD_URL"

# Extract
rm -rf "${TOKA_DIR}"
mkdir -p "${TOKA_DIR}"
echo "Extracting..."
tar -xzf "${TMP_DIR}/${TARBALL}" -C "$TMP_DIR" || { echo "Extraction failed."; exit 1; }

# Move files to ~/.toka
# The tarball unzips as an inner directory named toka-VERSION-OS-ARCH.
INNER_DIR="${TMP_DIR}/toka-${VERSION}-${OS}-${ARCH}"
if [ ! -d "$INNER_DIR" ]; then
  echo "Expected directory $INNER_DIR not found. Installation failed."
  exit 1
fi
cp -a "${INNER_DIR}/"* "${TOKA_DIR}/"
rm -rf "$TMP_DIR"

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
