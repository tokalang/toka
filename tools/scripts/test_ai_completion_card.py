#!/usr/bin/env python3

"""Keep executable AI Completion Card source facts honest."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
CARD = ROOT / "docs/ai_completion_card.md"
FIXTURE = ROOT / "tests/tooling/ai_completion_card/checked_decimal_parse.tk"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    suffix = ".exe" if sys.platform == "win32" else ""
    tokac = ROOT / args.build_dir / "bin" / ("tokac" + suffix)
    require(tokac.is_file(), "tokac is missing")
    card = CARD.read_text(encoding="utf-8")
    require("text.as_str().as_bytes().at(index).unwrap() as i32" in card,
            "completion card must use the byte lookup API")
    require("text.as_str().at(index).unwrap() as i32" not in card,
            "completion card must not describe Unicode-scalar lookup as byte lookup")

    with tempfile.TemporaryDirectory(prefix="toka-completion-card-") as temp:
        executable = Path(temp) / ("checked_decimal_parse" + suffix)
        compile_result = subprocess.run([str(tokac), str(FIXTURE), "-o", str(executable)],
                                        cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE)
        require(compile_result.returncode == 0, "completion-card fixture failed to compile:\n%s%s" %
                (compile_result.stdout, compile_result.stderr))
        run_result = subprocess.run([str(executable)], cwd=ROOT, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(run_result.returncode == 0, "completion-card fixture failed:\n%s%s" %
                (run_result.stdout, run_result.stderr))
    print("AI Completion Card executable facts gate PASSED")


if __name__ == "__main__":
    main()
