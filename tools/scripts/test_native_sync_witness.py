#!/usr/bin/env python3
"""Native owner witness denial and exact-source lifecycle gates."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
POSITIVE = ROOT / "tests/semantics/std_thread_handoff/sync_thread_pending.tk"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    with tempfile.TemporaryDirectory(prefix="toka-native-witness-") as directory:
        work = Path(directory)

        def compile_case(source, output, *flags):
            return subprocess.run([str(compiler), "--workspace-node", "native-witness-test",
                "--workspace-root", str(ROOT if source == POSITIVE else work),
                str(source), *flags, "-o", str(output)], env=env,
                capture_output=True, text=True, timeout=60)

        faults = 0
        for fault in ("missing", "origin", "factory", "allocation", "type", "element", "drop",
                      "acquire", "guard-drop", "guard-access", "thread-list", "access-site",
                      "slot-missing", "slot-type", "slot-destination"):
            for mode, extension in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (fault + extension)
                result = compile_case(POSITIVE, output, mode, "--native-sync-witness-fault=" + fault)
                assert result.returncode != 0 and "E0701" in result.stderr, (fault, result.stderr)
                assert not output.exists(), output
                faults += 1

        before = """import std/sync::Mutex
import std/thread::thread_spawn
"""
        creation = "auto ~mutex = Mutex<i32>::make_shared(7)\nauto ~worker_mutex = ~mutex\n"
        capture = """auto callback = { [cede ~worker_mutex] =>
auto locked = worker_mutex.lock().unwrap()
auto &value# = locked.borrow_mut()
value = 9
return 0
}:dyn fn() -> i32
"""
        spawn = "auto created = thread_spawn<i32>(cede callback)\nreturn 0\n"
        bodies = {
            "forged": "auto ~mutex = new Mutex<i32>(handle = 0:Addr#, data_ptr = 0:Addr#)\n"
                      "auto ~worker_mutex = ~mutex\n" + capture + spawn,
            "handle-escape": creation + "auto raw = mutex.get_handle()\n" + capture + spawn,
            "field-escape": creation + "auto raw = mutex.data_ptr\n" + capture + spawn,
            "stale-capture": creation + capture + "auto raw = mutex.get_handle()\n" + spawn,
            "unknown-call": creation + "touch(mutex)\n" + capture + spawn,
            "borrowed-payload": "auto ~mutex = Mutex<str>::make_shared(\"static\")\n"
                "auto ~worker_mutex = ~mutex\n" + capture.replace("value = 9", "auto length = value.len()") + spawn,
        }
        denials = 0
        for name, body in bodies.items():
            source = work / (name + ".tk")
            source.write_text(before + "fn touch(owner: Mutex<i32>) { auto raw = owner.get_handle() }\n"
                              "fn main() -> i32 {\n" + body + "}\n")
            normal = None
            for suffix, flags in (("normal", []), ("shadow", ["--non-call-transfer-shadow=json"])):
                output = work / (name + suffix + ".o")
                result = compile_case(source, output, "-c", *flags)
                assert result.returncode != 0 and not output.exists(), (name, result.stderr)
                # Borrowed T is currently stopped by the existing explicit
                # return-dependency contract before witness qualification;
                # do not report it as a witness-specific diagnostic.
                expected = ("E0418",) if name in ("forged", "field-escape") else (
                    ("E0454",) if name == "borrowed-payload" else ("E04661", "EnvironmentLifetimeUnproven"))
                assert any(reason in result.stderr for reason in expected), (name, result.stderr)
                if normal: assert (normal.returncode, normal.stderr) == (result.returncode, result.stderr), name
                else: normal = result
            denials += 1
        print(f"native witness: {faults} no-artifact faults; {denials} source denials with strict parity")


if __name__ == "__main__":
    main()
