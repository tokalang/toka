#!/usr/bin/env python3
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed


TOKAC = os.path.abspath(os.environ.get("TOKAC", "./build/bin/tokac"))
SOURCE = os.path.abspath(
    "tests/semantics/memory_summary/source_summary.tk")
FLAG = "--experimental-memory-contracts=nocapture"
AUDIT = os.path.abspath(
    "tools/scripts/audit_experimental_nocapture.py")


def run(command):
    result = subprocess.run(command, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise AssertionError(result.stderr)
    return result


def signature(ir, function):
    match = re.search(r"^define .*@" + re.escape(function) +
                      r"\([^\n]*$", ir, re.MULTILINE)
    if not match:
        raise AssertionError("missing IR definition for " + function)
    return match.group(0)


def contract_record(document, function, contract):
    matches = [entry for entry in document["records"]
               if (entry["function"] == function or
                   entry["function"].endswith("_" + function)) and
               entry["contract"] == contract]
    if len(matches) != 1:
        raise AssertionError("expected one %s record for %s" %
                             (contract, function))
    return matches[0]


def focused(work):
    audit_command = [sys.executable, AUDIT]
    first_audit = run(audit_command).stdout
    second_audit = run(audit_command).stdout
    if first_audit != second_audit:
        raise AssertionError("focused benefit audit is not deterministic")
    audit = json.loads(first_audit)
    if audit.get("schema") != "toka.nocapture-benefit-audit" or \
            audit.get("version") != 1 or \
            audit.get("decision") != "KeepExperimental" or \
            audit.get("reason") != "NoOptimizedIRDelta":
        raise AssertionError("unexpected focused benefit audit result")

    default_ir_path = os.path.join(work, "default.ll")
    experimental_ir_path = os.path.join(work, "experimental.ll")
    disabled_ir_path = os.path.join(work, "disabled.ll")
    run([TOKAC, "--emit-llvm", SOURCE, "-o", default_ir_path])
    run([TOKAC, FLAG, "--emit-llvm", SOURCE, "-o", experimental_ir_path])
    run([TOKAC, FLAG, "--disable-borrow-check", "--emit-llvm", SOURCE,
         "-o", disabled_ir_path])
    with open(default_ir_path, encoding="utf-8") as stream:
        default_ir = stream.read()
    with open(experimental_ir_path, encoding="utf-8") as stream:
        experimental_ir = stream.read()
    with open(disabled_ir_path, encoding="utf-8") as stream:
        disabled_ir = stream.read()

    candidates = ("ms_read", "ms_write")
    rejected = ("ms_consume", "ms_escape", "ms_forward", "ms_store", "ms_address",
                "ms_is_null", "ms_raw", "ms_async")
    for function in candidates + rejected:
        if "nocapture" in signature(default_ir, function):
            raise AssertionError("default mode emitted nocapture for " + function)
    for function in candidates:
        if "nocapture" not in signature(experimental_ir, function):
            raise AssertionError("candidate was not emitted for " + function)
    for function in rejected:
        if function == "ms_store":
            continue
        if "nocapture" in signature(experimental_ir, function):
            raise AssertionError("rejected boundary was emitted for " + function)
    store_signature = signature(experimental_ir, "ms_store")
    if "ptr %source" not in store_signature or \
            "ptr nocapture %target" not in store_signature:
        raise AssertionError("stored source and destination were not distinguished")
    if "nocapture" not in signature(
            experimental_ir, "ms_generic_M_SummaryData"):
        raise AssertionError("generic candidate was not emitted")
    if any(attribute in experimental_ir for attribute in
           (" noalias %data", " readonly %data", " writeonly %data")):
        raise AssertionError("experimental mode emitted a non-nocapture contract")
    if " nocapture " in disabled_ir:
        raise AssertionError("disabled PAL emitted nocapture")

    dump = run([TOKAC, FLAG, "--dump-memory-contracts=json", "-c", SOURCE,
                "-o", os.path.join(work, "dump.o")])
    document = json.loads(dump.stdout)
    if document.get("version") != 3:
        raise AssertionError("unexpected contract evidence schema")
    for function in candidates:
        record = contract_record(document, function, "nocapture")
        if record["decision"] != "Candidate" or not record["emitted"]:
            raise AssertionError("missing emission evidence for " + function)
    for function in rejected:
        if function == "ms_store":
            continue
        if contract_record(document, function, "nocapture")["emitted"]:
            raise AssertionError("rejected record claims emission for " + function)
    forward = contract_record(document, "ms_forward", "nocapture")
    if forward["reason"] != "IRCaptureDetected":
        raise AssertionError("unannotated transitive call passed IR capture gate")
    store_records = [entry for entry in document["records"]
                     if entry["function"] == "ms_store" and
                     entry["contract"] == "nocapture"]
    store_by_parameter = {entry["parameter"]: entry for entry in store_records}
    if store_by_parameter["source"]["reason"] != "CapturesRoot" or \
            store_by_parameter["source"]["emitted"]:
        raise AssertionError("stored source was not rejected as captured")
    if not store_by_parameter["target"]["emitted"]:
        raise AssertionError("non-escaping store target was not emitted")

    for level in ("-O0", "-O2", "-O3"):
        default_exe = os.path.join(work, "default-" + level[1:].lower())
        experimental_exe = os.path.join(
            work, "experimental-" + level[1:].lower())
        run([TOKAC, level, SOURCE, "-o", default_exe])
        run([TOKAC, FLAG, level, SOURCE, "-o", experimental_exe])
        default_result = run([default_exe])
        experimental_result = run([experimental_exe])
        if (default_result.stdout, default_result.stderr) != \
                (experimental_result.stdout, experimental_result.stderr):
            raise AssertionError(level + " behavior differs with nocapture")

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
    interface_dump = run(
        [TOKAC, FLAG, "--dump-memory-contracts=json", "-c",
         os.path.join(replay, consumer), "-o",
         os.path.join(replay, "consumer.o")])
    interface = json.loads(interface_dump.stdout)
    imported = contract_record(interface, "consume_payload", "nocapture")
    if imported["reason"] != "SignatureOnly" or imported["emitted"]:
        raise AssertionError("TKI-only function emitted nocapture")

    unsupported = subprocess.run(
        [TOKAC, "--experimental-memory-contracts=readonly", "-c", SOURCE,
         "-o", os.path.join(work, "unsupported.o")], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True)
    if unsupported.returncode == 0 or \
            "unsupported experimental memory contract" not in unsupported.stderr:
        raise AssertionError("unsupported contract flag was not rejected")


def compile_full_case(source, output):
    result = subprocess.run(
        [TOKAC, FLAG, "-c", source, "-o", output],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return source, result.returncode, result.stderr


def full_corpus(work):
    sources = []
    for root, _, files in os.walk("tests/pass"):
        for name in files:
            if name.endswith(".tk"):
                sources.append(os.path.abspath(os.path.join(root, name)))
    sources.sort()
    failures = []
    workers = int(os.environ.get("CORES", min(os.cpu_count() or 4, 8)))
    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = []
        for index, source in enumerate(sources):
            output = os.path.join(work, "full-%04d.o" % index)
            futures.append(executor.submit(compile_full_case, source, output))
        for future in as_completed(futures):
            source, status, stderr = future.result()
            if status != 0:
                failures.append((source, stderr))
    if failures:
        details = "\n".join("%s\n%s" % failure for failure in failures[:10])
        raise AssertionError("experimental full corpus failures: %d\n%s" %
                             (len(failures), details))
    print("Experimental nocapture full corpus PASSED: %d" % len(sources))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--full", action="store_true")
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(
            prefix="toka_experimental_nocapture_") as work:
        focused(work)
        if arguments.full:
            full_corpus(work)
    print("Experimental nocapture tests PASSED")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, ValueError, StopIteration) as error:
        print("Experimental nocapture tests FAILED: %s" % error,
              file=sys.stderr)
        sys.exit(1)
