#!/usr/bin/env python3

"""Qualify Stage-1 caller spelling for concrete dynamic-trait parameters."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/stage1_dynamic_trait_cede"

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

    named_results = {
        name: check(tokac, name)
        for name in (
            "named_copy_requires_cede.tk",
            "imported_named_copy_requires_cede.tk")
    }
    for name, result in named_results.items():
        require(result.returncode != 0 and
                result.stderr.count("error[E04509]") == 1 and
                "E0438" not in result.stderr and
                "E0410" not in result.stderr,
                name + " accepted or mutated a bare named source")
    named = named_results["named_copy_requires_cede.tk"]

    structured = check(tokac, "named_copy_requires_cede.tk",
                       "--diagnostics-json")
    document = json.loads(structured.stdout)
    diagnostic = next(item for item in document["diagnostics"]
                      if item["code"] == "E04509")
    require(len(diagnostic["fixes"]) == 1 and
            diagnostic["fixes"][0]["applicability"] ==
            "machine-applicable" and
            diagnostic["fixes"][0]["edits"][0]["newText"] == "cede ",
            "dynamic-trait E04509 has no exact cede insertion")

    explicit = check(tokac, "explicit_copy_invalidates.tk")
    require(explicit.returncode != 0 and "error[E0438]" in explicit.stderr,
            "explicit dynamic-trait cede did not invalidate Copy source")

    multi = check(tokac, "multi_argument_atomic_rejection.tk")
    require(multi.returncode != 0 and
            multi.stderr.count("error[E04509]") == 1 and
            "E0438" not in multi.stderr and "E0410" not in multi.stderr,
            "rejected dynamic-trait call changed an argument source")

    unsafe_bare = check(tokac, "unsafe_wrapper_requires_cede.tk")
    require(unsafe_bare.returncode != 0 and
            unsafe_bare.stderr.count("error[E04509]") == 1 and
            "E0438" not in unsafe_bare.stderr and
            "E0410" not in unsafe_bare.stderr,
            "unsafe wrapper bypassed dynamic-trait parameter handshake")
    unsafe_explicit = check(tokac, "unsafe_wrapper_explicit_cede.tk")
    require(unsafe_explicit.returncode == 0 and not unsafe_explicit.stderr,
            "explicit dynamic-trait cede or unsafe temporary regressed")

    receiver = check(tokac, "rejected_expression_receiver_restores_source.tk")
    require(receiver.returncode != 0 and
            receiver.stderr.count("error[E04509]") == 1 and
            "E0438" not in receiver.stderr and "E0410" not in receiver.stderr,
            "dynamic receiver evaluation escaped the rejection snapshot")

    with tempfile.TemporaryDirectory(
            prefix="toka-stage1-dynamic-tki-") as temp:
        work = Path(temp)
        provider = work / "dynamic_provider.tk"
        provider.write_text(
            "pub trait @Sink {\n"
            "    fn take(self, cede value: i32) -> i32\n"
            "}\n\n"
            "pub shape IntSink()\n\n"
            "impl IntSink@Sink {\n"
            "    fn take(self, cede value: i32) -> i32 {\n"
            "        return cede value\n"
            "    }\n"
            "}\n", encoding="utf-8")
        emitted = subprocess.run(
            [str(tokac), "-c", "--emit-interface", str(provider),
             "-o", str(work / "dynamic_provider.o")], cwd=ROOT,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            timeout=30)
        require(emitted.returncode == 0 and
                (work / "dynamic_provider.tki").is_file(),
                "could not emit source-hidden dynamic-trait provider")
        provider.unlink()
        consumer = work / "main.tk"
        consumer.write_text(
            "import dynamic_provider::{@Sink, IntSink}\n\n"
            "fn invoke(sink: dyn @Sink) -> i32 {\n"
            "    auto value = 7:i32\n"
            "    return sink.take(value)\n"
            "}\n\n"
            "fn main() -> i32 {\n"
            "    auto sink = IntSink()\n"
            "    return invoke(sink)\n"
            "}\n", encoding="utf-8")
        hidden = subprocess.run(
            [str(tokac), "-I", str(work), "--check-only", str(consumer)],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=30)
        require(hidden.returncode != 0 and
                hidden.stderr.count("error[E04509]") == 1 and
                "E0438" not in hidden.stderr and
                "E0410" not in hidden.stderr,
                "source-hidden dynamic trait lost Stage-1 caller spelling")

    positive = check(tokac, "temporary_and_copy_keep_live.tk")
    require(positive.returncode == 0 and not positive.stderr,
            "dynamic-trait Copy KeepLive or temporary exemption regressed")
    with tempfile.TemporaryDirectory(prefix="toka-stage1-dynamic-") as temp:
        artifact = Path(temp) / "dynamic-positive"
        built = subprocess.run(
            [str(tokac), str(FIXTURES / "temporary_and_copy_keep_live.tk"),
             "-o", str(artifact)], cwd=ROOT, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, timeout=30)
        require(built.returncode == 0 and artifact.is_file(), built.stderr)
        ran = subprocess.run(
            [str(artifact)], cwd=temp, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, timeout=10)
        require(ran.returncode == 0 and not ran.stderr,
                "dynamic-trait Stage-1 positive artifact failed")

    shadow = check(tokac, "named_copy_requires_cede.tk",
                   "--call-transfer-shadow=json")
    require(named.returncode == shadow.returncode and
            named.stderr == shadow.stderr,
            "dynamic-trait shadow changed activated diagnostics")
    payload = json.loads(shadow.stdout)
    matches = [
        transaction for transaction in payload["transactions"]
        if transaction["route"] == "dynamic-trait-method" and
        transaction["callee"] == "Sink::take" and
        transaction["location"]["file"].endswith(
            "named_copy_requires_cede.tk")
    ]
    require(len(matches) == 1 and not matches[0]["commit_allowed"] and
            matches[0]["items"][1]["role"] == "argument" and
            matches[0]["items"][1]["formal_index"] == 2 and
            matches[0]["items"][1]["rejection"] ==
            "MissingCedeForNamedSource" and
            matches[0]["items"][1]["source"] == "NoStateChange",
            "dynamic-trait diagnostic diverged from Stage-0 plan")

    legacy = check(tokac, "named_copy_requires_cede.tk",
                   "--stage1-legacy-ordinary-cede")
    require(legacy.returncode == 0,
            "historical replay activated dynamic-trait caller spelling")

    print("stage1 dynamic-trait parameter cede: pass")


if __name__ == "__main__":
    main()
