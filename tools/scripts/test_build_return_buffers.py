#!/usr/bin/env python3
"""Exercise the actual private build helpers and their string-buffer lifetime."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/semantics/stage1_return_matrix/build_return_buffers.tk.inc"


def qualify(build_dir):
    tokac = Path(build_dir).resolve() / "bin/tokac"
    clang = next((p for p in (
        "/opt/homebrew/opt/llvm@20/bin/clang",
        "/opt/homebrew/opt/llvm/bin/clang", shutil.which("clang"))
        if p and Path(p).is_file()), None)
    if not clang:
        raise RuntimeError("clang is required for buffer-release instrumentation")
    with tempfile.TemporaryDirectory(prefix="toka-build-return-buffers-") as tmp:
        work = Path(tmp)
        library = work / "lib"
        # Physical copies keep trusted source modules beneath TOKA_LIB even
        # when the resolver canonicalizes their paths.
        shutil.copytree(ROOT / "lib", library,
                        ignore=shutil.ignore_patterns("*.o", "*.tki", "__pycache__"))
        # Observe releases in the test copy, immediately before the unchanged
        # free instruction. Both destruction and capacity growth use this
        # instruction, so an early release cannot go unnoticed.
        string_source = (ROOT / "lib/core/string.tk").read_text()
        release = "unsafe free [0]self.*#buf"
        if string_source.count(release) != 2:
            raise RuntimeError("string release sites changed; review instrumentation")
        (library / "core/string.tk").write_text(
            "extern fn test_buffer_freed(nul *ptr: void) -> void\n" +
            string_source.replace(release,
                "test_buffer_freed(self.*buf as nul *void); " + release))
        shim = work / "tracker.c"
        shim.write_text('''#include <stdlib.h>
static void *watched;
static int releases;
void test_buffer_watch(void *p) { watched = p; releases = 0; }
int test_buffer_releases(void) { return releases; }
void test_buffer_freed(void *p) {
    if (p && p == watched) ++releases;
}
''')
        tracker = work / "tracker.o"
        subprocess.run([clang, "-c", str(shim), "-o", str(tracker)], check=True)
        source = work / "build_probe.tk"
        # Same-module suffix can call the real private helpers without changing
        # their visibility or maintaining a second implementation in the test.
        source.write_text((ROOT / "lib/build.tk").read_text() + "\n" +
                          FIXTURE.read_text())
        executable = work / "probe"
        env = dict(os.environ, TOKA_LIB=str(library))
        built = subprocess.run(
            [str(tokac), str(source), str(tracker), "-o", str(executable)],
            cwd=work, env=env, capture_output=True, text=True, timeout=90)
        if built.returncode:
            raise RuntimeError(built.stderr)
        ran = subprocess.run([str(executable)], cwd=work, env=env,
                             capture_output=True, text=True, timeout=30)
        if ran.returncode:
            raise RuntimeError("buffer lifetime/JSON test failed: " +
                               str(ran.returncode) + "\n" + ran.stderr)
    print("build return buffers: pass (3 helpers, capacity/address reuse, exact-once free, 5 parsers)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    qualify(parser.parse_args().build_dir)
