#!/usr/bin/env python3
"""Ensure ordinary audit failures retain stderr and their input sidecars."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch


def main():
    spec = importlib.util.spec_from_file_location(
        "fz3", Path(__file__).with_name("audit_fz3_reliability.py"))
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    with tempfile.TemporaryDirectory(prefix="toka-fz3-diagnostics-control-") as temp:
        root = Path(temp)
        audit.DIAGNOSTICS = root / "diagnostics"
        audit.DIAGNOSTICS.mkdir()
        audit.WORKSPACE = root / "workspace"
        audit.WORKSPACE.mkdir()
        source = audit.WORKSPACE / "main.tk"
        source.write_text("import ./lib::{make}\n")
        sidecar = audit.WORKSPACE / "lib.tki"
        sidecar.write_bytes(b"retained sidecar control\x00\xff")
        command = [sys.executable, "-c",
                   "import sys; sys.stderr.buffer.write(b'AddressSanitizer: retained control\\n'); sys.exit(1)",
                   str(source)]
        result = audit.run(command, root)
        assert result["returncode"] == 1
        record = audit.FAILURE_RECORDS[0]
        directory = Path(record["path"])
        assert (directory / "stderr.txt").read_bytes() == result["stderr"]
        assert (directory / "inputs/workspace/lib.tki").read_bytes() == sidecar.read_bytes()
        assert (directory / "inputs/workspace/main.tk").read_bytes() == source.read_bytes()
        assert json.loads((directory / "failure.json").read_text())["command"] == command

        # Exercise signal classification without depending on a host debugger.
        with patch.object(audit.shutil, "which", return_value=None), patch.object(
                audit.subprocess, "run", return_value=subprocess.CompletedProcess(
                    command, -6, b"partial stdout", b"assertion failed\n")):
            audit.run(command, root)
        signal = audit.FAILURE_RECORDS[1]
        assert signal["returncode"] == -6 and signal["reason"] == "crashed with signal 6"
        assert (Path(signal["path"]) / "stderr.txt").read_bytes() == b"assertion failed\n"
        audit.run([sys.executable, "-c", "import sys; sys.exit(1)"], root)
        assert len(audit.FAILURE_RECORDS) == 2, "ordinary semantic rejection was misclassified"
    print("FZ3 stderr, signal and sidecar retention controls PASSED")


if __name__ == "__main__":
    main()
