#!/usr/bin/env python3
"""Required shared-parameter runtime matrix; failures are not xfailed/skipped."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/std_thread_handoff"
CASES = (
    "sync explicit borrow", "sync morphic borrow", "sync explicit cede",
    "sync morphic cede", "sync morphic return", "async explicit borrow",
    "async morphic borrow", "async explicit cede", "async morphic cede",
    "sync generic hatted borrow", "sync generic hatted cede",
    "async generic hatted borrow", "async generic hatted cede",
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))
    passed, failures = [], []
    with tempfile.TemporaryDirectory(prefix="toka-shared-parameter-abi-") as directory:
        work = Path(directory)
        runtime, hook = work / "toka_rt.o", work / "case.o"
        for source, output in ((ROOT / "lib/sys/toka_rt.c", runtime),
                               (ROOT / "tests/runtime/shared_parameter_case.c", hook)):
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-pthread",
                            "-c", str(source), "-o", str(output)], check=True)

        def compile_source(source, *flags, cwd=ROOT):
            return subprocess.run([str(compiler), str(source), *map(str, flags)],
                                  env=env, cwd=cwd, capture_output=True, text=True, timeout=45)

        def execute(binary, label, mode=0):
            try:
                result = subprocess.run([str(binary)], env=dict(env, TOKA_SHARED_CASE=str(mode)),
                                        capture_output=True, text=True, timeout=10)
                if result.returncode == 0:
                    passed.append(label)
                    print("PASS " + label, flush=True)
                else:
                    failures.append(label + f": rc={result.returncode}\n" + result.stderr)
            except subprocess.TimeoutExpired:
                failures.append(label + ": timeout")

        for name in ("shared_parameter_matrix.tk", "sync_managed_storage_pending.tk", "sync_shared_guard.tk"):
            source = FIXTURES / name
            normal = compile_source(source, "--check-only")
            shadow = compile_source(source, "--check-only", "--non-call-transfer-shadow=json")
            if normal.returncode != 0 or shadow.returncode != 0 or normal.stderr != shadow.stderr:
                failures.append(name + ": frontend/parity\n" + normal.stderr + shadow.stderr)
                continue
            binary = work / name
            built = compile_source(source, runtime, hook, "-o", binary)
            if built.returncode != 0:
                failures.append(name + ": build\n" + built.stderr)
                continue
            if name == "shared_parameter_matrix.tk":
                for mode, label in enumerate(CASES):
                    execute(binary, label, mode)
            else:
                execute(binary, name)

        provider = work / "shared_abi_provider.tk"
        shutil.copyfile(FIXTURES / provider.name, provider)
        provider_object = work / "shared_abi_provider.o"
        built = compile_source(provider, "--emit-interface", "-c", "-o", provider_object, cwd=work)
        if built.returncode != 0:
            failures.append("provider build\n" + built.stderr)
        else:
            provider.rename(work / "provider.source-hidden")
            consumer = work / "consumer.tk"
            shutil.copyfile(FIXTURES / "shared_abi_consumer.tk", consumer)
            binary = work / "consumer"
            built = compile_source(consumer, "-I", work, provider_object, runtime, hook,
                                   "-o", binary, cwd=work)
            if built.returncode != 0:
                failures.append("source-hidden build\n" + built.stderr)
            else:
                execute(binary, "source-hidden explicit return", 0)
                execute(binary, "source-hidden morphic return", 1)

        source = ROOT / "tests/semantics/raw_take/reject_unqualified_shared_generic_parameter.tk"
        for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = work / ("raw_take_isolation" + suffix)
            result = compile_source(source, mode, "-o", output)
            label = "raw_take isolation " + suffix
            if result.returncode == 1 and "UnqualifiedSharedGenericParameter" in result.stderr and not output.exists():
                passed.append(label)
                print("PASS " + label, flush=True)
            else:
                failures.append(label + "\n" + result.stderr)
        for name in ("shared_borrow_start_rejected.tk", "shared_morphic_borrow_start_rejected.tk"):
            for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (name + suffix)
                result = compile_source(FIXTURES / name, mode, "-o", output)
                label = name + " " + suffix
                if result.returncode == 1 and "E04583" in result.stderr and not output.exists():
                    passed.append(label)
                    print("PASS " + label, flush=True)
                else:
                    failures.append(label + "\n" + result.stderr)
    for failure in failures:
        print("FAIL " + failure, flush=True)
    print(f"shared parameter ABI: {len(passed)} passed; {len(failures)} failed; no skips")
    raise SystemExit(1 if failures else 0)


if __name__ == "__main__":
    main()
