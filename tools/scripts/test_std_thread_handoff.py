#!/usr/bin/env python3
"""Directed public dynamic/state/Unit responsibility subset, not full thread acceptance.

Captured thin-fn and mutable dynamic provenance targets remain separate open
positive fixtures; this subset must not be reported as their qualification.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/std_thread_handoff"


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    cc = os.environ.get("CC", "clang")
    with tempfile.TemporaryDirectory(prefix="toka-public-thread-") as directory:
        work = Path(directory)
        core = work / "toka_rt_core.o"
        native = work / "handoff.o"
        hooks = work / "hooks.o"
        for source, output, flags in (
            (ROOT / "lib/sys/toka_rt.c", core, ["-DTOKA_RT_EXTERNAL_THREAD_HANDOFF=1"]),
            (ROOT / "lib/sys/toka_thread_handoff_v1.c", native, ["-DTOKA_THREAD_HANDOFF_TESTING"]),
            (ROOT / "tests/runtime/public_thread_hooks.c", hooks, []),
        ):
            subprocess.run([cc, "-std=c11", "-pthread", "-I", str(ROOT / "tests/runtime"),
                            *flags, "-c", str(source), "-o", str(output)], check=True)
        for source in ("dynamic_and_state.tk", "unit_dynamic.tk", "unit.tk", "fresh_mutable.tk",
                       "owned_closure.tk", "owned_state_return.tk", "results.tk", "responsibility.tk",
                       "thread_spawn_copy_sync_capture.tk", "thread_spawn_nested_closure.tk",
                       "thread_spawn_binding_capture.tk", "thread_spawn_send_capture.tk",
                       "thread_spawn_assignment_capture.tk"):
            command = [str(compiler), str(FIXTURES / source)]
            normal = subprocess.run(command + ["--check-only"], cwd=ROOT, env=env,
                                    capture_output=True, text=True, timeout=45)
            shadow = subprocess.run(command + ["--check-only", "--non-call-transfer-shadow=json"],
                                    cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)
            require(normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr,
                    source + ": frontend/parity\n" + normal.stderr + shadow.stderr)
            output = work / source.removesuffix(".tk")
            built = subprocess.run(command + [str(core), str(native), str(hooks), "-o", str(output)],
                                   cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)
            require(built.returncode == 0, source + ": build\n" + built.stderr)
            modes = range(15) if source == "responsibility.tk" else (0,)
            for mode in modes:
                executed = subprocess.run([str(output)], env=dict(env, TOKA_THREAD_TEST_MODE=str(mode)),
                                          capture_output=True, text=True, timeout=10)
                expected = 134 if source == "responsibility.tk" and mode == 8 else 0
                require(executed.returncode == expected,
                        source + f" mode {mode}: rc={executed.returncode}\n" + executed.stderr)
        for source, code in {
            "borrowed_consuming_parameter_probe.tk": "E0473",
            "reject_consuming_copy_bound.tk": "E0606",
            "reject_consuming_copy_capture.tk": "E04581",
            "consuming_thin_ascription_probe.tk": "E04606",
            "reject_consuming_thin_copy.tk": "E04661",
        }.items():
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                artifact = work / (source + suffix)
                failed = subprocess.run([str(compiler), str(FIXTURES / source), flag, "-o", str(artifact)],
                                        cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)
                require(failed.returncode == 1 and code in failed.stderr and not artifact.exists() and
                        "E0438" not in failed.stderr and "E0410" not in failed.stderr,
                        source + ": rejection/rollback\n" + failed.stderr)
        for fault in ("missing", "site", "incomplete", "output", "owner"):
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                artifact = work / ("fault_" + fault + suffix)
                failed = subprocess.run([str(compiler), str(FIXTURES / "dynamic_and_state.tk"),
                                         "--public-thread-fault=" + fault, flag, "-o", str(artifact)],
                                        cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)
                require(failed.returncode == 1 and "E0701" in failed.stderr and not artifact.exists(),
                        fault + ": fault produced artifact\n" + failed.stderr)
        provider = work / "provider.tk"
        provider.write_text('import std/thread::{thread_spawn, JoinHandle, ThreadError}\n'
                            'pub fn start() -> Result<JoinHandle<i32>, ThreadError> {\n'
                            '  auto callback = { => 42 }:dyn fn() -> i32\n'
                            '  return thread_spawn<i32>(cede callback)\n}\n')
        provider_object = work / "provider.o"
        built = subprocess.run([str(compiler), "--emit-interface", "-c", str(provider),
                                "-o", str(provider_object)], env=env, cwd=work,
                               capture_output=True, text=True, timeout=45)
        require(built.returncode == 0, "provider\n" + built.stderr)
        provider.rename(work / "provider.hidden")
        consumer = work / "consumer.tk"
        consumer.write_text('import provider::start\nfn main() -> i32 {\n'
                            ' auto created = start()\n if created.is_err() { return 1 }\n'
                            ' auto handle# = created.unwrap()\n auto result = handle#.join()\n'
                            ' if result.is_err() { return 2 }\n return result.unwrap() - 42\n}\n')
        executable = work / "source-hidden"
        built = subprocess.run([str(compiler), "-I", str(work), str(consumer), str(provider_object),
                                str(core), str(native), str(hooks), "-o", str(executable)],
                               cwd=work, env=env, capture_output=True, text=True, timeout=45)
        require(built.returncode == 0, "source-hidden consumer\n" + built.stderr)
        executed = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
        require(executed.returncode == 0, "source-hidden runtime\n" + executed.stderr)
    print("public thread subset: 13 strict parity fixtures; 27 runtime executions; source-hidden TKI/object run; 10 source rejection/rollback + 10 fault no-artifact checks")


if __name__ == "__main__":
    main()
