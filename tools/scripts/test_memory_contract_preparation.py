#!/usr/bin/env python3
"""One-shot memory-contract preparation and fail-closed input changes."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/semantics/memory_summary/source_summary.tk"
REPLAY = ROOT / "tests/semantics/tki_replay/cases/own_cede_001_signature"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(compiler, env, *args):
    return subprocess.run(
        [str(compiler), *(str(arg) for arg in args)], cwd=ROOT, env=env,
        capture_output=True, text=True, timeout=90)


def find_record(document, suffix, contract):
    matches = [entry for entry in document["records"]
               if (entry["function"] == suffix or
                   entry["function"].endswith("_" + suffix))
               and entry["contract"] == contract]
    require(len(matches) == 1, "expected one %s/%s record" %
            (suffix, contract))
    return matches[0]


def dump(compiler, env, source, output):
    result = run(compiler, env, "--dump-memory-contracts=json", "-c",
                 source, "-o", output)
    require(result.returncode == 0 and output.is_file(),
            "contract dump failed: " + result.stderr)
    document = json.loads(result.stdout)
    require(document.get("schema") == "toka.memory-contract-shadow" and
            document.get("version") == 3, "contract schema changed")
    return document


def fault_matrix(compiler, env, source, work, faults):
    for fault, expected in faults:
        for flags, suffix in ((("-c",), ".o"),
                              (("--emit-llvm",), ".ll")):
            output = work / ("fault-" + fault + suffix)
            result = run(compiler, env,
                         "--memory-contract-prep-fault=" + fault,
                         *flags, source, "-o", output)
            require(result.returncode == 1 and expected in result.stderr and
                    not output.exists(),
                    "%s did not reject without %s: %s" %
                    (fault, suffix, result.stderr))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    compiler = args.build_dir.resolve() / "bin/tokac"
    require(compiler.is_file(), "tokac missing")
    env = dict(os.environ)
    env["TOKA_LIB"] = os.pathsep.join((
        str(ROOT / "lib"), str(args.build_dir.resolve() / "lib")))

    with tempfile.TemporaryDirectory(prefix="toka-contract-preparation-") as tmp:
        work = Path(tmp)
        baseline = work / "baseline.o"
        first = dump(compiler, env, SOURCE, baseline)
        repeat = dump(compiler, env, SOURCE, work / "repeat.o")
        require(first == repeat, "contract decisions are not deterministic")
        require(find_record(first, "ms_read", "nocapture")["decision"] ==
                "Candidate", "source-body nocapture changed")
        require(find_record(first, "ms_forward", "nocapture")["reason"] ==
                "IRCaptureDetected", "capture rejection changed")
        require(find_record(first, "ms_read", "noalias")["reason"] ==
                "SeparateNoAliasGate", "noalias escaped its own gate")

        plain = work / "plain.o"
        built = run(compiler, env, "-c", SOURCE, "-o", plain)
        require(built.returncode == 0 and plain.read_bytes() ==
                baseline.read_bytes(),
                "contract verification or dump changed object bytes: " +
                built.stderr)
        require(find_record(first, "ms_read", "readonly")["decision"] ==
                "Candidate", "baseline readonly decision changed")
        disabled_result = run(compiler, env, "--disable-borrow-check",
                              "--dump-memory-contracts=json", "-c", SOURCE,
                              "-o", work / "disabled.o")
        require(disabled_result.returncode == 0, disabled_result.stderr)
        disabled = json.loads(disabled_result.stdout)
        require(find_record(disabled, "ms_read", "readonly")["reason"] ==
                "BorrowCheckDisabled", "borrow-check mode was reused")

        fault_matrix(compiler, env, SOURCE, work, (
            ("record", "record differs from a fresh shadow analysis"),
            ("ir", "prepared memory contract IR or summary changed"),
            ("summary", "prepared memory contract IR or summary changed"),
            ("mode", "prepared memory contract input identity or mode changed"),
            ("attribute", "shadow contract readonly was emitted"),
        ))

        hidden = work / "source-hidden"
        shutil.copytree(REPLAY, hidden)
        provider = hidden / "lib.tk"
        provider_result = run(compiler, env, "-c", provider,
                              "-o", hidden / "lib.o")
        require(provider_result.returncode == 0, provider_result.stderr)
        provider.rename(hidden / "lib.tk.source-hidden")
        consumer = hidden / "pass_explicit_cede.tk"
        cached = dump(compiler, env, consumer, hidden / "consumer.o")
        require(find_record(cached, "consume_payload", "nocapture")[
                    "reason"] == "SignatureOnly",
                "source-hidden declaration acquired an unsound contract")
        fault_matrix(compiler, env, consumer, hidden, (
            ("record", "record differs from a fresh shadow analysis"),
        ))

    print("Memory contract preparation tests PASSED")


if __name__ == "__main__":
    main()
