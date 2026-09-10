#!/usr/bin/env python3
"""Real source factory sealing and fault gates; no fabricated qualification bits."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FAULTS = ("missing", "site", "declaration", "input", "owner", "element", "kind", "incomplete")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    with tempfile.TemporaryDirectory(prefix="toka-native-factory-plans-") as directory:
        work = Path(directory)
        library = work / "lib"
        shutil.copytree(ROOT / "lib", library,
                        ignore=shutil.ignore_patterns("*.o", "*.a", "*.tki", "*.ll"))
        source = work / "main.tk"
        source.write_text("import std/sync::__factory_test\nfn main() -> i32 { return __factory_test() }\n")
        sdk = (ROOT / "lib/std/sync.tk").read_text()
        env = dict(os.environ, TOKA_LIB=str(library))

        def compile_case(label, flags=(), succeeds=True, diagnostic=None):
            obj = work / (label + ".o")
            result = subprocess.run([str(compiler), "--workspace-node", "native-factory-test",
                                     "--workspace-root", str(work), str(source), *flags,
                                     "-c", "-o", str(obj)], cwd=work, env=env,
                                    capture_output=True, text=True, timeout=45)
            if succeeds:
                if result.returncode or not obj.exists():
                    raise AssertionError(label + "\n" + result.stderr)
            elif result.returncode == 0 or obj.exists() or (diagnostic and diagnostic not in result.stderr):
                raise AssertionError(label + ": expected rejection with no artifact\n" + result.stderr)
            return result

        positive = fault_count = 0
        for kind in ("mutex", "rw", "cond"):
            argument = "" if kind == "cond" else "7"
            body = (f"auto owner# = __sync_{kind}_create<i32>({argument})\n"
                    f"__sync_{kind}_drop<i32>(owner#)\nreturn 0\n")
            (library / "std/sync.tk").write_text(sdk + "\npub fn __factory_test() -> i32 {\n" + body + "}\n")
            compile_case(kind)
            positive += 1
            for fault in FAULTS:
                compile_case(kind + "-" + fault, ["--native-sync-factory-fault=" + fault],
                             succeeds=False, diagnostic="E0701")
                fault_count += 1

        # Generic bodies are privately prepared. Cache reuse must seal the
        # retained body without re-running its consuming source expression.
        (library / "std/sync.tk").write_text(sdk + '''
fn __factory_generic<T>(seed: T) -> i32 {
    auto owner# = __sync_mutex_create<i32>(7)
    __sync_mutex_drop<i32>(owner#)
    return 0
}
pub fn __factory_test() -> i32 {
    auto first = __factory_generic(1)
    return __factory_generic(2)
}
''')
        compile_case("generic-cache")
        positive += 1
        # A body that fails semantic validation cannot seal or emit its
        # prepared factory edge, including on the second specialization hit.
        invalid = (library / "std/sync.tk").read_text().replace(
            "fn __factory_generic<T>(seed: T) -> i32", "fn __factory_generic<T>(seed: T) -> T").replace(
            "return 0\n}\npub fn __factory_test", "return true\n}\npub fn __factory_test")
        (library / "std/sync.tk").write_text(invalid)
        compile_case("invalid-parent", succeeds=False, diagnostic="E0408")

        # Return-nominal preparation must not make genuine factory recursion
        # admissible. Only metadata ordering changed, not Unchecked authority.
        recursive_sdk = sdk.replace(
            "fn __sync_mutex_create<'T: @Send>(cede 'data: T) -> Mutex<'T> {",
            "fn __sync_mutex_create<'T: @Send>(cede 'data: T) -> Mutex<'T> {\n"
            "    return __sync_mutex_create<'T>(cede 'data)")
        (library / "std/sync.tk").write_text(recursive_sdk + '''
pub fn __factory_test() -> i32 {
    auto owner = __sync_mutex_create<i32>(7)
    return 0
}
''')
        compile_case("recursive-native-factory", succeeds=False,
                     diagnostic="recursive generic specialization is not supported")

        # Origin continuity is not payload-write authority. These programs use
        # the same checked factory as the mutable positive above. The requested
        # owner-write contract must not be granted to a readonly view. Do not
        # confuse explicitly writable `handle#` fields with inherited owner
        # permission: field-local mutability is an existing independent rule.
        readonly_cases = {
            "readonly-local": '''
fn __factory_write(owner#: Mutex<i32>) { owner.handle = 0:Addr }
pub fn __factory_test() -> i32 {
    auto owner = __sync_mutex_create<i32>(7)
    __factory_write(owner#)
    return 0
}
''',
            "readonly-formal": '''
fn __factory_write(owner#: Mutex<i32>) { owner.handle = 0:Addr }
fn __factory_readonly(owner: Mutex<i32>) { __factory_write(owner#) }
pub fn __factory_test() -> i32 {
    auto owner# = __sync_mutex_create<i32>(7)
    __factory_readonly(owner)
    return 0
}
''',
        }
        for name, body in readonly_cases.items():
            (library / "std/sync.tk").write_text(sdk + body)
            normal = compile_case(name, succeeds=False, diagnostic="E04571")
            shadow = compile_case(name + "-shadow", ["--non-call-transfer-shadow=json"],
                                  succeeds=False, diagnostic="E04571")
            if normal.returncode != shadow.returncode or normal.stderr != shadow.stderr:
                raise AssertionError(name + ": normal/shadow diagnostic mismatch")
            if "E0701" in normal.stderr:
                raise AssertionError(name + ": permission must be rejected by Sema, not CodeGen")

        (library / "std/sync.tk").write_text(sdk + '''
pub fn __factory_test() -> i32 {
    auto owner = __sync_mutex_create<i32>(7)
    auto native = owner.handle
    return 0
}
''')
        compile_case("readonly-read")
        positive += 1

        # Resolver trust matters: an ordinary source function with the same
        # spelling is not assigned a private contract (and a native-plan fault
        # therefore cannot affect it).
        source.write_text('''
fn __sync_mutex_create(value: i32) -> i32 { return value }
fn main() -> i32 { return __sync_mutex_create(0) }
''')
        compile_case("same-name-user", ["--native-sync-factory-fault=missing"])
        positive += 1
        print(f"native factory plans: {positive} source positives, {fault_count} E0701/no-artifact faults, "
              "invalid-parent/recursive-factory and 2 readonly rejections (strict shadow parity); "
              "no thread witness granted")


if __name__ == "__main__":
    main()
