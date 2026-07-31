#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile


TOKAC = os.path.abspath(os.environ.get("TOKAC", "./build/bin/tokac"))
SOURCE = os.path.abspath("tests/semantics/memory_summary/source_summary.tk")
FLAG = "--experimental-memory-contracts=readonly"
AUDIT = os.path.abspath("tools/scripts/audit_cross_module_readonly.py")


def run(command):
    result = subprocess.run(command, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise AssertionError(result.stderr)
    return result


def signature(ir, function):
    for line in ir.splitlines():
        if line.startswith("define ") and ("@" + function + "(") in line:
            return line
    raise AssertionError("missing IR definition for " + function)


def record(document, function, parameter="data"):
    matches = [entry for entry in document["records"]
               if entry["function"] == function and
               entry["parameter"] == parameter and
               entry["contract"] == "readonly"]
    if len(matches) != 1:
        raise AssertionError("missing readonly record for " + function)
    return matches[0]


def main():
    audit = json.loads(run([sys.executable, AUDIT]).stdout)
    if audit["schema"] != "toka.cross-module-readonly-audit" or \
            audit["decision"] not in ("BenchmarkRequired", "NoStaticDelta"):
        raise AssertionError("unexpected cross-module readonly audit result")
    with tempfile.TemporaryDirectory(prefix="toka_readonly_") as work:
        default_ir_path = os.path.join(work, "default.ll")
        experimental_ir_path = os.path.join(work, "experimental.ll")
        disabled_ir_path = os.path.join(work, "disabled.ll")
        run([TOKAC, "-O0", "--emit-llvm", SOURCE, "-o", default_ir_path])
        run([TOKAC, "-O0", FLAG, "--emit-llvm", SOURCE,
             "-o", experimental_ir_path])
        run([TOKAC, "-O0", FLAG, "--disable-borrow-check", "--emit-llvm",
             SOURCE, "-o", disabled_ir_path])
        with open(default_ir_path, encoding="utf-8") as stream:
            default_ir = stream.read()
        with open(experimental_ir_path, encoding="utf-8") as stream:
            experimental_ir = stream.read()
        with open(disabled_ir_path, encoding="utf-8") as stream:
            disabled_ir = stream.read()

        candidates = ("ms_read", "ms_escape", "ms_is_null")
        rejected = ("ms_write", "ms_forward", "ms_address", "ms_raw",
                    "ms_async", "ms_consume")
        for function in candidates + rejected:
            if " readonly " in signature(default_ir, function):
                raise AssertionError("default emitted readonly for " + function)
        for function in candidates:
            if " readonly " not in signature(experimental_ir, function):
                raise AssertionError("candidate was not emitted for " + function)
        for function in rejected:
            if " readonly " in signature(experimental_ir, function):
                raise AssertionError("rejected boundary emitted readonly for " +
                                     function)
        if " nocapture " in experimental_ir or " writeonly " in experimental_ir:
            raise AssertionError("readonly mode emitted another contract")
        if " readonly " in disabled_ir:
            raise AssertionError("disabled PAL emitted readonly")

        dump = run([TOKAC, FLAG, "--dump-memory-contracts=json", "-c",
                    SOURCE, "-o", os.path.join(work, "dump.o")])
        document = json.loads(dump.stdout)
        if document.get("version") != 3:
            raise AssertionError("unexpected contract evidence schema")
        for function in candidates:
            value = record(document, function)
            if value["decision"] != "Candidate" or not value["emitted"]:
                raise AssertionError("missing readonly evidence for " + function)
        for function in rejected:
            if record(document, function)["emitted"]:
                raise AssertionError("rejected record claims readonly emission")

        for level in ("-O0", "-O2", "-O3"):
            default_exe = os.path.join(work, "default-" + level[1:])
            experimental_exe = os.path.join(work, "experimental-" + level[1:])
            run([TOKAC, level, SOURCE, "-o", default_exe])
            run([TOKAC, level, FLAG, SOURCE, "-o", experimental_exe])
            default_result = run([default_exe])
            experimental_result = run([experimental_exe])
            if (default_result.stdout, default_result.stderr) != \
                    (experimental_result.stdout, experimental_result.stderr):
                raise AssertionError(level + " behavior differs with readonly")

    print("Experimental readonly tests PASSED")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, ValueError) as error:
        print("Experimental readonly tests FAILED: %s" % error,
              file=sys.stderr)
        sys.exit(1)
