#!/usr/bin/env python3

"""Qualify Stage-1 caller spelling for indirect fn/dyn-fn parameters."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/stage1_indirect_cede"

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
            "indirect_fn_named_copy_requires_cede.tk",
            "indirect_dyn_named_copy_requires_cede.tk",
            "unique_named_requires_cede.tk")
    }
    for name, result in named_results.items():
        require(result.returncode != 0 and
                result.stderr.count("error[E04570]") == 1 and
                "E0438" not in result.stderr and
                "E0410" not in result.stderr,
                name + " accepted or mutated a bare named source")

    unique_document = json.loads(check(
        tokac, "unique_named_requires_cede.tk", "--diagnostics-json").stdout)
    unique_diagnostic = next(item for item in unique_document["diagnostics"]
                             if item["code"] == "E04570")
    unique_edit = unique_diagnostic["fixes"][0]["edits"][0]
    unique_start = unique_edit["range"]["start"]
    unique_line = (FIXTURES / "unique_named_requires_cede.tk").read_text(
        encoding="utf-8").splitlines()[unique_start["line"]]
    require(unique_edit["newText"] == "cede " and
            unique_edit["range"]["end"] == unique_start and
            unique_line[unique_start["character"]:].startswith("^source"),
            "indirect E04570 fix did not preserve unique-handle spelling")

    explicit = check(tokac, "explicit_copy_invalidates.tk")
    require(explicit.returncode != 0 and "error[E0438]" in explicit.stderr,
            "explicit indirect cede did not invalidate Copy source")

    unsafe_bare = check(tokac, "unsafe_wrapper_requires_cede.tk")
    require(unsafe_bare.returncode != 0 and
            unsafe_bare.stderr.count("error[E04570]") == 2 and
            "E0438" not in unsafe_bare.stderr and
            "E0410" not in unsafe_bare.stderr,
            "unsafe wrapper bypassed indirect parameter handshake")
    unsafe_explicit = check(tokac, "unsafe_wrapper_explicit_cede.tk")
    require(unsafe_explicit.returncode == 0 and not unsafe_explicit.stderr,
            "explicit indirect cede or unsafe temporary regressed")

    multi = check(tokac, "multi_argument_atomic_rejection.tk")
    require(multi.returncode != 0 and
            multi.stderr.count("error[E04570]") == 1 and
            "E0438" not in multi.stderr and "E0410" not in multi.stderr,
            "rejected indirect call changed an argument source")

    for name in (
            "nested_fn_rejection_restores_source.tk",
            "nested_dyn_rejection_restores_source.tk"):
        nested = check(tokac, name)
        require(nested.returncode != 0 and
                nested.stderr.count("error[E04570]") == 1 and
                "E0438" not in nested.stderr and
                "E0410" not in nested.stderr,
                name + " leaked a nested argument transfer")

    receiver = check(
        tokac, "rejected_parameter_restores_callable_receiver.tk")
    require(receiver.returncode != 0 and
            receiver.stderr.count("error[E04570]") == 1 and
            "E0438" not in receiver.stderr and
            "E0410" not in receiver.stderr,
            "indirect rejection leaked callable receiver consumption")

    borrowed = check(tokac, "borrowed_identity_out_of_slice.tk")
    require("E04570" not in borrowed.stderr,
            "borrowed identity was pulled into indirect value activation")
    for name in (
            "generic_fn_formal_out_of_slice.tk",
            "generic_dyn_formal_out_of_slice.tk"):
        generic = check(tokac, name)
        require(generic.returncode == 0 and "E04570" not in generic.stderr,
                name + " lost declaration-side generic provenance")

    positive = check(tokac, "temporary_and_copy_keep_live.tk")
    require(positive.returncode == 0 and not positive.stderr,
            "indirect Copy KeepLive or temporary exemption regressed")
    with tempfile.TemporaryDirectory(prefix="toka-stage1-indirect-") as temp:
        artifact = Path(temp) / "indirect-positive"
        built = subprocess.run(
            [str(tokac), str(FIXTURES / "temporary_and_copy_keep_live.tk"),
             "-o", str(artifact)], cwd=ROOT, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, timeout=30)
        require(built.returncode == 0 and artifact.is_file(), built.stderr)
        ran = subprocess.run(
            [str(artifact)], cwd=temp, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, timeout=10)
        require(ran.returncode == 0 and not ran.stderr,
                "indirect Stage-1 positive artifact/drop accounting failed")

    for name, route in (
            ("indirect_fn_named_copy_requires_cede.tk", "indirect-fn"),
            ("indirect_dyn_named_copy_requires_cede.tk", "indirect-dyn-fn")):
        normal = named_results[name]
        shadow = check(tokac, name, "--call-transfer-shadow=json")
        require(normal.returncode == shadow.returncode and
                normal.stderr == shadow.stderr,
                name + " shadow changed activated diagnostics")
        payload = json.loads(shadow.stdout)
        matches = [
            transaction for transaction in payload["transactions"]
            if transaction["route"] == route and
            transaction["callee"] == "callback" and
            transaction["location"]["file"].endswith(name)
        ]
        require(len(matches) == 1 and not matches[0]["commit_allowed"] and
                matches[0]["items"][1]["role"] == "argument" and
                matches[0]["items"][1]["rejection"] ==
                "MissingCedeForNamedSource" and
                matches[0]["items"][1]["source"] == "NoStateChange",
                name + " diagnostic diverged from Stage-0 parameter plan")

    with tempfile.TemporaryDirectory(
            prefix="toka-stage1-indirect-tki-") as temp:
        work = Path(temp)
        provider = work / "callback_provider.tk"
        provider.write_text(
            "pub fn make() -> fn(cede i32) -> i32 {\n"
            "    return { value => cede value }:fn(cede i32) -> i32\n"
            "}\n", encoding="utf-8")
        emitted = subprocess.run(
            [str(tokac), "-c", "--emit-interface", str(provider),
             "-o", str(work / "callback_provider.o")], cwd=ROOT,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            timeout=30)
        require(emitted.returncode == 0 and
                (work / "callback_provider.tki").is_file(),
                "could not emit source-hidden indirect provider")
        provider.unlink()
        consumer = work / "main.tk"
        consumer.write_text(
            "import callback_provider::{make}\n\n"
            "fn main() -> i32 {\n"
            "    auto callback = make()\n"
            "    auto value = 7:i32\n"
            "    return callback(value)\n"
            "}\n", encoding="utf-8")
        hidden = subprocess.run(
            [str(tokac), "-I", str(work), "--check-only", str(consumer)],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=30)
        require(hidden.returncode != 0 and
                hidden.stderr.count("error[E04570]") == 1 and
                "E0438" not in hidden.stderr and
                "E0410" not in hidden.stderr,
                "source-hidden indirect signature lost caller spelling")

    for name in (
            "indirect_fn_named_copy_requires_cede.tk",
            "indirect_dyn_named_copy_requires_cede.tk"):
        legacy = check(tokac, name, "--stage1-legacy-ordinary-cede")
        require(legacy.returncode == 0,
                name + " changed historical replay behavior")

    print("stage1 indirect parameter cede: pass")


if __name__ == "__main__":
    main()
