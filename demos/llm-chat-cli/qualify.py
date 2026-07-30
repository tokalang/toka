#!/usr/bin/env python3
"""Credential-free compile and usage check for the LLM chat demo."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def main() -> int:
    tokac = ROOT / "build" / "bin" / "tokac"
    if not tokac.is_file():
        raise RuntimeError("build tokac before qualifying the LLM chat demo")
    with tempfile.TemporaryDirectory(prefix="toka-llm-chat-cli-") as work:
        executable = Path(work) / "llm-chat-cli"
        subprocess.run([str(tokac), str(ROOT / "demos" / "llm-chat-cli" / "main.tk"),
                        "-I", str(ROOT / "lib"), "-I", str(ROOT / "official" / "openai_compat" / "lib"),
                        "-o", str(executable)], cwd=ROOT, check=True, timeout=120)
        subprocess.run([str(executable), "--help"], cwd=ROOT, check=True, timeout=30)
    print("llm-chat-cli qualification: PASSED")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
