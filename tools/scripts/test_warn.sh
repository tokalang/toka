#!/bin/bash

make -C build -j8
if [ $? -ne 0 ]; then
    echo "Compiler Build Failed"
    exit 1
fi

VERIFIER="tools/scripts/test_verify_warn.py"

if [ ! -f "$VERIFIER" ]; then
    echo "Error: Verifier script not found at $VERIFIER"
    exit 1
fi

python3 "$VERIFIER"
exit $?
