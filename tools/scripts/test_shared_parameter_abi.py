#!/usr/bin/env python3
"""Required shared-parameter runtime matrix; failures are not xfailed/skipped."""
import argparse
import os
from pathlib import Path
import re
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


def verify_carrier_ir(text):
    bodies = {}
    for match in re.finditer(r"^define linkonce_odr[^\n]*@__toka_gfn_([0-9a-f]+)[^\n]*\n.*?^}",
                             text, re.M | re.S):
        identity = bytes.fromhex(match[1]).decode()
        for name in ("generic_read", "generic_take", "generic_echo", "generic_take_async"):
            if f";{len(name)}:{name};" in identity:
                bodies[name] = match[0]
    assert len(bodies) == 4, "missing exact generic IR bodies"
    argument = '%"\'value"'
    for name in ("generic_read", "generic_take", "generic_echo"):
        body = bodies[name]
        assert f"(ptr {argument})" in body.splitlines()[0], name + " ABI changed"
        assert '%"\'value.addr" = alloca ptr' not in body, name + " has carrier wrapper"
    assert f"ptr {argument}, i32 0, i32 0" in bodies["generic_read"], "read misses real carrier"
    for name in ("generic_take", "generic_echo"):
        assert f"load {{ ptr, ptr }}, ptr {argument}" in bodies[name], name + " move misses carrier"
        assert f"store {{ ptr, ptr }} zeroinitializer, ptr {argument}" in bodies[name], name + " source not retired"
    assert bodies["generic_take"].count("atomicrmw sub") == 1, "discard must release exactly one shared reference"
    assert "ptr %unused.result.tmp)" not in bodies["generic_take"], "payload Drop called on a carrier"
    asynchronous = bodies["generic_take_async"]
    assert f"(ptr {argument})" in asynchronous.splitlines()[0], "async ABI changed"
    assert '%"\'value.addr" = alloca ptr' not in asynchronous, "async carrier wrapped again"
    loaded = re.search(r'(%[0-9A-Za-z_.]+) = load \{ ptr, ptr \}, ptr ' + re.escape(argument), asynchronous)
    assert loaded and f"store {{ ptr, ptr }} {loaded[1]}, ptr" in asynchronous, "async frame lacks complete carrier copy"


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

        for name in ("shared_parameter_matrix.tk", "sync_managed_storage_pending.tk", "sync_shared_guard.tk",
                     "g08_morphology_constraint_domains.tk"):
            source = ROOT / "tests/pass" / name if name.startswith("g08_") else FIXTURES / name
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
            if name == "g08_morphology_constraint_domains.tk":
                ir = work / "morphic-borrow.ll"
                emitted = compile_source(source, "--emit-llvm", "-o", ir)
                assert emitted.returncode == 0, emitted.stderr
                bodies = [match[0] for match in re.finditer(
                    r"^define linkonce_odr[^\n]*@__toka_gfn_([0-9a-f]+)_M_S[^\n]*\n.*?^}",
                    ir.read_text(), re.M | re.S)
                    if ";15:borrow_identity;" in bytes.fromhex(match[1]).decode()]
                assert len(bodies) == 1 and 'ret ptr %"\'value"' in bodies[0], "morphic shared borrow must return the caller carrier"
                assert "shared.data_ptr" not in bodies[0], "morphic borrow incorrectly selected payload"
            if name == "shared_parameter_matrix.tk":
                ir = work / "shared-parameter.ll"
                emitted = compile_source(source, "--emit-llvm", "-o", ir)
                try:
                    assert emitted.returncode == 0, emitted.stderr
                    verify_carrier_ir(ir.read_text())
                    print("PASS IR: sync real carrier / async full frame / exact shared discard", flush=True)
                except AssertionError as error:
                    failures.append("IR carrier qualification: " + str(error))
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
