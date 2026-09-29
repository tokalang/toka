#!/usr/bin/env python3
"""Source-hidden receiver dependency contract and interface-version gate."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
CANDIDATE = "0.9.9-33"


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
        provider.write_text(provider.read_text() + """pub fn replace_direct(self#: Slot, next: Input)
effects:
    self <- next.view
{ self.view = next.view }
""")
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
        require("pub fn replace_direct" in original and
                original.count("self <- next.view") == 2 and
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

        direct_consumer = work / "direct_consumer.tk"
        direct_consumer.write_text(consumer.read_text().replace(
            "import ./slot::{Slot, Input}",
            "import ./slot::{Slot, Input, replace_direct}").replace(
            "box#.replace(input)", "replace_direct(box#, input)"))
        direct_normal = subprocess.run(
            [str(compiler), "--check-only", str(direct_consumer)], cwd=work,
            env=env, capture_output=True, text=True, timeout=45)
        direct_shadow = subprocess.run(
            [str(compiler), "--check-only", "--non-call-transfer-shadow=json",
             str(direct_consumer)], cwd=work, env=env, capture_output=True,
            text=True, timeout=45)
        require(direct_normal.returncode == direct_shadow.returncode == 1 and
                direct_normal.stderr == direct_shadow.stderr and
                "error[E0455]" in direct_normal.stderr,
                "source-hidden direct receiver effect did not reach lifetime check")
        for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = work / ("direct_consumer" + suffix)
            rejected = subprocess.run(
                [str(compiler), flag, "-o", str(output), str(direct_consumer)],
                cwd=work, env=env, capture_output=True, text=True, timeout=45)
            require(rejected.returncode == 1 and not output.exists(),
                    "source-hidden direct escape produced an artifact")

        live_owner = work / "source_hidden_owner_mutation.tk"
        live_owner.write_text("""import ./slot::{Slot, Input}
import core/string::{string}
fn main() -> i32 {
    auto owner# = string::from("local")
    auto box# = Slot(view = "static")
    auto input = Input(view = owner.as_str(), other = "static")
    box#.replace(input)
    owner#.push_str(" invalidates view")
    return 0
}
""")
        normal = subprocess.run(
            [str(compiler), "--check-only", str(live_owner)], cwd=work,
            env=env, capture_output=True, text=True, timeout=45)
        shadow = subprocess.run(
            [str(compiler), "--check-only", "--non-call-transfer-shadow=json",
             str(live_owner)], cwd=work, env=env, capture_output=True,
            text=True, timeout=45)
        require(normal.returncode == shadow.returncode == 1 and
                normal.stderr == shadow.stderr and "error[E0441]" in normal.stderr and
                "owner.buf" in normal.stderr,
                "source-hidden receiver loan did not protect its owner")
        for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = work / ("source_hidden_owner_mutation" + suffix)
            rejected = subprocess.run(
                [str(compiler), flag, "-o", str(output), str(live_owner)],
                cwd=work, env=env, capture_output=True, text=True, timeout=45)
            require(rejected.returncode == 1 and not output.exists(),
                    "source-hidden owner mutation produced an artifact")

        direct_live_owner = work / "source_hidden_direct_owner_mutation.tk"
        direct_live_owner.write_text(live_owner.read_text().replace(
            "import ./slot::{Slot, Input}",
            "import ./slot::{Slot, Input, replace_direct}").replace(
            "box#.replace(input)", "replace_direct(box#, input)"))
        direct_normal = subprocess.run(
            [str(compiler), "--check-only", str(direct_live_owner)], cwd=work,
            env=env, capture_output=True, text=True, timeout=45)
        direct_shadow = subprocess.run(
            [str(compiler), "--check-only", "--non-call-transfer-shadow=json",
             str(direct_live_owner)], cwd=work, env=env, capture_output=True,
            text=True, timeout=45)
        require(direct_normal.returncode == direct_shadow.returncode == 1 and
                direct_normal.stderr == direct_shadow.stderr and
                "error[E0441]" in direct_normal.stderr and
                "owner.buf" in direct_normal.stderr,
                "source-hidden direct receiver loan did not protect its owner")
        for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = work / ("source_hidden_direct_owner_mutation" + suffix)
            rejected = subprocess.run(
                [str(compiler), flag, "-o", str(output),
                 str(direct_live_owner)], cwd=work, env=env,
                capture_output=True, text=True, timeout=45)
            require(rejected.returncode == 1 and not output.exists(),
                    "source-hidden direct owner mutation produced an artifact")

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
            "compiler_version: 0.9.9-25", 1))
        old = subprocess.run(
            [str(compiler), "--check-only", str(consumer)], cwd=work,
            env=env, capture_output=True, text=True, timeout=45)
        require(old.returncode == 1 and
                "Compiler version mismatch" in old.stderr and
                "E0455" not in old.stderr,
                "old interface was accepted or silently replayed")

        interface.write_text(original.replace(
            "format_version: 5", "format_version: 4", 1))
        old_format = subprocess.run(
            [str(compiler), "--check-only", str(consumer)], cwd=work,
            env=env, capture_output=True, text=True, timeout=45)
        require(old_format.returncode == 1 and
                "Interface format version mismatch" in old_format.stderr and
                "E0455" not in old_format.stderr,
                "old interface format was accepted or silently replayed")

    with tempfile.TemporaryDirectory(prefix="toka-unsafe-call-tki-") as directory:
        work = Path(directory)
        provider = work / "provider.tk"
        provider.write_text("pub unsafe fn make() -> i32 { return 7 }\n"
                            "pub shape Cell(value: i32)\n"
                            "impl Cell { pub unsafe fn make_cell() -> Cell "
                            "{ return Cell(value = 7) } }\n")
        built = subprocess.run(
            [str(compiler), "--emit-interface", "-c", "-o",
             str(work / "provider.o"), str(provider)], cwd=work, env=env,
            capture_output=True, text=True, timeout=60)
        require(built.returncode == 0,
                "unsafe provider failed: " + built.stderr)
        interface = work / "provider.tki"
        original = interface.read_text()
        require("pub unsafe fn make()" in original and
                "pub unsafe fn make_cell()" in original and
                "format_version: 5" in original,
                "interface lost the unsafe call boundary")
        provider.rename(work / "provider.tk.hidden")
        consumer = work / "consumer.tk"
        consumer.write_text("import ./provider::{make}\n"
                            "fn main() -> i32 { return make() }\n")
        safe = subprocess.run([str(compiler), "--check-only", str(consumer)],
                              cwd=work, env=env, capture_output=True,
                              text=True, timeout=45)
        require(safe.returncode == 1 and "error[E0623]" in safe.stderr,
                "source-hidden unsafe call was accepted")
        for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = work / ("consumer" + suffix)
            rejected = subprocess.run(
                [str(compiler), flag, "-o", str(output), str(consumer)],
                cwd=work, env=env, capture_output=True, text=True, timeout=45)
            require(rejected.returncode == 1 and not output.exists(),
                    "source-hidden unsafe call produced an artifact")
        consumer.write_text("import ./provider::{make}\n"
                            "fn main() -> i32 { return unsafe make() }\n")
        allowed = subprocess.run([str(compiler), "--check-only", str(consumer)],
                                 cwd=work, env=env, capture_output=True,
                                 text=True, timeout=45)
        require(allowed.returncode == 0,
                "explicit unsafe call rejected: " + allowed.stderr)
        cell_consumer = work / "cell_consumer.tk"
        cell_consumer.write_text("import ./provider::{Cell}\n"
                                 "fn main() -> i32 { auto value = Cell::make_cell(); "
                                 "return value.value }\n")
        unsafe_static = subprocess.run(
            [str(compiler), "--check-only", str(cell_consumer)], cwd=work,
            env=env, capture_output=True, text=True, timeout=45)
        require(unsafe_static.returncode == 1 and
                "error[E0623]" in unsafe_static.stderr,
                "source-hidden unsafe static method was accepted")
        cell_consumer.write_text("import ./provider::{Cell}\n"
                                 "fn main() -> i32 { auto value = "
                                 "unsafe Cell::make_cell(); return value.value }\n")
        allowed_static = subprocess.run(
            [str(compiler), "--check-only", str(cell_consumer)], cwd=work,
            env=env, capture_output=True, text=True, timeout=45)
        require(allowed_static.returncode == 0,
                "explicit unsafe static call rejected: " + allowed_static.stderr)
        interface.write_text(original.replace("pub unsafe fn make()",
                                              "pub fn make()", 1))
        tampered = subprocess.run(
            [str(compiler), "--check-only", str(consumer)], cwd=work,
            env=env, capture_output=True, text=True, timeout=45)
        require(tampered.returncode == 1 and
                "Interface replay surface hash mismatch" in tampered.stderr,
                "unsafe contract was removed from a source-hidden interface")

        for name, source in (
            ("unsafe_trait_declaration",
             "pub trait @Example { pub unsafe fn get(self) -> i32 }\n"),
            ("unsafe_trait_implementation",
             "pub trait @Example { pub fn get(self) -> i32 }\n"
             "shape Value(value: i32)\n"
             "impl Value@Example { pub unsafe fn get(self) -> i32 { return self.value } }\n"),
        ):
            unsupported = work / (name + ".tk")
            unsupported.write_text(source)
            rejected = subprocess.run(
                [str(compiler), "--check-only", str(unsupported)], cwd=work,
                env=env, capture_output=True, text=True, timeout=45)
            require(rejected.returncode == 1 and
                    "error[E0624]" in rejected.stderr,
                    "unsafe trait method was accepted: " + rejected.stderr)

    print("receiver poststate TKI: source-hidden dependencies and unsafe calls verified")


if __name__ == "__main__":
    main()
