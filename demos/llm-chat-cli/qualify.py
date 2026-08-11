#!/usr/bin/env python3
"""Credential-free compile and usage check for the LLM chat demo."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "scripts"))
from registry_fixture import materialize_locked_library

def main() -> int:
    tokac = ROOT / "build" / "bin" / "tokac"
    if not tokac.is_file():
        raise RuntimeError("build tokac before qualifying the LLM chat demo")
    with tempfile.TemporaryDirectory(prefix="toka-llm-chat-cli-") as work:
        work = Path(work)
        openai_compat_library = materialize_locked_library(
            ROOT, "registry_openai_compat_consumer", "openai_compat", work)
        executable = work / "llm-chat-cli"
        subprocess.run([str(tokac), str(ROOT / "demos" / "llm-chat-cli" / "main.tk"),
                        "-I", str(ROOT / "lib"), "-I", str(openai_compat_library),
                        "-o", str(executable)], cwd=ROOT, check=True, timeout=120)
        subprocess.run([str(executable), "--help"], cwd=ROOT, check=True, timeout=30)
    print("llm-chat-cli qualification: PASSED")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
