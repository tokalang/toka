#!/usr/bin/env python3

"""Qualify deterministic tokalsp transport failures without signal handlers."""

import argparse
import json
import subprocess
from pathlib import Path
import sys

from test_lsp_protocol import LspClient


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    suffix = ".exe" if sys.platform == "win32" else ""
    tokalsp = (root / args.build_dir / "bin" / ("tokalsp" + suffix)).resolve()
    require(tokalsp.is_file(), "tokalsp binary is missing")

    cases = [
        ("eof", b"", "unexpected EOF before message headers"),
        ("truncated-header", b"Content-Length: 2\r\n",
         "truncated message headers"),
        ("malformed-header", b"Not-A-Header\r\n\r\n",
         "malformed message header"),
        ("missing-length", b"Content-Type: application/json\r\n\r\n",
         "missing Content-Length header"),
        ("invalid-length", b"Content-Length: 2x\r\n\r\n{}",
         "invalid Content-Length header"),
        ("duplicate-length",
         b"Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
         "duplicate Content-Length header"),
        ("short-body", b"Content-Length: 10\r\n\r\n{}",
         "truncated message body"),
    ]

    receipts = []
    for name, frame, expected in cases:
        process = subprocess.run(
            [str(tokalsp)], input=frame, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, timeout=5,
        )
        stderr = process.stderr.decode("utf-8", errors="replace")
        require(process.returncode == 1,
                "%s returned %d instead of 1" % (name, process.returncode))
        require(expected in stderr,
                "%s missed diagnostic %r: %s" % (name, expected, stderr))
        receipts.append({
            "case": name,
            "returncode": process.returncode,
            "diagnostic": expected,
        })

    client_receipts = []
    client = LspClient([tokalsp], root, None)
    client.process.stdin.close()
    returncode = client.process.wait(timeout=5)
    try:
        client.receive(lambda _: False, timeout=2)
        raise RuntimeError("LspClient accepted server EOF")
    except RuntimeError as error:
        require("returncode=%s" % returncode in str(error),
                "LspClient EOF omitted returncode: " + str(error))
    client_receipts.append({"case": "client-eof", "returncode": returncode})

    client = LspClient([tokalsp], root, None)
    client.process.terminate()
    returncode = client.process.wait(timeout=5)
    try:
        client.receive(lambda _: False, timeout=2)
        raise RuntimeError("LspClient accepted terminated server")
    except RuntimeError as error:
        require("returncode=%s" % returncode in str(error),
                "LspClient termination omitted returncode: " + str(error))
    client_receipts.append({
        "case": "client-external-termination",
        "returncode": returncode,
    })

    print(json.dumps({
        "schema": "toka.lsp-transport-failures",
        "version": 1,
        "result": "pass",
        "cases": receipts,
        "clientCases": client_receipts,
    }, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
