#!/usr/bin/env python3
"""Qualify generic, TKI, extern, async, and thread-state cede routes."""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/semantics/signature_driven_cede_direct"
FLAG = "--experimental-signature-driven-cede"


def run(command, cwd=ROOT):
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def compile_and_run(tokac, source, executable, extra=(), cwd=ROOT):
    compiled = run([str(tokac), *extra, str(source), "-o", str(executable)], cwd)
    require(compiled.returncode == 0,
            f"compile/link failed for {source}: {compiled.stderr}")
    executed = run([str(executable)], cwd)
    require(executed.returncode == 0,
            f"runtime failed for {source}: {executed.returncode}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    tokac = pathlib.Path(args.build_dir).resolve() / "bin/tokac"
    require(tokac.exists(), f"missing compiler: {tokac}")

    with tempfile.TemporaryDirectory(prefix="toka-cede-remaining-") as temp:
        temp_path = pathlib.Path(temp)

        for fixture, legacy_code in (("generic_runtime.tk", "E04570"),
                                     ("generic_method_runtime.tk", "E04509"),
                                     ("async_runtime.tk", "E04570"),
                                     ("thread_state_runtime.tk", "E04570")):
            source = FIXTURES / fixture
            legacy = run([str(tokac), "--check-only", str(source)])
            require(legacy.returncode != 0 and legacy_code in legacy.stderr,
                    f"legacy boundary missing for {fixture}")
            enabled = run([str(tokac), FLAG, "--check-only", str(source)])
            require(enabled.returncode == 0,
                    f"experimental route rejected {fixture}: {enabled.stderr}")
            compile_and_run(tokac, source, temp_path / fixture.removesuffix(".tk"),
                            (FLAG,))

        for fixture in ("generic_use_after_implicit.tk",
                        "async_use_after_implicit.tk"):
            moved = run([str(tokac), FLAG, "--check-only",
                         str(FIXTURES / fixture)])
            require(moved.returncode != 0 and "E0438" in moved.stderr and
                    "E04570" not in moved.stderr,
                    f"route did not invalidate source: {fixture}")

        for fixture in ("dynamic_trait_runtime.tk",
                        "dynamic_trait_multi_runtime.tk",
                        "indirect_unique_runtime.tk",
                        "partial_field_runtime.tk",
                        "partial_array_runtime.tk",
                        "method_multi_runtime.tk",
                        "callable_multi_runtime.tk",
                        "static_multi_runtime.tk",
                        "async_alternate_runtime.tk",
                        "lazy_generic_static_runtime.tk"):
            source = FIXTURES / fixture
            legacy = run([str(tokac), "--check-only", str(source)])
            require(legacy.returncode != 0,
                    f"legacy boundary unexpectedly accepted {fixture}")
            compile_and_run(tokac, source,
                            temp_path / fixture.removesuffix(".tk"), (FLAG,))

        for fixture, diagnostic in (
                ("dynamic_trait_use_after_implicit.tk", "E0438"),
                ("dynamic_trait_multi_alias_atomic.tk", "E0475"),
                ("indirect_unique_use_after_implicit.tk", "E0438"),
                ("partial_field_use_after_implicit.tk", "E0410"),
                ("partial_array_use_after_implicit.tk", "E0410"),
                ("method_multi_alias_atomic.tk", "E0475"),
                ("callable_multi_alias_atomic.tk", "E0475"),
                ("static_multi_alias_atomic.tk", "E0475"),
                ("async_method_alias_atomic.tk", "E0475"),
                ("async_static_alias_atomic.tk", "E0475"),
                ("async_callable_alias_atomic.tk", "E0475"),
                ("lazy_generic_static_alias_atomic.tk", "E0475")):
            rejected = run([str(tokac), FLAG, "--check-only",
                            str(FIXTURES / fixture)])
            require(rejected.returncode != 0 and
                    diagnostic in rejected.stderr,
                    f"missing final-route diagnostic for {fixture}")
            if diagnostic == "E0475" and not fixture.startswith("dynamic_trait"):
                require("E0438" not in rejected.stderr,
                        f"alternate route partially invalidated {fixture}")

        c_compiler = os.environ.get("CC", "cc")
        native_object = temp_path / "extern_take.o"
        native = run([c_compiler, "-c", str(FIXTURES / "extern_take.c"),
                      "-o", str(native_object)])
        require(native.returncode == 0, "failed to compile extern fixture")
        extern_source = FIXTURES / "extern_runtime.tk"
        legacy_extern = run([str(tokac), "--check-only", str(extern_source)])
        require(legacy_extern.returncode != 0 and
                "E04570" in legacy_extern.stderr,
                "legacy extern boundary changed")
        compile_and_run(tokac, extern_source, temp_path / "extern-implicit",
                        (FLAG, str(native_object)))
        compile_and_run(tokac, FIXTURES / "extern_explicit_runtime.tk",
                        temp_path / "extern-explicit", (str(native_object),))
        extern_moved = run([str(tokac), FLAG, "--check-only",
                            str(FIXTURES / "extern_use_after_implicit.tk")])
        require(extern_moved.returncode != 0 and
                "E0438" in extern_moved.stderr,
                "extern implicit move did not invalidate source")
        compile_and_run(tokac, FIXTURES / "extern_multi_runtime.tk",
                        temp_path / "extern-multi",
                        (FLAG, str(native_object)))
        extern_alias = run([str(tokac), FLAG, "--check-only",
                            str(FIXTURES / "extern_multi_alias_atomic.tk")])
        require(extern_alias.returncode != 0 and
                "E0475" in extern_alias.stderr and
                "E0438" not in extern_alias.stderr,
                "extern multi-argument failure was not atomic")

        for name in ("tki_provider.tk", "tki_consumer.tk",
                     "tki_use_after_implicit.tk"):
            shutil.copy2(FIXTURES / name, temp_path / name)
        provider = run([str(tokac), "-c", "tki_provider.tk", "-o",
                        "tki_provider.o"], temp_path)
        require(provider.returncode == 0 and
                (temp_path / "tki_provider.tki").is_file(),
                "failed to build TKI provider")

        source_consumer = run([str(tokac), FLAG, "tki_consumer.tk",
                               "tki_provider.o", "-o", "source-consumer"],
                              temp_path)
        require(source_consumer.returncode == 0,
                "source-backed TKI consumer failed")
        require(run([str(temp_path / "source-consumer")], temp_path).returncode == 0,
                "source-backed TKI consumer runtime failed")

        (temp_path / "tki_provider.tk").rename(
            temp_path / "tki_provider.tk.source-hidden")
        hidden_legacy = run([str(tokac), "--check-only", "tki_consumer.tk"],
                            temp_path)
        require(hidden_legacy.returncode != 0 and
                "E04570" in hidden_legacy.stderr,
                "source-hidden legacy boundary changed")
        hidden = run([str(tokac), FLAG, "tki_consumer.tk", "tki_provider.o",
                      "-o", "hidden-consumer"], temp_path)
        require(hidden.returncode == 0,
                "source-hidden signature-driven consumer failed")
        require(run([str(temp_path / "hidden-consumer")], temp_path).returncode == 0,
                "source-hidden consumer runtime failed")
        hidden_moved = run([str(tokac), FLAG, "--check-only",
                            "tki_use_after_implicit.tk"], temp_path)
        require(hidden_moved.returncode != 0 and
                "E0438" in hidden_moved.stderr,
                "source-hidden transfer did not invalidate source")

        for fixture in ("thread_closure_runtime.tk",
                        "thread_closure_forward_runtime.tk",
                        "thread_closure_use_after_implicit.tk"):
            legacy_closure = run([str(tokac), "--check-only",
                                  str(FIXTURES / fixture)])
            require(legacy_closure.returncode != 0 and
                    "E04570" in legacy_closure.stderr,
                    "legacy owning closure boundary accepted a bare move")
        compile_and_run(tokac, FIXTURES / "thread_closure_runtime.tk",
                        temp_path / "thread-closure", (FLAG,))
        compile_and_run(tokac, FIXTURES / "thread_closure_forward_runtime.tk",
                        temp_path / "thread-closure-forward", (FLAG,))
        moved_closure = run(
            [str(tokac), FLAG, "--check-only",
             str(FIXTURES / "thread_closure_use_after_implicit.tk")])
        require(moved_closure.returncode != 0 and
                "E0438" in moved_closure.stderr and
                "E04570" not in moved_closure.stderr,
                "signature-driven closure handoff did not invalidate source")

    print("Signature-driven remaining-route tests PASSED")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Signature-driven remaining-route tests FAILED: {error}",
              file=sys.stderr)
        sys.exit(1)
