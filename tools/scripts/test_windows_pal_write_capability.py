#!/usr/bin/env python3
"""Windows reactor output accepts writable storage and rejects readonly storage."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    compiler = root / args.build_dir / "bin" / ("tokac.exe" if os.name == "nt" else "tokac")
    env = dict(os.environ, TOKA_LIB=str(root / "lib"))
    with tempfile.TemporaryDirectory(prefix="toka-windows-pal-write-") as temp:
        work = Path(temp)
        ir = work / "cold-task.ll"
        positive = subprocess.run(
            [str(compiler), "--target", "x86_64-w64-windows-gnu", "--emit-llvm",
             "tests/pass/g09_async_cold_task_semantics.tk", "-o", str(ir)],
            cwd=root, env=env, capture_output=True, text=True, timeout=60)
        assert positive.returncode == 0 and ir.is_file(), positive.stdout + positive.stderr
        readonly = work / "readonly.tk"
        readonly.write_text("""import core/types::Addr
import sys/os/net
fn main() -> i32 {
    auto events = [0:Addr; 64]
    unsafe {
        auto *pointer = &events[0] as *Addr
        return net::reactor_wait_impl(0, 0, *pointer, 64)
    }
}
""")
        output = work / "readonly.o"
        negative = subprocess.run(
            [str(compiler), "--target", "x86_64-w64-windows-gnu", "-c",
             str(readonly), "-o", str(output)], cwd=root, env=env,
            capture_output=True, text=True, timeout=60)
        assert negative.returncode == 1 and "E04571" in negative.stderr, negative.stderr
        assert not output.exists(), "readonly reactor output emitted an object"
    print("Windows writable reactor IR and readonly no-object control PASSED")


if __name__ == "__main__":
    main()
