#!/usr/bin/env python3
"""Build and run the isolated POSIX handoff runtime, without enabling std/thread."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    if sys.platform not in ("darwin", "linux", "freebsd", "openbsd", "netbsd"):
        raise SystemExit("thread handoff v1 qualification requires a POSIX pthread host")
    compiler = shlex.split(os.environ.get("CC", "cc"))
    common = compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
                         "-I", str(root / "lib/sys")]
    runtime = root / "lib/sys/toka_thread_handoff_v1.c"
    with tempfile.TemporaryDirectory(prefix="toka-thread-handoff-v1-") as directory:
        target = Path(directory)
        subprocess.run(common + ["-c", str(runtime), "-o", str(target / "production.o")], check=True)
        executable = target / "runtime-test"
        subprocess.run(common + ["-DTOKA_THREAD_HANDOFF_TESTING",
                                 "-I", str(root / "tests/runtime"), str(runtime),
                                 str(root / "tests/runtime/thread_handoff_v1.c"),
                                 "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=45)
    print("Production build and isolated runtime gate passed; temporary artifacts removed.")


if __name__ == "__main__":
    main()
