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
    import fcntl
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
                                 str(root / "tests/runtime/thread_handoff_v1_equivalent_ops.c"),
                                 "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=45)
        # A fatal path may not wait for a stderr reader or depend on SIGPIPE.
        for full in (True, False):
            read_fd, write_fd = os.pipe()
            child = None
            try:
                if full:
                    flags = fcntl.fcntl(write_fd, fcntl.F_GETFL)
                    fcntl.fcntl(write_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
                    try:
                        while True:
                            os.write(write_fd, b'x' * 4096)
                    except BlockingIOError:
                        pass
                    fcntl.fcntl(write_fd, fcntl.F_SETFL, flags)
                else:
                    os.close(read_fd)
                    read_fd = None
                child = subprocess.Popen([str(executable), "--fatal-probe"],
                                         stdout=subprocess.DEVNULL, stderr=write_fd)
                if child.wait(timeout=5) != 134:
                    raise RuntimeError("fatal did not exit deterministically with unavailable stderr")
            finally:
                if child is not None and child.poll() is None:
                    child.kill()
                    child.wait()
                if read_fd is not None:
                    os.close(read_fd)
                os.close(write_fd)
        print("fatal full/closed stderr: 2 bounded-exit checks passed")
    print("Production build and isolated runtime gate passed; temporary artifacts removed.")


if __name__ == "__main__":
    main()
