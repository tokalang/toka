#!/usr/bin/env python3
"""Exercise 0.14 G1 static dispatch async trait lifecycle with isolated test observation."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/fixtures/g1_lifecycle/g14_static_async_trait_lifecycle.tk"

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def run(command, env=None, timeout=60, cwd=ROOT):
    argv = [str(x) for x in command]
    start = time.monotonic()
    proc = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout)
    duration_ms = (time.monotonic() - start) * 1000.0
    return proc

def qualify(build_dir):
    build = Path(build_dir).resolve()
    compiler = build / "bin/tokac"
    require(compiler.is_file(), f"compiler not found at {compiler}")
    cc = os.environ.get("CC", "clang")

    with tempfile.TemporaryDirectory(prefix="toka-g1-lifecycle-") as directory:
        work = Path(directory)

        # 1. Verify production runtime isolation (no test tracking symbols)
        prod_rt = work / "toka_rt_production.o"
        res_prod_compile = run([cc, "-c", str(ROOT / "lib/sys/toka_rt.c"), "-o", str(prod_rt)])
        require(res_prod_compile.returncode == 0, f"failed to compile production runtime: {res_prod_compile.stderr}")

        nm_res = run(["nm", "-g", str(prod_rt)])
        require(nm_res.returncode == 0, f"failed to run nm: {nm_res.stderr}")
        symbols = nm_res.stdout
        require("toka_rt_test_track_buffer" not in symbols,
                "toka_rt_test_track_buffer leaked into production runtime")
        require("toka_rt_test_notify_buffer_free" not in symbols,
                "toka_rt_test_notify_buffer_free leaked into production runtime")
        require("toka_rt_test_tracked_buffer" not in symbols,
                "toka_rt_test_tracked_buffer symbols leaked into production runtime")

        # Verify normal Vec program compiles and links cleanly against production runtime
        norm_prog_tk = work / "norm_vec.tk"
        norm_prog_tk.write_text("""import std/vec::{Vec}
fn main() -> i32 {
    auto v# = Vec<i32>::new()
    v#.push(10)
    v#.push(20)
    assert(v.len() == 2:usize, "len is 2")
    return 0
}
""", encoding="utf-8")
        norm_prog_bin = work / "norm_vec"
        norm_env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
        res_norm_build = run([compiler, str(norm_prog_tk), str(prod_rt), "-o", str(norm_prog_bin)], env=norm_env)
        require(res_norm_build.returncode == 0, f"normal Vec program failed to build: {res_norm_build.stderr}")
        res_norm_run = run([str(norm_prog_bin)])
        require(res_norm_run.returncode == 0, f"normal Vec program failed to run: {res_norm_run.stderr}")
        print("  [Isolation Proof] Production runtime contains 0 test observer symbols and links normal Vec cleanly.")

        # 2. Build isolated test copy of library and observed runtime
        library = work / "lib"
        shutil.copytree(ROOT / "lib", library, ignore=shutil.ignore_patterns("*.o", "*.tki", "__pycache__"))

        # Instrument Vec.drop in the test copy only
        vec_source = (ROOT / "lib/std/vec.tk").read_text(encoding="utf-8")
        drop_target = "unsafe free [self.len]self.*#buf"
        require(vec_source.count(drop_target) == 1, "Vec.drop target not found or changed in lib/std/vec.tk")
        instrumented_vec = (
            "extern fn toka_rt_test_notify_buffer_free(*ptr: void) -> void\n" +
            vec_source.replace(
                drop_target,
                "toka_rt_test_notify_buffer_free(*self.*#buf as *void)\n                " + drop_target
            )
        )
        (library / "std/vec.tk").write_text(instrumented_vec, encoding="utf-8")

        observed_rt = work / "toka_rt_observed.o"
        res_obs_compile = run([cc, "-DTOKA_BUFFER_TEST_OBSERVATION=1", "-c", str(ROOT / "lib/sys/toka_rt.c"), "-o", str(observed_rt)])
        require(res_obs_compile.returncode == 0, f"failed to compile observed runtime: {res_obs_compile.stderr}")

        nm_obs_res = run(["nm", "-g", str(observed_rt)])
        require("toka_rt_test_track_buffer" in nm_obs_res.stdout, "observed runtime missing tracking symbol")

        # 3. Compile and execute four-path lifecycle verification
        observed_prog_bin = work / "lifecycle_observed"
        obs_env = dict(os.environ, TOKA_LIB=str(library))
        res_obs_build = run([compiler, str(FIXTURE), str(observed_rt), "-o", str(observed_prog_bin)], env=obs_env, cwd=work)
        require(res_obs_build.returncode == 0, f"lifecycle test build failed:\n{res_obs_build.stderr}")

        res_obs_run = run([str(observed_prog_bin)], env=obs_env, cwd=work)
        require(res_obs_run.returncode == 0, f"lifecycle test execution failed (code={res_obs_run.returncode}):\n{res_obs_run.stderr}\n{res_obs_run.stdout}")
        require("All 4 lifecycle branches passed successfully!" in res_obs_run.stdout, "unexpected output from lifecycle test")

        print("  [Lifecycle Proof] All 4 branches passed with exact buffer tracking (zero leaks, exact-once drops).")

    print("test_g1_lifecycle: PASS (isolated observation, production cleanliness verified)")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Test G1 static async trait lifecycle with isolated observation")
    parser.add_argument("--build-dir", default=str(ROOT / "../builds/fix-r3-task-escape"),
                        help="Path to CMake build directory containing tokac")
    args = parser.parse_args()
    qualify(args.build_dir)
