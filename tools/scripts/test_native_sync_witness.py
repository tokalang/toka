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
                "--workspace-root", str(ROOT if source.is_relative_to(ROOT) else work),
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
        rw = ROOT / "tests/semantics/std_thread_handoff/sync_thread_rw.tk"
        cond = ROOT / "tests/pass/g09_sync_condvar.tk"
        for source, extra in ((rw, ("read-acquire", "read-drop", "read-access")),
                              (cond, ("notify", "wait-guard"))):
            for fault in extra:
                for mode, extension in (("-c", ".o"), ("--emit-llvm", ".ll")):
                    output = work / (fault + extension)
                    result = compile_case(source, output, mode, "--native-sync-witness-fault=" + fault)
                    assert result.returncode != 0 and "E0701" in result.stderr and not output.exists(), result.stderr
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
        bodies["wait-live-slot"] = """auto ~mutex = Mutex<i32>::make_shared(7)
auto ~condition = CondVar<i32>::make_shared()
auto lock = mutex.lock().unwrap()
auto &slot = lock.borrow_mut()
condition.wait_cond(lock)
return slot
"""
        bodies["read-slot-write"] = """auto ~mutex = RwMutex<i32>::make_shared(7)
auto held = mutex.read_lock().unwrap()
auto &slot = held.borrow()
slot = 9
return 0
"""
        denials = 0
        for name, body in bodies.items():
            source = work / (name + ".tk")
            source.write_text(before + "import std/sync::{CondVar, RwMutex}\n"
                              "fn touch(owner: Mutex<i32>) { auto raw = owner.get_handle() }\n"
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
                if name == "wait-live-slot": expected = ("LiveUnborrowedMutexGuardRequired",)
                if name == "read-slot-write": expected = ("E0424", "E04573", "E0433", "E0423")
                assert any(reason in result.stderr for reason in expected), (name, result.stderr)
                if normal: assert (normal.returncode, normal.stderr) == (result.returncode, result.stderr), name
                else: normal = result
            denials += 1
        # TKI type identity is not a serialized initialized-storage witness.
        provider = work / "native_provider.tk"
        provider.write_text("import std/sync::Mutex\npub fn make_owner() -> ~Mutex<i32> {\n"
                            "return Mutex<i32>::make_shared(7)\n}\n")
        provider_object = work / "native_provider.o"
        built = subprocess.run([str(compiler), "--workspace-node", "native-witness-test",
            "--workspace-root", str(work), "--emit-interface", "-c", str(provider), "-o", str(provider_object)],
            cwd=work, env=env, capture_output=True, text=True, timeout=60)
        assert built.returncode == 0, built.stderr
        provider.rename(work / "native_provider.hidden")
        consumer = work / "native_consumer.tk"
        consumer.write_text(before + "import native_provider::make_owner\nfn main() -> i32 {\n"
            "auto ~mutex = make_owner()\nauto ~worker_mutex = ~mutex\n" + capture + spawn + "}\n")
        for flag, extension in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = work / ("hidden-consumer" + extension)
            rejected = compile_case(consumer, output, "-I", str(work), flag)
            assert rejected.returncode != 0 and "E04661" in rejected.stderr, rejected.stderr
            assert not output.exists(), output
        print(f"native witness: {faults} no-artifact faults; {denials} source denials with strict parity; "
              "source-hidden witness rejection")


if __name__ == "__main__":
    main()
