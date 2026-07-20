#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import sys
import tempfile


TOKAC = os.path.abspath(os.environ.get("TOKAC", "./build/bin/tokac"))
SOURCE = "tests/semantics/memory_summary/source_summary.tk"


def compile_summary(source, output, extra=None, cwd=None):
    command = [TOKAC, "--dump-memory-summaries=json", "-c", source,
               "-o", output]
    if extra:
        command[1:1] = extra
    result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise AssertionError(result.stderr)
    return result.stdout, json.loads(result.stdout)


def find_function(document, suffix):
    matches = [entry for entry in document["functions"]
               if entry["name"] == suffix or entry["name"].endswith("_" + suffix)]
    if len(matches) != 1:
        raise AssertionError("expected one summary for %s, found %d" %
                             (suffix, len(matches)))
    return matches[0]


def root(function, name):
    matches = [entry for entry in function["roots"] if entry["name"] == name]
    if len(matches) != 1:
        raise AssertionError("missing root %s in %s" % (name, function["name"]))
    return matches[0]


def require(values, *expected):
    missing = set(expected) - set(values)
    if missing:
        raise AssertionError("missing effects: " + ", ".join(sorted(missing)))


def main():
    with tempfile.TemporaryDirectory(prefix="toka_memory_summary_") as work:
        first_text, first = compile_summary(
            SOURCE, os.path.join(work, "first.o"))
        second_text, _ = compile_summary(
            SOURCE, os.path.join(work, "second.o"))
        if first_text != second_text:
            raise AssertionError("memory summary output is not deterministic")
        if first.get("schema") != "toka.memory-summary" or \
                first.get("version") != 2:
            raise AssertionError("unexpected memory summary schema")

        write = find_function(first, "ms_write")
        require(root(write, "data")["local_effects"], "read", "write")

        forward = find_function(first, "ms_forward")
        if "write" in root(forward, "data")["local_effects"]:
            raise AssertionError("transitive write leaked into local effects")
        require(root(forward, "data")["effects"], "read", "write")

        store = find_function(first, "ms_store")
        require(root(store, "target")["local_effects"], "write", "rebind")
        ir_path = os.path.join(work, "summary.ll")
        ir = subprocess.run(
            [TOKAC, "--emit-llvm", SOURCE, "-o", ir_path],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if ir.returncode != 0:
            raise AssertionError(ir.stderr)
        with open(ir_path, encoding="utf-8") as handle:
            if '!{!"rebind"}' not in handle.read():
                raise AssertionError("handle store lacks rebind IR evidence")

        consume = find_function(first, "ms_consume")
        require(root(consume, "data")["effects"], "transfer", "invalidate",
                "escape")

        raw = find_function(first, "ms_raw")
        require(raw["local_effects"], "raw_provenance", "unsafe_boundary",
                "unknown_boundary")
        require(root(raw, "data")["effects"], "unknown")

        async_fn = find_function(first, "ms_async")
        require(async_fn["local_effects"], "allocate", "suspend")
        require(root(async_fn, "data")["effects"], "capture", "escape",
                "transfer", "invalidate")

        require(find_function(first, "ms_touch_global")["local_effects"],
                "touch_global")
        require(find_function(first, "ms_memory")["local_effects"],
                "allocate", "free", "unsafe_boundary", "unknown_boundary")

        _, degraded = compile_summary(
            SOURCE, os.path.join(work, "degraded.o"),
            extra=["--disable-borrow-check"])
        require(root(find_function(degraded, "ms_read"), "data")["effects"],
                "unknown")

        case = os.path.abspath(
            "tests/semantics/tki_replay/cases/own_cede_001_signature")
        replay = os.path.join(work, "replay")
        shutil.copytree(case, replay)
        tokac = os.path.abspath(TOKAC)
        provider = subprocess.run(
            [tokac, "-c", os.path.join(replay, "lib.tk"), "-o",
             os.path.join(replay, "lib.o")],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if provider.returncode != 0:
            raise AssertionError(provider.stderr)
        os.rename(os.path.join(replay, "lib.tk"),
                  os.path.join(replay, "lib.tk.source-hidden"))
        consumer = next(name for name in os.listdir(replay)
                        if name.startswith("pass_") and name.endswith(".tk"))
        _, replay_summary = compile_summary(
            os.path.join(replay, consumer), os.path.join(replay, "consumer.o"))
        opaque = find_function(replay_summary, "consume_payload")
        if opaque["origin"] != "signature_only":
            raise AssertionError("TKI summary claimed source-body origin")
        require(root(opaque, "p")["effects"], "transfer", "invalidate",
                "escape", "unknown")

    print("Memory summary tests PASSED")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, ValueError, StopIteration) as error:
        print("Memory summary tests FAILED: %s" % error, file=sys.stderr)
        sys.exit(1)
