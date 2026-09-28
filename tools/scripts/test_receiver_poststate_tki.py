#!/usr/bin/env python3
"""Source-hidden receiver dependency contract and interface-version gate."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
CANDIDATE = "0.9.9-24"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    compiler = build / "bin/tokac"
    require(compiler.is_file(), "tokac is missing")
    env = dict(os.environ, TOKA_LIB=os.pathsep.join(
        (str(ROOT / "lib"), str(build / "lib"))))

    with tempfile.TemporaryDirectory(prefix="toka-receiver-tki-") as directory:
        work = Path(directory)
        provider = work / "slot.tk"
        provider.write_text("""pub shape Input(view: str, other: str)
pub shape Slot(view: str)
impl Slot {
    pub fn replace(self#, next: Input)
    effects:
        self <- next.view
    { self.view = next.view }
}
""")
        for name, replacement, expected in (
            ("missing_effect", "", "ReceiverWriteDependencyUndeclared"),
            ("wrong_projection", "self <- next.missing",
             "receiver dependency projection is not a resolved field"),
            ("wrong_source_field", "self <- next.other",
             "ReceiverWriteDependencyUndeclared"),
        ):
            bad_source = work / (name + ".tk")
            bad_source.write_text(provider.read_text().replace(
                "self <- next.view", replacement))
            rejected = subprocess.run(
                [str(compiler), "--check-only", str(bad_source)], cwd=work,
                env=env, capture_output=True, text=True, timeout=45)
            require(rejected.returncode == 1 and expected in rejected.stderr,
                    name + ": invalid declaration was accepted")
        consumer = work / "consumer.tk"
        consumer.write_text("""import ./slot::{Slot, Input}
import core/string::{string}
fn escaped() -> Slot {
    auto owner = string::from("local")
    auto box# = Slot(view = "static")
    auto input = Input(view = owner.as_str(), other = "static")
    box#.replace(input)
    return cede box
}
fn main() -> i32 { return 0 }
""")
        built = subprocess.run(
            [str(compiler), "--emit-interface", "-c", "-o",
             str(work / "slot.o"), str(provider)], cwd=work, env=env,
            capture_output=True, text=True, timeout=60)
        require(built.returncode == 0, "provider failed: " + built.stderr)
        interface = work / "slot.tki"
        original = interface.read_text()
        require("self <- next.view" in original and
                "compiler_version: " + CANDIDATE in original,
                "interface lost receiver effect or version")
        provider.rename(work / "slot.tk.hidden")

        graph = subprocess.run(
            [str(compiler), "--dump-dependencies=json", str(consumer)],
            cwd=work, env=env, capture_output=True, text=True, timeout=45)
        require(graph.returncode == 0, "source-hidden dependency graph failed")
        modules = json.loads(graph.stdout)["modules"]
        require(any(path.endswith("slot.tki") and module["kind"] == "interface"
                    for path, module in modules.items()),
                "consumer did not use the source-hidden interface")

        normal = subprocess.run(
            [str(compiler), "--check-only", str(consumer)], cwd=work,
            env=env, capture_output=True, text=True, timeout=45)
        shadow = subprocess.run(
            [str(compiler), "--check-only", "--non-call-transfer-shadow=json",
             str(consumer)], cwd=work, env=env, capture_output=True,
            text=True, timeout=45)
        require(normal.returncode == shadow.returncode == 1 and
                normal.stderr == shadow.stderr and "error[E0455]" in normal.stderr,
                "source-hidden receiver effect did not reach lifetime check")
        for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = work / ("consumer" + suffix)
            rejected = subprocess.run(
                [str(compiler), flag, "-o", str(output), str(consumer)],
                cwd=work, env=env, capture_output=True, text=True, timeout=45)
            require(rejected.returncode == 1 and not output.exists(),
                    "source-hidden escape produced an artifact")

        interface.write_text(original.replace("self <- next.view",
                                              "self <- next.missing", 1))
        tampered = subprocess.run(
            [str(compiler), "--check-only", str(consumer)], cwd=work,
            env=env, capture_output=True, text=True, timeout=45)
        require(tampered.returncode == 1 and
                "Interface replay surface hash mismatch" in tampered.stderr and
                "E0455" not in tampered.stderr,
                "modified interface contract was accepted")

        interface.write_text(original.replace(
            "compiler_version: " + CANDIDATE,
            "compiler_version: 0.9.9-23", 1))
        old = subprocess.run(
            [str(compiler), "--check-only", str(consumer)], cwd=work,
            env=env, capture_output=True, text=True, timeout=45)
        require(old.returncode == 1 and
                "Compiler version mismatch" in old.stderr and
                "E0455" not in old.stderr,
                "old interface was accepted or silently replayed")

        interface.write_text(original.replace(
            "format_version: 4", "format_version: 3", 1))
        old_format = subprocess.run(
            [str(compiler), "--check-only", str(consumer)], cwd=work,
            env=env, capture_output=True, text=True, timeout=45)
        require(old_format.returncode == 1 and
                "Interface format version mismatch" in old_format.stderr and
                "E0455" not in old_format.stderr,
                "old interface format was accepted or silently replayed")

    print("receiver poststate TKI: source-hidden rejection, no artifacts, altered contract and old versions rejected")


if __name__ == "__main__":
    main()
