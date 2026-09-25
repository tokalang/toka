#!/usr/bin/env python3
"""Concrete payload captures select payload storage without changing handle ABI."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/rc14_call_argument_address"
POSITIVE = (
    "payload_capture_matrix.tk",
    "wrapped_payload_identity.tk",
    "owner_lifecycle.tk",
    "cede_payload_from_unique.tk",
)
SSA = r"%[A-Za-z0-9_.$]+"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(command, *, cwd, env, timeout=120):
    return subprocess.run(
        [str(part) for part in command], cwd=cwd, env=env,
        capture_output=True, text=True, timeout=timeout,
    )


def verify_payload_address_ir(ir):
    main = re.search(r"^define i32 @main\(\) \{\n(.*?)^\}", ir,
                     re.M | re.S)
    require(main is not None, "main IR body is missing")
    body = main.group(1)
    definitions = dict(re.findall(r"^\s*(" + SSA + r") = (.+)$", body,
                                  re.M))

    def call_operand(callee):
        call = re.search(r"\bcall (?:i32|void) @" + callee +
                         r"\(ptr (" + SSA + r")(?:,|\))", body)
        require(call is not None, callee + " call is missing")
        require(call.group(1) in definitions,
                callee + " does not pass an SSA-derived address")
        return call.group(1)

    def loaded_from(operand, callee):
        load = re.match(r"load ptr, ptr (" + SSA + r")", definitions[operand])
        require(load is not None,
                callee + " passed a handle slot/carrier instead of its payload")
        return load.group(1)

    unique_slot = loaded_from(call_operand("read_unique"), "read_unique")
    require(definitions.get(unique_slot, "").startswith("alloca ptr"),
            "unique payload address is not loaded from the owning handle slot")

    shared_field = loaded_from(call_operand("read_shared"), "read_shared")
    shared_gep = definitions.get(shared_field, "")
    require(shared_gep.startswith("getelementptr") and
            "{ ptr, ptr }" in shared_gep,
            "shared payload address is not loaded from the carrier data field")

    write_slot = loaded_from(call_operand("write_plain"), "write_plain")
    require(definitions.get(write_slot, "").startswith("alloca ptr"),
            "writable payload capture does not address the original owner")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    compiler = args.build_dir.resolve() / "bin/tokac"
    require(compiler.is_file(), "tokac is missing")
    env = dict(os.environ)
    env["TOKA_LIB"] = os.pathsep.join((
        str(ROOT / "lib"), str(args.build_dir.resolve() / "lib")))

    with tempfile.TemporaryDirectory(prefix="toka-rc14-call-address-") as temp:
        work = Path(temp)
        for name in POSITIVE:
            source = FIXTURES / name
            artifact = work / source.stem
            built = run((compiler, source, "-o", artifact), cwd=ROOT, env=env)
            require(built.returncode == 0 and artifact.is_file(),
                    name + " failed to compile:\n" + built.stderr)
            executed = run((artifact,), cwd=work, env=env, timeout=15)
            require(executed.returncode == 0,
                    name + " failed at runtime with exit " +
                    str(executed.returncode) + ":\n" + executed.stderr)

            normal = run((compiler, "--check-only", source), cwd=ROOT, env=env)
            shadow = run((compiler, "--call-transfer-shadow=json",
                          "--check-only", source), cwd=ROOT, env=env)
            require(normal.returncode == shadow.returncode == 0 and
                    normal.stderr == shadow.stderr,
                    name + " changed normal/shadow admission or diagnostics")

        ir_file = work / "payload_capture_matrix.ll"
        emitted = run((compiler, "--emit-llvm", FIXTURES / POSITIVE[0],
                       "-o", ir_file), cwd=ROOT, env=env)
        require(emitted.returncode == 0 and ir_file.is_file(),
                "payload address LLVM IR emission failed:\n" +
                emitted.stderr)
        verify_payload_address_ir(ir_file.read_text(encoding="utf-8"))

        rejected = FIXTURES / "readonly_payload_reject.tk"
        for mode, flags, suffix in (
            ("check", ("--check-only",), ".check"),
            ("object", ("-c",), ".o"),
            ("llvm", ("--emit-llvm",), ".ll"),
        ):
            output = work / ("readonly" + suffix)
            result = run((compiler, *flags, rejected, "-o", output),
                         cwd=ROOT, env=env)
            require(result.returncode != 0 and "error[E04571]" in result.stderr
                    and not output.exists(),
                    "read-only payload upgrade did not fail closed in " + mode)

    print("RC14 concrete payload call-address matrix PASSED")


if __name__ == "__main__":
    main()
