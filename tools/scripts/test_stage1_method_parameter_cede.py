#!/usr/bin/env python3

"""Qualify Stage-1 caller spelling for instance-method parameters only."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/stage1_method_cede"

if not os.environ.get("TOKA_LIB"):
    os.environ["TOKA_LIB"] = str(ROOT / "lib")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check(tokac, name, *extra):
    return subprocess.run(
        [str(tokac), *extra, "--check-only", str(FIXTURES / name)],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, timeout=30)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = Path(args.build_dir).resolve() / "bin" / "tokac"
    require(tokac.is_file(), "tokac is missing: " + str(tokac))

    named_failures = (
        "named_copy_requires_cede.tk",
        "imported_named_copy_requires_cede.tk",
        "async_named_copy_requires_cede.tk",
    )
    results = {name: check(tokac, name) for name in named_failures}
    for name, result in results.items():
        require(result.returncode != 0 and
                result.stderr.count("error[E04509]") == 1 and
                "E0438" not in result.stderr and
                "E0410" not in result.stderr,
                name + " accepted or mutated a bare named source")
    named = results["named_copy_requires_cede.tk"]

    structured = check(tokac, "named_copy_requires_cede.tk",
                       "--diagnostics-json")
    document = json.loads(structured.stdout)
    diagnostic = next(item for item in document["diagnostics"]
                      if item["code"] == "E04509")
    require(len(diagnostic["fixes"]) == 1 and
            diagnostic["fixes"][0]["applicability"] ==
            "machine-applicable" and
            diagnostic["fixes"][0]["edits"][0]["newText"] == "cede ",
            "method E04570 has no machine-applicable cede insertion")

    explicit = check(tokac, "explicit_copy_invalidates.tk")
    require(explicit.returncode != 0 and "error[E0438]" in explicit.stderr,
            "explicit method-parameter cede did not invalidate Copy source")

    multi = check(tokac, "multi_argument_atomic_rejection.tk")
    require(multi.returncode != 0 and
            multi.stderr.count("error[E04509]") == 1 and
            "E0438" not in multi.stderr and "E0410" not in multi.stderr,
            "rejected method call changed an argument source")

    receiver_rollback = check(
        tokac, "rejected_parameter_restores_receiver.tk")
    require(receiver_rollback.returncode != 0 and
            receiver_rollback.stderr.count("error[E04509]") == 1 and
            "E0438" not in receiver_rollback.stderr and
            "E0410" not in receiver_rollback.stderr,
            "rejected method parameter leaked receiver invalidation")
    receiver_shadow = check(
        tokac, "rejected_parameter_restores_receiver.tk",
        "--call-transfer-shadow=json")
    require(receiver_rollback.returncode == receiver_shadow.returncode and
            receiver_rollback.stderr == receiver_shadow.stderr,
            "receiver rollback shadow changed activated diagnostics")

    for name in (
            "rejected_expression_receiver_restores_source.tk",
            "rejected_nested_receiver_restores_source.tk"):
        rejected_receiver = check(tokac, name)
        require(rejected_receiver.returncode != 0 and
                rejected_receiver.stderr.count("error[E04509]") == 1 and
                "E0438" not in rejected_receiver.stderr and
                "E0410" not in rejected_receiver.stderr,
                name + " evaluated receiver outside the rejection snapshot")

    generic_rejection = check(
        tokac, "generic_rejection_restores_source.tk")
    require(generic_rejection.returncode != 0 and
            "error[E04510]" in generic_rejection.stderr and
            "E0438" not in generic_rejection.stderr and
            "E0410" not in generic_rejection.stderr,
            "rejected generic method leaked argument source state")

    unsafe_bare = check(tokac, "unsafe_wrapper_requires_cede.tk")
    require(unsafe_bare.returncode != 0 and
            unsafe_bare.stderr.count("error[E04509]") == 1 and
            "E0438" not in unsafe_bare.stderr and
            "E0410" not in unsafe_bare.stderr,
            "unsafe wrapper bypassed the method parameter handshake")
    unsafe_explicit = check(tokac, "unsafe_wrapper_explicit_cede.tk")
    require(unsafe_explicit.returncode == 0 and not unsafe_explicit.stderr,
            "explicit method cede or unsafe temporary exemption regressed")

    positive = check(tokac, "temporary_and_copy_keep_live.tk")
    require(positive.returncode == 0 and not positive.stderr,
            "method Copy KeepLive or NoSourcePlace exemption regressed")
    receiver = check(tokac, "receiver_out_of_slice.tk")
    require(receiver.returncode == 0 and "E04509" not in receiver.stderr,
            "method parameter slice changed receiver spelling")
    generic_method = check(tokac, "generic_method_out_of_slice.tk")
    require(generic_method.returncode != 0 and
            "error[E04510]" in generic_method.stderr and
            "E04509" not in generic_method.stderr,
            "concrete method slice activated a generic method parameter")

    with tempfile.TemporaryDirectory(
            prefix="toka-stage1-method-tki-") as temp:
        work = Path(temp)
        provider = work / "method_provider.tk"
        provider.write_text(
            "pub shape Sink()\n\n"
            "impl Sink {\n"
            "    pub fn consume(self, cede value: i32) -> i32 {\n"
            "        return cede value\n"
            "    }\n"
            "}\n", encoding="utf-8")
        emitted = subprocess.run(
            [str(tokac), "-c", "--emit-interface", str(provider),
             "-o", str(work / "method_provider.o")], cwd=ROOT,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            timeout=30)
        require(emitted.returncode == 0 and
                (work / "method_provider.tki").is_file(),
                "could not emit source-hidden method provider")
        provider.unlink()
        consumer = work / "main.tk"
        consumer.write_text(
            "import method_provider::{Sink}\n\n"
            "fn main() -> i32 {\n"
            "    auto sink = Sink()\n"
            "    auto value = 7:i32\n"
            "    return sink.consume(value)\n"
            "}\n", encoding="utf-8")
        hidden = subprocess.run(
            [str(tokac), "-I", str(work), "--check-only", str(consumer)],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=30)
        require(hidden.returncode != 0 and
                hidden.stderr.count("error[E04509]") == 1 and
                "E0438" not in hidden.stderr and
                "E0410" not in hidden.stderr,
                "source-hidden concrete method lost Stage-1 caller spelling")

    with tempfile.TemporaryDirectory(prefix="toka-stage1-method-") as temp:
        artifact = Path(temp) / "method-positive"
        built = subprocess.run(
            [str(tokac), str(FIXTURES / "temporary_and_copy_keep_live.tk"),
             "-o", str(artifact)], cwd=ROOT, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, timeout=30)
        require(built.returncode == 0 and artifact.is_file(), built.stderr)
        ran = subprocess.run(
            [str(artifact)], cwd=temp, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, timeout=10)
        require(ran.returncode == 0 and not ran.stderr,
                "method Stage-1 positive artifact failed")

    shadow = check(tokac, "named_copy_requires_cede.tk",
                   "--call-transfer-shadow=json")
    require(named.returncode == shadow.returncode and
            named.stderr == shadow.stderr,
            "method shadow changed activated diagnostics")
    payload = json.loads(shadow.stdout)
    matches = [
        transaction for transaction in payload["transactions"]
        if transaction["route"] == "method" and
        transaction["callee"] == "consume" and
        transaction["location"]["file"].endswith(
            "named_copy_requires_cede.tk")
    ]
    require(len(matches) == 1 and not matches[0]["commit_allowed"] and
            matches[0]["items"][1]["role"] == "argument" and
            matches[0]["items"][1]["formal_index"] == 2 and
            matches[0]["items"][1]["rejection"] ==
            "MissingCedeForNamedSource" and
            matches[0]["items"][1]["source"] == "NoStateChange",
            "method diagnostic diverged from frozen Stage-0 plan")

    legacy = check(tokac, "named_copy_requires_cede.tk",
                   "--stage1-legacy-ordinary-cede")
    require(legacy.returncode == 0,
            "historical replay no longer preserves method fixture behavior")

    print("stage1 method parameter cede: pass")


if __name__ == "__main__":
    main()
