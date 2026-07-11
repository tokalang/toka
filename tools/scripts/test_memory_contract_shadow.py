#!/usr/bin/env python3
import filecmp
import json
import os
import shutil
import subprocess
import sys
import tempfile


TOKAC = os.path.abspath(os.environ.get("TOKAC", "./build/bin/tokac"))
SOURCE = os.path.abspath(
    "tests/semantics/memory_summary/source_summary.tk")


def run(command):
    result = subprocess.run(command, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise AssertionError(result.stderr)
    return result


def compile_shadow(source, output, extra=None):
    command = [TOKAC, "--dump-memory-contracts=json", "-c", source,
               "-o", output]
    if extra:
        command[1:1] = extra
    result = run(command)
    return result.stdout, json.loads(result.stdout)


def function_records(document, suffix):
    records = [record for record in document["records"]
               if record["function"] == suffix or
               record["function"].endswith("_" + suffix)]
    if not records:
        raise AssertionError("missing shadow records for " + suffix)
    return records


def record(document, function, contract):
    matches = [entry for entry in function_records(document, function)
               if entry["contract"] == contract]
    if len(matches) != 1:
        raise AssertionError("expected one %s record for %s" %
                             (contract, function))
    return matches[0]


def expect(document, function, contract, decision, reason):
    actual = record(document, function, contract)
    if actual["decision"] != decision or actual["reason"] != reason:
        raise AssertionError(
            "%s/%s: expected %s/%s, got %s/%s" %
            (function, contract, decision, reason,
             actual["decision"], actual["reason"]))


def main():
    with tempfile.TemporaryDirectory(
            prefix="toka_memory_contract_shadow_") as work:
        first_text, first = compile_shadow(
            SOURCE, os.path.join(work, "first.o"))
        second_text, _ = compile_shadow(
            SOURCE, os.path.join(work, "second.o"))
        if first_text != second_text:
            raise AssertionError("shadow output is not deterministic")
        if first.get("schema") != "toka.memory-contract-shadow" or \
                first.get("version") != 1:
            raise AssertionError("unexpected shadow schema")

        expect(first, "ms_read", "nocapture", "Candidate",
               "ProvenBySummary")
        expect(first, "ms_read", "readonly", "Candidate",
               "ProvenBySummary")
        expect(first, "ms_read", "writeonly", "Reject", "ReadsMemory")
        expect(first, "ms_read", "noalias", "Reject",
               "SeparateNoAliasGate")

        expect(first, "ms_write", "nocapture", "Candidate",
               "ProvenBySummary")
        expect(first, "ms_write", "readonly", "Reject", "WritesMemory")
        expect(first, "ms_forward", "readonly", "Reject", "WritesMemory")
        expect(first, "ms_consume", "nocapture", "Reject",
               "TransfersOwnership")
        expect(first, "ms_raw", "readonly", "Reject", "UnsafeBoundary")
        expect(first, "ms_async", "nocapture", "Reject", "SuspendBoundary")
        expect(first, "ms_touch_global", "readonly", "Reject",
               "NonPointerABI")

        generic = [entry for entry in first["records"]
                   if "ms_generic" in entry["function"] and
                   entry["contract"] == "readonly"]
        if len(generic) != 1 or generic[0]["decision"] != "Candidate":
            raise AssertionError("generic instance lacks readonly candidate")

        _, degraded = compile_shadow(
            SOURCE, os.path.join(work, "degraded.o"),
            extra=["--disable-borrow-check"])
        expect(degraded, "ms_read", "readonly", "Reject",
               "BorrowCheckDisabled")
        expect(degraded, "ms_read", "noalias", "Reject",
               "SeparateNoAliasGate")

        _, shared_symbol = compile_shadow(
            os.path.abspath("tests/pass/g03_path.tk"),
            os.path.join(work, "shared-symbol.o"))
        keys = [(entry["function"], entry["parameter_index"],
                 entry["contract"]) for entry in shared_symbol["records"]]
        if len(keys) != len(set(keys)):
            raise AssertionError("shared LLVM symbols produced duplicate records")
        panic_records = [entry for entry in shared_symbol["records"]
                         if entry["function"] == "__toka_panic_handler"]
        if len(panic_records) != 4:
            raise AssertionError("shared panic declarations were not merged")

        plain_object = os.path.join(work, "plain.o")
        run([TOKAC, "-c", SOURCE, "-o", plain_object])
        shadow_object = os.path.join(work, "shadow.o")
        compile_shadow(SOURCE, shadow_object)
        if not filecmp.cmp(plain_object, shadow_object, shallow=False):
            raise AssertionError("shadow dump changed generated object code")

        for level in ("-O0", "-O2", "-O3"):
            executable = os.path.join(work, level[1:].lower())
            run([TOKAC, level, SOURCE, "-o", executable])
            run([executable])

        conflict = subprocess.run(
            [TOKAC, "--dump-memory-contracts=json",
             "--dump-memory-summaries=json", "-c", SOURCE, "-o",
             os.path.join(work, "conflict.o")], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True)
        if conflict.returncode == 0 or \
                "cannot be combined" not in conflict.stderr:
            raise AssertionError("JSON output mode conflict was not rejected")

        case = os.path.abspath(
            "tests/semantics/tki_replay/cases/own_cede_001_signature")
        replay = os.path.join(work, "replay")
        shutil.copytree(case, replay)
        run([TOKAC, "-c", os.path.join(replay, "lib.tk"), "-o",
             os.path.join(replay, "lib.o")])
        os.rename(os.path.join(replay, "lib.tk"),
                  os.path.join(replay, "lib.tk.source-hidden"))
        consumer = next(name for name in os.listdir(replay)
                        if name.startswith("pass_") and name.endswith(".tk"))
        _, interface = compile_shadow(
            os.path.join(replay, consumer),
            os.path.join(replay, "consumer.o"))
        expect(interface, "consume_payload", "nocapture", "Reject",
               "SignatureOnly")
        expect(interface, "consume_payload", "readonly", "Reject",
               "SignatureOnly")
        expect(interface, "consume_payload", "noalias", "Reject",
               "SeparateNoAliasGate")

    print("Memory contract shadow tests PASSED")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, ValueError, StopIteration) as error:
        print("Memory contract shadow tests FAILED: %s" % error,
              file=sys.stderr)
        sys.exit(1)
