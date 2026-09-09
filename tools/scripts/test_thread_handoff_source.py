#!/usr/bin/env python3
"""Actual Toka source -> Sema seal -> existing cleanup wrappers -> native runtime.

Private test-driver only. Does not qualify public std/thread, opaque parameters,
all callable-return environments or unimplemented source routes.
"""
import argparse
import os
import re
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/thread_handoff_source"


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    def run(source, *flags, enabled=True):
        return subprocess.run([str(compiler), *(["--thread-handoff-source-probe"] if enabled else []),
                               *map(str, flags), str(FIXTURES / source)], cwd=ROOT, env=env,
                              capture_output=True, text=True, timeout=45)

    with tempfile.TemporaryDirectory(prefix="toka-real-thread-source-") as directory:
        work = Path(directory)
        runtime = work / "thread.o"
        subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-pthread", "-c",
                        str(ROOT / "lib/sys/toka_thread_handoff_v1.c"), "-o", str(runtime)], check=True)
        positives = ("basic.tk", "results.tk", "temporary.tk", "spoof.tk",
                     "invoke_mode_control.tk", "result_drop_nominal.tk")
        for source in positives:
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 0 and normal.stderr == shadow.stderr,
                    source + ": " + normal.stderr + shadow.stderr)
            output = work / source.removesuffix(".tk")
            linked = run(source, *([runtime] if source != "spoof.tk" else []), "-o", output)
            require(linked.returncode == 0 and output.exists(), source + ": " + linked.stderr)
            executed = subprocess.run([str(output)], capture_output=True, text=True, timeout=15)
            require(executed.returncode == 0, source + ": runtime " + str(executed.returncode) + executed.stderr)
        ir = work / "results.ll"
        emitted = run("results.tk", "--emit-llvm", "-o", ir)
        require(emitted.returncode == 0, emitted.stderr)
        bodies = re.findall(r'^define internal void @__toka_thread_source_[^\n]*\.run\([^\n]*\n.*?^}',
                            ir.read_text(), re.M | re.S)
        require(any("sret(" in body for body in bodies) and
                any(re.search(r'call %Token ', body) for body in bodies),
                "real-source adapter must cover both actual sret and small-struct value return")
        for source, reason in {
            "reject_bare.tk": "MissingCedeForNamedSource",
            "reject_result_type.tk": "ResultTypeArgumentMismatch",
            "reject_borrowed_result.tk": "ResultDependenciesUnproven",
            "reject_environment.tk": "EnvironmentLifetimeUnproven",
            "reject_mode.tk": "CallableFormalModeMismatch",
            "reject_arity.tk": "InvalidProbeArity",
            "reject_send.tk": "ActualEnvironmentSendUnproven",
            "reject_opaque.tk": "ActualEnvironmentSendUnproven",
            "reject_nested_rollback.tk": "ResultTypeArgumentMismatch",
            "reject_temporary_cede.tk": "CedeRequiresNamedSource",
            "reject_unit.tk": "UnitResultABIUnqualified",
            "reject_mode_ascription.tk": "ActualInvokeModeMismatch",
            "reject_result_ascription.tk": "ActualInvokeSignatureMismatch",
        }.items():
            normal = run(source, "--check-only")
            shadow = run(source, "--check-only", "--non-call-transfer-shadow=json")
            require(normal.returncode == shadow.returncode == 1 and normal.stderr == shadow.stderr and
                    reason in normal.stderr and "E0438" not in normal.stderr and "E0410" not in normal.stderr,
                    source + ": rejection/rollback " + normal.stderr + shadow.stderr)
            for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (source + suffix)
                rejected = run(source, mode, "-o", output)
                require(rejected.returncode == 1 and not output.exists(), source + ": emitted rejected artifact")
        for fault in ("missing", "source", "result", "cleanup"):
            for mode, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
                output = work / (fault + suffix)
                rejected = run("basic.tk", "--thread-handoff-source-fault=" + fault, mode, "-o", output)
                require(rejected.returncode == 1 and "E0701" in rejected.stderr and not output.exists(),
                        fault + ": invalid plan emitted artifact\n" + rejected.stderr)
        disabled = run("basic.tk", "--check-only", enabled=False)
        require(disabled.returncode == 1 and "requires --thread-handoff-source-probe" in disabled.stderr,
                "private driver leaked into normal source mode")
        unchecked = run("basic.tk", "--check-only", "--disable-borrow-check")
        require(unchecked.returncode == 1 and "FinalCheckedContextRequired" in unchecked.stderr,
                "handoff qualified without the borrow checker")
        output = work / "without-new-runtime"
        old = run("basic.tk", "-o", output)
        require(old.returncode != 0 and not output.exists() and "toka_thread_" in old.stderr,
                "source bridge linked without the new runtime\n" + old.stderr)
    print("thread source: 6 runtime/parity positives, 13 source rejection/rollback cases, 26 negative + 8 fault no-artifact checks, disabled/private, borrow-check and missing-runtime gates; no skips")


if __name__ == "__main__":
    main()
