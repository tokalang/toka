#!/usr/bin/env python3

"""Qualify exact-once dyn-fn environment lifetime across binding copies."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/dyn_fn_binding_lifecycle"
INDIRECT_FIXTURES = ROOT / "tests/semantics/stage1_indirect_cede"

if not os.environ.get("TOKA_LIB"):
    os.environ["TOKA_LIB"] = str(ROOT / "lib")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = Path(args.build_dir).resolve() / "bin" / "tokac"
    require(tokac.is_file(), "tokac is missing: " + str(tokac))

    with tempfile.TemporaryDirectory(prefix="toka-dyn-fn-lifecycle-") as temp:
        cases = (
            FIXTURES / "binding_copy_exact_once.tk",
            FIXTURES / "projected_copy_exact_once.tk",
            FIXTURES / "binding_transfer_exact_once.tk",
            FIXTURES / "consuming_transfer_exact_once.tk",
            FIXTURES / "consuming_projection_transfer_exact_once.tk",
            INDIRECT_FIXTURES / "alias_dyn_binding_copy_indeterminate.tk",
            INDIRECT_FIXTURES / "alias_fn_binding_copy_indeterminate.tk",
        )
        for source in cases:
            artifact = Path(temp) / source.stem
            built = subprocess.run(
                [str(tokac), str(source), "-o", str(artifact)],
                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, timeout=30)
            require(built.returncode == 0 and artifact.is_file(), built.stderr)
            ran = subprocess.run(
                [str(artifact)], cwd=temp, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True, timeout=10)
            require(ran.returncode == 0,
                    source.name +
                    " did not release its environment exactly once: " +
                    ran.stderr)

    for source in (
            "consuming_binding_copy_rejected.tk",
            "consuming_projection_copy_rejected.tk"):
        artifact = Path(tempfile.gettempdir()) / source.removesuffix(".tk")
        if artifact.exists():
            artifact.unlink()
        rejected = subprocess.run(
            [str(tokac), str(FIXTURES / source), "-o", str(artifact)],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=30)
        require(rejected.returncode != 0 and
                rejected.stderr.count("error[E04653]") == 1 and
                not artifact.exists(),
                source + " copied a consuming dynamic environment")

    rejected_modes = {
        "consuming_transfer_requires_cede.tk": "error[E04591]",
        "consuming_transfer_forwarding_rejected.tk": "error[E04571]",
    }
    for source, diagnostic in rejected_modes.items():
        rejected = subprocess.run(
            [str(tokac), "--check-only", str(FIXTURES / source)], cwd=ROOT,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            timeout=30)
        require(rejected.returncode != 0 and
                rejected.stderr.count(diagnostic) == 1 and
                "E0438" not in rejected.stderr and
                "E0410" not in rejected.stderr,
                source + " lost its consuming callable mode")

    with tempfile.TemporaryDirectory(prefix="toka-dyn-fn-interface-") as temp:
        work = Path(temp)
        provider = work / "provider.tk"
        provider.write_text(
            "pub fn make() -> dyn fn(i32) -> i32 {\n"
            "    return { value => value }:dyn fn(i32) -> i32\n"
            "}\n", encoding="utf-8")
        emitted = subprocess.run(
            [str(tokac), "-c", "--emit-interface", str(provider),
             "-o", str(work / "provider.o")], cwd=ROOT,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            timeout=30)
        interface = work / "provider.tki"
        require(emitted.returncode == 0 and interface.is_file(),
                emitted.stderr)
        interface.write_text(
            interface.read_text(encoding="utf-8").replace(
                "compiler_version: 0.9.9-17",
                "compiler_version: 0.9.9-16"),
            encoding="utf-8")
        provider.unlink()
        consumer = work / "main.tk"
        consumer.write_text(
            "import provider::{make}\n"
            "fn main() -> i32 {\n"
            "    auto callback = make()\n"
            "    return callback(1:i32)\n"
            "}\n",
            encoding="utf-8")
        rejected = subprocess.run(
            [str(tokac), "-I", str(work), "--check-only", str(consumer)],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=30)
        require(rejected.returncode != 0 and
                "Compiler version mismatch" in rejected.stderr and
                "0.9.9-17" in rejected.stderr and
                "0.9.9-16" in rejected.stderr,
                "old dyn-fn environment ABI was not rejected")

    print("dyn fn binding lifecycle: pass")


if __name__ == "__main__":
    main()
