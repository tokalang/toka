#!/bin/bash
set -e

# Path to the compiler
TOKAC="./build/bin/tokac"
LLI="lli-20"
CLANG="clang-20"

if which lli-20 >/dev/null 2>&1; then
    LLI="lli-20"
    if which clang-20 >/dev/null 2>&1; then
        CLANG="clang-20"
    else
        CLANG="clang"
    fi
elif [ -x "/opt/homebrew/opt/llvm@20/bin/lli" ]; then
    LLI="/opt/homebrew/opt/llvm@20/bin/lli"
    CLANG="/opt/homebrew/opt/llvm@20/bin/clang"
elif [ -x "/usr/local/opt/llvm@20/bin/lli" ]; then
    LLI="/usr/local/opt/llvm@20/bin/lli"
    CLANG="/usr/local/opt/llvm@20/bin/clang"
else
    LLI=$(which lli)
    CLANG=$(which clang)
fi

echo "--- Compiling Toka Build Tool ---"
$TOKAC -I build/generated -I tools/toka tools/toka/src/main.tk > build/toka.ll

echo "Generating toka native binary via $CLANG..."
if [ "$(uname)" == "Darwin" ]; then
    $CLANG build/toka.ll lib/sys/toka_rt.o -lm -isysroot $(xcrun --show-sdk-path) -o build/toka
else
    $CLANG build/toka.ll lib/sys/toka_rt.o -lm -o build/toka
fi

echo "--- Testing 'toka new test_project' ---"
cd build
rm -rf test_project
./toka new test_project
cd test_project

echo "--- Testing 'toka run' (Compiling and Running build.tk) ---"
ls -lah
cat build.tk

# Symlink lib so tokac finds the standard library (Toka searches ./lib and ../lib)
ln -s ../../lib .

# Note: The test environment needs to know where 'tokac' is.
# We'll export PATH so that 'tokac' and 'lli' can be found.
export PATH="$PATH:$(pwd)/../bin"
export TOKA_LLI="$LLI"
export TOKA_CLANG="$CLANG"

../toka run

echo "--- Toka Build System PASS ---"
