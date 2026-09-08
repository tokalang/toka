#!/usr/bin/env python3
"""Standalone synthetic LLVM adapter / private runtime ABI test, no activation.

Usage: python3 tools/scripts/test_thread_handoff_adapter.py [--llvm-config PATH]
This builds only the new isolated test in a temporary directory; no compiler,
CMake, std/thread, cache, or source-language semantic qualification is implied.
"""
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-config", default=shutil.which("llvm-config"))
    args = parser.parse_args()
    if not args.llvm_config:
        parser.error("llvm-config is required (use --llvm-config)")
    root = Path(__file__).resolve().parents[2]

    def config(*flags):
        return shlex.split(subprocess.check_output([args.llvm_config, *flags], text=True))

    bindir = Path(subprocess.check_output([args.llvm_config, "--bindir"], text=True).strip())
    cxx = bindir / "clang++"
    cc = bindir / "clang"
    with tempfile.TemporaryDirectory(prefix="toka-thread-adapter-") as temporary:
        output = Path(temporary)
        runtime = output / "runtime.o"
        subprocess.run([str(cc), "-std=c11", "-pthread", "-I", str(root / "lib/sys"),
                        "-c", str(root / "lib/sys/toka_thread_handoff_v1.c"),
                        "-o", str(runtime)], check=True)
        command = [str(cxx), *config("--cxxflags"), "-std=c++17", "-pthread",
                   "-I", str(root / "include"), "-I", str(root / "lib/sys"),
                   str(root / "tests/ThreadHandoffAdapter.cpp"),
                   str(root / "src/CodeGen/ThreadHandoffAdapter.cpp"), str(runtime),
                   *config("--ldflags", "--libs", "core", "executionengine", "mcjit", "native", "--system-libs"),
                   "-o", str(output / "adapter-test")]
        subprocess.run(command, check=True)
        subprocess.run([str(output / "adapter-test")], check=True)


if __name__ == "__main__":
    main()
