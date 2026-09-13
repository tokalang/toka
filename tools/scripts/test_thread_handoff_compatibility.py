#!/usr/bin/env python3
"""Version gates before thread source activation; not a thread runtime gate."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def fnv_path(path):
    value = 14695981039346656037
    for byte in str(path.resolve()).encode():
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    compiler = Path(args.build_dir).resolve() / "bin/tokac"
    version = re.search(r'#define TOKA_COMPILER_INTERFACE_VERSION "([^"]+)"',
                       (ROOT / "include/toka/InterfaceVersion.h").read_text()).group(1)
    # G changes compiler-side generic contracts, not the v1 native layout or
    # handoff protocol. Reject pre-G TKI/cache while retaining that native ABI.
    require(version == "0.9.9-20", "compiler interface migration must be qualified")
    env = dict(os.environ, TOKA_LIB=str(ROOT / "lib"))

    with tempfile.TemporaryDirectory(prefix="toka-thread-version-") as directory:
        work = Path(directory)
        provider = work / "provider.tk"
        consumer = work / "consumer.tk"
        provider.write_text("pub fn value() -> i32 { return 41 }\n")
        consumer.write_text("import provider::{value}\nfn main() -> i32 { return value() - 41 }\n")

        def run(*flags):
            return subprocess.run([str(compiler), *map(str, flags)], cwd=work, env=env,
                                  capture_output=True, text=True, timeout=45)

        built = run("--emit-interface", "-c", provider, "-o", work / "provider.o")
        require(built.returncode == 0, built.stderr)
        tki = work / "provider.tki"
        current = tki.read_text()
        require("compiler_version: " + version in current, "wrong emitted interface version")
        stale = current.replace("compiler_version: " + version, "compiler_version: 0.9.9-19")
        provider.unlink()
        tki.write_text(stale)
        for flag, suffix in (("-c", ".o"), ("--emit-llvm", ".ll")):
            output = work / ("rejected" + suffix)
            rejected = run("-I", work, flag, consumer, "-o", output)
            require(rejected.returncode == 1 and "Compiler version mismatch" in rejected.stderr and
                    version in rejected.stderr and "0.9.9-19" in rejected.stderr and not output.exists(),
                    "old source-hidden TKI acquired authority\n" + rejected.stderr)

        # Put an actual stale object/interface pair in the resolver-owned cache.
        # A source-present fallback must parse source rather than trust the pair.
        provider.write_text("pub fn value() -> i32 { return 42 }\n")
        cache = work / "cache"
        (cache / "interfaces").mkdir(parents=True)
        (cache / "objects").mkdir()
        key = fnv_path(provider)
        (cache / "interfaces" / (key + ".tki")).write_text(stale)
        shutil.copyfile(work / "provider.o", cache / "objects" / (key + ".o"))
        env["TOKA_BUILD_DIR"] = str(cache)
        env["TOKA_USE_LIB_CACHE"] = "1"
        graph = run("-I", work, "--dump-dependencies=json", consumer)
        require(graph.returncode == 0, graph.stderr)
        record = json.loads(graph.stdout)["modules"][str(provider.resolve())]
        require(record["fallback_triggered"] and record["kind"] == "source" and
                record["cache_status"] == "CompilerVersionMismatch" and
                record["compiler_version"] == version, "old cache was not invalidated: " + str(record))
        consumer.write_text("import provider::{value}\nfn main() -> i32 { return value() - 42 }\n")
        executable = work / "fresh-source"
        rebuilt = run("-I", work, consumer, "-o", executable)
        require(rebuilt.returncode == 0, rebuilt.stderr)
        executed = subprocess.run([str(executable)], timeout=10)
        require(executed.returncode == 0, "stale object used instead of current source")

        # Removing source must turn the same incompatible cache into hard error.
        provider.unlink()
        tki.write_text(stale)
        rejected = run("-I", work, "--check-only", consumer)
        require(rejected.returncode == 1 and "Compiler version mismatch" in rejected.stderr,
                "source-hidden cached interface was accepted\n" + rejected.stderr)

        # Link-time guard for generated adapters: a legacy runtime without the
        # versioned strong symbol cannot satisfy the bridge. No fake runtime is run.
        cc = os.environ.get("CC", "clang")
        source = work / "requires_current.c"
        source.write_text('#include "toka_thread_handoff_v1.h"\n'
                          'int main(void) { toka_thread_require_compiler_0_9_9_19_v1(); return 0; }\n')
        legacy = work / "legacy.c"
        legacy.write_text("void toka_thread_require_compiler_0_9_9_18_v1(void) {}\n")
        artifact = work / "old-runtime"
        linked = subprocess.run([cc, "-I", str(ROOT / "lib/sys"), str(source), str(legacy),
                                 "-o", str(artifact)], capture_output=True, text=True, timeout=30)
        require(linked.returncode != 0 and not artifact.exists() and
                "toka_thread_require_compiler_0_9_9_19_v1" in linked.stderr,
                "old runtime unexpectedly linked\n" + linked.stderr)
    print("thread compatibility: 2 stale TKI no-artifact cases, stale cache source fallback/runtime, source-hidden cache rejection, old-runtime link rejection; no skips")


if __name__ == "__main__":
    main()
