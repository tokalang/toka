#!/usr/bin/env python3
"""Qualify the private, per-instance ReadDataFile native lease handoff."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/datafile_lease"
ORIGINAL = ROOT / "tests/conformance/io/datafile_concurrency_test.tk"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(command, env, timeout=45, cwd=ROOT):
    return subprocess.run(
        [str(item) for item in command], cwd=cwd, env=env,
        capture_output=True, text=True, timeout=timeout,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    build = Path(args.build_dir).resolve()
    compiler = build / "bin/tokac"
    require(compiler.is_file(), "compiler not built")
    env = dict(os.environ, TOKA_LIB=os.pathsep.join(
        (str(ROOT / "lib"), str(build / "lib"))))
    cc = os.environ.get("CC", "clang")

    with tempfile.TemporaryDirectory(prefix="toka-datafile-lease-") as directory:
        work = Path(directory)
        runtime = work / "toka_rt_observed.o"
        handoff = work / "handoff.o"
        hooks = work / "hooks.o"
        for source, output, flags in (
            (ROOT / "lib/sys/toka_rt.c", runtime,
             ["-DTOKA_RT_EXTERNAL_THREAD_HANDOFF=1",
              "-DTOKA_DATAFILE_TEST_OBSERVATION=1"]),
            (ROOT / "lib/sys/toka_thread_handoff_v1.c", handoff,
             ["-DTOKA_THREAD_HANDOFF_TESTING"]),
            (ROOT / "tests/runtime/public_thread_hooks.c", hooks, []),
        ):
            subprocess.run(
                [cc, "-std=c11", "-pthread", "-I", str(ROOT / "tests/runtime"),
                 *flags, "-c", str(source), "-o", str(output)], check=True,
                capture_output=True, text=True, timeout=45,
            )
        production_runtime = work / "toka_rt_production.o"
        subprocess.run(
            [cc, "-std=c11", "-pthread", "-c", str(ROOT / "lib/sys/toka_rt.c"),
             "-o", str(production_runtime)], check=True,
            capture_output=True, text=True, timeout=45,
        )
        symbols = subprocess.run(["nm", "-g", str(production_runtime)],
                                 check=True, capture_output=True, text=True)
        require("toka_rt_test_datafile_" not in symbols.stdout,
                "test observation symbols leaked into the production runtime")

        lifecycle = FIXTURES / "lease_lifecycle.tk"
        normal = run([compiler, lifecycle, "--check-only"], env)
        shadow = run([compiler, lifecycle, "--check-only",
                      "--non-call-transfer-shadow=json"], env)
        require(normal.returncode == shadow.returncode == 0 and
                normal.stderr == shadow.stderr,
                "lease lifecycle normal/shadow parity: " + normal.stderr + shadow.stderr)
        program = work / "lease_lifecycle"
        built = run([compiler, lifecycle, runtime, handoff, hooks, "-o", program], env)
        require(built.returncode == 0 and program.is_file(),
                "lease lifecycle build: " + built.stderr)
        for mode in (0, 1, 2):
            executed = run([program], dict(env, TOKA_THREAD_TEST_MODE=str(mode)), 15)
            require(executed.returncode == 0,
                    f"lease lifecycle mode {mode}: rc={executed.returncode}\n"
                    + executed.stderr + executed.stdout)

        move_source = FIXTURES / "move_only.tk"
        normal = run([compiler, move_source, "--check-only"], env)
        shadow = run([compiler, move_source, "--check-only",
                      "--non-call-transfer-shadow=json"], env)
        require(normal.returncode == shadow.returncode == 0 and
                normal.stderr == shadow.stderr,
                "move-only lease parity: " + normal.stderr + shadow.stderr)
        move_program = work / "move_only"
        built = run([compiler, move_source, runtime, handoff, hooks,
                     "-o", move_program], env)
        require(built.returncode == 0 and move_program.is_file(),
                "move-only lease build: " + built.stderr)
        executed = run([move_program], env, 10)
        require(executed.returncode == 0,
                "cede move retained or duplicated native lease: " + executed.stderr)

        # The original workload is never weakened: four cloned handles, four
        # workers, fifty independent-offset reads per worker, all byte asserts.
        original = work / "original_concurrent_reads"
        built = run([compiler, ORIGINAL, "-o", original], env)
        require(built.returncode == 0 and original.is_file(), built.stderr)
        executed = run([original], env, 20)
        require(executed.returncode == 0 and "PASSED" in executed.stdout,
                "original concurrency workload: " + executed.stderr)

        rejected = {
            "reject_forged_handle.tk": "E0418",
            "reject_same_name.tk": "E0406",
            "reject_wrapped_result.tk": "E0406",
            "reject_rebound_unknown.tk": "E0406",
            "reject_borrowed_alias.tk": "E0406",
            "reject_branch_unknown.tk": "E0406",
            "reject_duplicate_consume.tk": "E0438",
            "reject_clone_after_move.tk": "E0438",
            "reject_bare_copy.tk": "E04652",
            "reject_call_rollback.tk": "E04571",
        }
        for source, code in rejected.items():
            fixture = FIXTURES / source
            normal = run([compiler, fixture, "--check-only"], env)
            shadow = run([compiler, fixture, "--check-only",
                          "--non-call-transfer-shadow=json"], env)
            require(normal.returncode == shadow.returncode == 1 and
                    normal.stderr == shadow.stderr and
                    f"error[{code}]" in normal.stderr,
                    source + ": wrong rejection/parity\n" + normal.stderr)
            if source == "reject_call_rollback.tk":
                require("E0438" not in normal.stderr and "E0410" not in normal.stderr,
                        "rejected call leaked source invalidation")
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (source + suffix)
                failed = run([compiler, fixture, flag, "-o", output], env)
                require(failed.returncode == 1 and f"error[{code}]" in failed.stderr and
                        not output.exists(), source + ": rejected edge emitted artifact")

        for fault in ("datafile-lease", "type", "datafile-capture", "thread-list"):
            for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / ("fault-" + fault + suffix)
                failed = run([compiler, ORIGINAL,
                              "--native-sync-witness-fault=" + fault,
                              flag, "-o", output], env)
                require(failed.returncode == 1 and "E0701" in failed.stderr and
                        not output.exists(), fault + ": fault emitted artifact\n" + failed.stderr)

        # An otherwise trusted module with the retain operation removed is
        # not the reviewed operation schema. Never run this unsafe mutation.
        altered = work / "lib/std/data_file.tk"
        altered.parent.mkdir(parents=True)
        text = (ROOT / "lib/std/data_file.tk").read_text(encoding="utf-8")
        old = "unsafe { toka_datafile_read_retain(self.handle) }"
        require(text.count(old) == 1, "reviewed clone operation changed")
        altered.write_text(
            text.replace(old, "unsafe { toka_datafile_read_retain(0:Addr) }"),
            encoding="utf-8")
        altered_env = dict(env, TOKA_LIB=os.pathsep.join(
            (str(work / "lib"), str(ROOT / "lib"), str(build / "lib"))))
        consumer = work / "consumer.tk"
        shutil.copy2(ORIGINAL, consumer)
        dependencies = run([compiler, "--dump-dependencies=json", consumer],
                           altered_env, cwd=work)
        require(dependencies.returncode == 0, dependencies.stderr)
        selected = [Path(path).resolve() for path, info in
                    json.loads(dependencies.stdout)["modules"].items()
                    if info.get("shadow_coordinate", {}).get("logical_module_path")
                    == "std/data_file"]
        require(selected == [altered.resolve()],
                "tampered provider was not selected: " + str(selected))
        output = work / "altered.o"
        failed = run([compiler, consumer, "-c", "-o", output],
                     altered_env, cwd=work)
        require(failed.returncode == 1 and not output.exists(),
                "modified native retain body inherited lease authority: "
                f"rc={failed.returncode} artifact={output.exists()}\n"
                + failed.stderr)

        # A source-hidden std/data_file interface cannot recreate this local
        # source contract from its public type or @Send annotation alone.
        hidden = work / "hidden"
        hidden.mkdir()
        provider_object = hidden / "data_file.o"
        emitted = run([compiler, "--emit-interface", "-c",
                       ROOT / "lib/std/data_file.tk", "-o", provider_object], env)
        require(emitted.returncode == 0, "provider interface: " + emitted.stderr)
        provider_interface = provider_object.with_suffix(".tki")
        require(provider_interface.is_file(), "provider interface missing")
        hidden_lib = hidden / "lib"
        shutil.copytree(ROOT / "lib", hidden_lib)
        shutil.copy2(provider_interface, hidden_lib / "std/data_file.tki")
        shutil.copy2(provider_object, hidden_lib / "std/data_file.o")
        (hidden_lib / "std/data_file.tk").rename(hidden / "source_data_file.tk")
        hidden_consumer = hidden / "consumer.tk"
        shutil.copy2(ORIGINAL, hidden_consumer)
        hidden_env = dict(env, TOKA_LIB=str(hidden_lib), TOKA_USE_LIB_CACHE="1")
        dependencies = run([compiler, "--dump-dependencies=json", hidden_consumer],
                           hidden_env, cwd=hidden)
        require(dependencies.returncode == 0, dependencies.stderr)
        hidden_selected = [(Path(path).resolve(), info["kind"]) for path, info in
                           json.loads(dependencies.stdout)["modules"].items()
                           if info.get("shadow_coordinate", {}).get("logical_module_path")
                           == "std/data_file"]
        require(hidden_selected == [((hidden_lib / "std/data_file.tki").resolve(),
                                     "interface")],
                "source-hidden provider was not selected: " + str(hidden_selected))
        for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = hidden / ("consumer" + suffix)
            failed = run([compiler, hidden_consumer, flag, "-o", output],
                         hidden_env, cwd=hidden)
            require(failed.returncode == 1 and "E04661" in failed.stderr and
                    not output.exists(),
                    "source-hidden lease regained authority: " + failed.stderr)

    print("ReadDataFile lease: original 4x50 runtime; three exact-count/cleanup modes; "
          "direct cede move zero-retain; ten parity rejections; "
          "twenty negative and eight fault no-artifact checks; "
          "altered body and source-hidden provider fail-closed")


if __name__ == "__main__":
    main()
